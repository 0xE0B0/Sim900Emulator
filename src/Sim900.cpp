#include "Sim900.h"
#include "DebugInterface.h"

static inline Print& beginl(Print &stream) {
    static constexpr const char name[] = "SIM900";
    return beginl<name>(stream);
}

void Sim900::init() {
    ModemSerial.begin(MODEM_BAUD, SERIAL_8N1, MODEM_RX, MODEM_TX); // ESP32 <-> SA2700
    Serial << beginl << "Modem serial started at " << MODEM_BAUD << " baud, rx pin: " << MODEM_RX << ", tx pin: " << MODEM_TX << DebugInterface::endl;
    commands.push("ATZ"); // force sending startup messages
}

void Sim900::splitCommands() {
    int start = 0;
    rxBuffer.trim();
    if (rxBuffer.length() == 0)
        return;
    // remove leading "AT+" if present, but keep the plus sign
    if (rxBuffer.startsWith("AT+")) {
        rxBuffer = rxBuffer.substring(2);
    }
    Serial << beginl << blue << "RX: " << rxBuffer << DebugInterface::endl;
    // split multiple commands by semicolon
    while (start < rxBuffer.length()) {
        int end = rxBuffer.indexOf(';', start);
        String cmd;
        if (end == -1) {
            cmd = rxBuffer.substring(start);
            start = rxBuffer.length();
        } else {
            cmd = rxBuffer.substring(start, end);
            start = end + 1;
        }
        cmd.trim();
        if (cmd.length() > 0) {
            if (!commands.push(cmd))
                Serial << beginl << "Command buffer full, dropping: " << cmd << DebugInterface::endl;
        }
    }
    rxBuffer.clear();
}

void Sim900::sendToHost(String msg) {
    ModemSerial.print(msg + "\r\n");
    Serial << beginl << cyan << "TX: " << msg << DebugInterface::endl;
}

void Sim900::loop() {

    auto startDelay = [this](unsigned long duration=responseDelay) {
        delayCount = millis() + duration;
        state = ModemState::WaitToSend;
    };

    while (ModemSerial.available()) {
        char c = ModemSerial.read();
        if (c < 32 && c != '\r' && c != '\n' && c != 26) {
            Serial << beginl << red << "Received control character: " << (int)c << ", ignore" << DebugInterface::endl;
            continue;
        }
        if (c == '\r')
            continue; // ignore carriage return
        if (receiveSMS && c == 26)
            textEnd = true;  // Ctrl+Z, used to end SMS body
        if (c == '\n' || textEnd) {
            if (rxBuffer.length() > 0)
                splitCommands();
        } else {
            rxBuffer += c;
        }
    }

    switch (state) {
        case ModemState::Idle:
            if (commands.size() > 0) {
                state = ModemState::ProcessCommand;
            }
            break;
        case ModemState::ProcessCommand: {
            String cmd = commands.pop();

            if (receiveSMS) {
                if (smsRxBuffer.length() != 0)
                    smsRxBuffer += "|";  // separate SMS body parts
                smsRxBuffer += cmd;
                if (textEnd) {
                    textEnd = false;
                    Serial << beginl << "Received SMS from host: " << smsRxBuffer << DebugInterface::endl;
                    if (!msgRxBuffer.push(smsRxBuffer)) { // store the received SMS
                        Serial << beginl << red << "Message buffer full, dropping: " << smsRxBuffer << DebugInterface::endl;
                    }
                    smsRxBuffer.clear();
                    response.push("+CMGS: 123"); // simulate SMS sent response
                    response.push("OK");
                    receiveSMS = false; // done
                } else {
                    response.push(">"); // prompt for more SMS content
                }
                startDelay();
                return;
            }
            // process AT commands
            Serial << beginl << yellow << "Processing command: " << cmd << DebugInterface::endl;

            if ((cmd == "AT") ||
                (cmd == "ATH") ||                 // hang up
                (cmd == "+CMGF=1") ||             // set SMS mode to text
                (cmd == "+CNMI=3,1") ||           // enable unsolicited SMS notifications
                (cmd == "+CMGDA=\"DEL ALL\"") ||  // delete all messages                
                (cmd.startsWith("+IPR="))     ||  // set baud rate
                (cmd.startsWith("+CSCS="))    ||  // set character set
                (cmd.startsWith("+CMGD="))    ||  // delete SMS
                (cmd.startsWith("+CLTS="))    ||  // set SMS timestamp
                (cmd.startsWith("+CSCLK="))   ||  // set SMS storage
                (cmd.startsWith("+CMEE="))    ||  // set SMS error reporting
                (cmd.startsWith("+CSDT="))    ||  // set SMS data format
                (cmd.startsWith("+MORING="))  ||  // set SMS roaming
                (cmd.startsWith("+CSMINS="))  ||  // set SMS memory status
                (cmd.startsWith("+CSMP="))) {     // set SMS parameters
                    response.push("OK");
            } else if (cmd == "ATZ") {
                // reset the modem
                response.push("OK");
                response.push("RDY");
                response.push("+CSMINS: 1,1");
                response.push("+CFUN: 1");
                response.push("+CPIN: READY");
                response.push("Call Ready");
            } else if (cmd == "+CPOWD=1") {
                // power down the modem
                response.push("NORMAL POWER DOWN");
                response.push("OK");
                response.push("RDY");
                response.push("+CSMINS: 1,1");
                response.push("+CFUN: 1");
                response.push("+CPIN: READY");
                response.push("Call Ready");
            } else if (cmd.startsWith("+CPIN?")) {
                // simulate SIM card ready status
                response.push("+CPIN: READY");
                response.push("OK");
            } else if (cmd == "+CCLK?") {
                // current clock request
                response.push("+CCLK: \"25/01/01,12:00:00+08\"");
                response.push("OK");
            } else if (cmd == "+CSQ") {
                // signal quality request
                response.push("+CSQ: 23,0");
                response.push("OK");
            } else if (cmd == "+CREG?") {
                // network registration status request
                response.push("+CREG: 0,1");
                response.push("OK");
            } else if (cmd == "+CMGD=" + smsId) {
                // delete SMS by ID
                response.push("OK");
                smsTxBuffer.clear(); // clear the SMS content after deletion
            } else if (cmd == "+CMGR=" + smsId) {
                response.push("+CMGR: \"REC UNREAD\",\"" + phoneNumber + "\",,\"25/01/01,12:00:00+08\"");
                response.push(smsTxBuffer); // send the SMS content
                response.push("OK");
            } else if (cmd.startsWith("+CMGS=")) {
                // receive SMS from host
                int start = cmd.indexOf('"') + 1;
                int end = cmd.lastIndexOf('"');
                smsNumber = cmd.substring(start, end);
                receiveSMS = true;
                smsRxBuffer.clear(); // clear the buffer for new SMS content
                response.push(">");  // prompt for SMS body
            } else {
                Serial << beginl << red << "Unknown command: " << cmd << DebugInterface::endl;
                response.push("ERROR");
            }
            startDelay();
            break;
        }
        case ModemState::WaitToSend:
            // delay between sending responses
            if (millis() > delayCount) {
                state = ModemState::SendResponse;
            }
            break;
        case ModemState::SendResponse: {
            // send cummulated responses with a delay in between
            String resp = response.pop();
            sendToHost(resp);
            if (response.size() > 0) {
                startDelay();
            } else {
                state = ModemState::Idle;
            }
            break;
        }
        default:    
            break;
    }
}
