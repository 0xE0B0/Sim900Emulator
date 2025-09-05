#include "Sim900.h"
#include "DebugInterface.h"

// debug output module identifier
static inline Print& beginl(Print &stream) {
    static constexpr const char name[] = "SIM900";
    return beginl<name>(stream);
}

void Sim900::init() {
    ModemSerial.begin(MODEM_BAUD, SERIAL_8N1, MODEM_RX, MODEM_TX); // ESP32 <-> SA2700
    Serial << beginl << "Modem serial started at " << MODEM_BAUD << " baud, rx pin: " << MODEM_RX << ", tx pin: " << MODEM_TX << DI::endl;
    commands.push(FixedString128("ATZ")); // force sending startup messages
}

void Sim900::splitCommands() {
    int start = 0;
    rxBuffer.trim();
    if (rxBuffer.length() == 0)
        return;
    // remove leading "AT+" if present, but keep the plus sign
    if (rxBuffer.startsWith("AT+")) {
        rxBuffer.remove(0, 2);
    }
    Serial << beginl << blue << "RX: " << rxBuffer << DI::endl;
    // split multiple commands by semicolon
    const char* s = rxBuffer.c_str();
    int len = rxBuffer.length();
    while (start < len) {
        int end = start;
        while (end < len && s[end] != ';') ++end;
        // copy from start..end-1 into fixed buffer
        int seglen = end - start;
        // trim whitespace from both ends
        int segStart = start;
        int segEnd = end - 1;
        while (segStart <= segEnd && isspace((unsigned char)s[segStart])) ++segStart;
        while (segEnd >= segStart && isspace((unsigned char)s[segEnd])) --segEnd;
        if (segEnd >= segStart) {
            int copyLen = segEnd - segStart + 1;
            FixedString128 cfs;
            if (copyLen >= (int)sizeof(cfs.buf)) copyLen = sizeof(cfs.buf) - 1;
            memcpy(cfs.buf, s + segStart, copyLen);
            cfs.buf[copyLen] = '\0';
            cfs.len = copyLen;
            if (!commands.push(cfs)) {
                Serial << beginl << red << "Command buffer full, dropping: " << cfs.c_str() << DI::endl;
            }
        }
        start = (end < len && s[end] == ';') ? end + 1 : end;
    }
    rxBuffer.clear();
}

void Sim900::sendToHost(const FixedString128 &msg) {
    ModemSerial.print(msg.c_str());
    ModemSerial.print("\r\n");
    Serial << beginl << cyan << "TX: " << msg.c_str() << DI::endl;
}

void Sim900::loop() {

    auto startDelay = [this](unsigned long duration=responseDelay) {
        delayCount = millis() + duration;
        state = ModemState::WaitToSend;
    };

    while (ModemSerial.available()) {
        char c = ModemSerial.read();
        if (c < 32 && c != '\r' && c != '\n' && c != 26) {
            Serial << beginl << red << "Received control character: " << (int)c << ", ignore" << DI::endl;
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
                // check if there are commands to be processed
                state = ModemState::ProcessCommand;
            } else if (msgTxBuffer.size() > 0) {
                // check if there are SMS messages to be sent to the host
                FixedString128 fs;
                fs = msgTxBuffer.pop();
                smsTxBuffer = fs;  // set SMS body to be sent
                // send unsolicited SMS indication to the host
                snprintf(fs.buf, sizeof(fs.buf), "+CMTI: \"SM\",%s", smsId);
                fs.len = strnlen(fs.buf, sizeof(fs.buf)-1);
                response.push(fs);
                Serial << beginl << green << "Indicate SMS to host, id: " << smsId << DI::endl;
                startDelay();   
            }
            break;
        case ModemState::ProcessCommand: {
            FixedString128 cmdFs;
            const char* ccmd;
            cmdFs = commands.pop();
            ccmd = cmdFs.c_str();

            if (receiveSMS) {
                if (smsRxBuffer.length() != 0)
                    smsRxBuffer += "|";  // separate SMS body parts
                smsRxBuffer += ccmd;
                if (textEnd) {
                    textEnd = false;
                    Serial << beginl << "Received SMS from host: " << smsRxBuffer << DI::endl;
                    FixedString128 fs(smsRxBuffer);
                    if (!msgRxBuffer.push(fs)) { // store the received SMS
                        Serial << beginl << red << "Message buffer full, dropping: " << smsRxBuffer << DI::endl;
                    }      
                    smsRxBuffer.clear();
                    response.push(FixedString128("+CMGS: 123")); // simulate SMS sent response
                    response.push(FixedString128("OK"));
                    receiveSMS = false; // done
                } else {
                    response.push(FixedString128(">")); // prompt for more SMS content
                }
                startDelay();
                return;
            }
            // process AT commands using C strings to avoid String allocations
            Serial << beginl << yellow << "Processing command: " << ccmd << DI::endl;
            if (strcmp(ccmd, "AT") == 0 ||           // basic attention command
                strcmp(ccmd, "ATH") == 0 ||          // hang up
                strcmp(ccmd, "+CMGF=1") == 0 ||      // set SMS text mode
                strcmp(ccmd, "+CNMI=3,1") == 0 ||    // new SMS message indications
                strcmp(ccmd, "+CMGDA=\"DEL ALL\"") == 0 ||  // delete all SMS messages
                strncmp(ccmd, "+IPR=", 5) == 0 ||    // set fixed baud rate
                strncmp(ccmd, "+CSCS=", 6) == 0 ||   // set character set
                strncmp(ccmd, "+CMGD=", 6) == 0 ||   // delete SMS message by index
                strncmp(ccmd, "+CLTS=", 6) == 0 ||   // set local time stamp
                strncmp(ccmd, "+CSCLK=", 7) == 0 ||  // set slow clock mode
                strncmp(ccmd, "+CMEE=", 6) == 0 ||   // set extended error reporting
                strncmp(ccmd, "+CSDT=", 6) == 0 ||   // set data type
                strncmp(ccmd, "+MORING=", 8) == 0 || // set MO ring
                strncmp(ccmd, "+CSMINS=", 8) == 0 || // SIM card status
                strncmp(ccmd, "+CSMP=", 6) == 0) {   // set SMS parameters
                response.push(FixedString128("OK"));
            } else if (strcmp(ccmd, "ATZ") == 0) {
                // reset the modem
                response.push(FixedString128("OK"));
                response.push(FixedString128("RDY"));
                response.push(FixedString128("+CSMINS: 1,1"));
                response.push(FixedString128("+CFUN: 1"));
                response.push(FixedString128("+CPIN: READY"));
                response.push(FixedString128("Call Ready"));
            } else if (strcmp(ccmd, "+CPOWD=1") == 0) {
                // power down the modem
                response.push(FixedString128("NORMAL POWER DOWN"));
                response.push(FixedString128("OK"));
                response.push(FixedString128("RDY"));
                response.push(FixedString128("+CSMINS: 1,1"));
                response.push(FixedString128("+CFUN: 1"));
                response.push(FixedString128("+CPIN: READY"));
                response.push(FixedString128("Call Ready"));
            } else if (strncmp(ccmd, "+CPIN?", 6) == 0) {
                // simulate SIM card ready status
                response.push(FixedString128("+CPIN: READY"));
                response.push(FixedString128("OK"));
            } else if (strcmp(ccmd, "+CCLK?") == 0) {
                // current clock request
                response.push(FixedString128("+CCLK: \"25/01/01,12:00:00+08\""));
                response.push(FixedString128("OK"));
            } else if (strcmp(ccmd, "+CSQ") == 0) {
                // signal quality request
                response.push(FixedString128("+CSQ: 23,0"));
                response.push(FixedString128("OK"));
            } else if (strcmp(ccmd, "+CREG?") == 0) {
                // network registration status request
                response.push(FixedString128("+CREG: 0,1"));
                response.push(FixedString128("OK"));
            } else if (strncmp(ccmd, "+CMGR=", 6) == 0 && strcmp(ccmd + 6, smsId) == 0) {
                // read SMS message by index
                if (smsTxBuffer.length() > 0) {
                    FixedString160 tmp;
                    snprintf(tmp.buf, sizeof(tmp.buf), "+CMGR: \"REC UNREAD\",\"%s\",,\"25/01/01,12:00:00+08\"", phoneNumber);
                    tmp.len = strnlen(tmp.buf, sizeof(tmp.buf)-1);
                    response.push(FixedString128(tmp.c_str()));
                    // send the SMS content
                    FixedString128 fs(smsTxBuffer);
                    response.push(FixedString128(smsTxBuffer));
                    response.push(FixedString128("OK"));
                    smsTxBuffer.clear(); // clear the SMS content after sending
                } else {
                    response.push(FixedString128("ERROR")); // no SMS at this index, strange ...
                }
            } else if (strncmp(ccmd, "+CMGS=", 6) == 0) {
                // receive SMS from host
                // find first and last quote
                int start = -1;
                int end = -1;
                int clen = strlen(ccmd);
                for (int i = 0; i < clen; ++i) {
                    if (ccmd[i] == '"') { start = i + 1; break; }
                }
                for (int i = clen - 1; i >= 0; --i) {
                    if (ccmd[i] == '"') { end = i; break; }
                }
                if (start == -1 || end == -1 || end <= start) {
                    start = 0; end = 0;
                }
                int copyLen = end - start;
                if (copyLen >= (int)sizeof(smsNumber.buf)) copyLen = sizeof(smsNumber.buf) - 1;
                memcpy(smsNumber.buf, ccmd + start, copyLen);
                smsNumber.buf[copyLen] = '\0';
                smsNumber.len = copyLen;
                receiveSMS = true;
                smsRxBuffer.clear(); // clear the buffer for new SMS content
                response.push(FixedString128(">"));  // prompt for SMS body
            } else {
                Serial << beginl << red << "Unknown command: " << ccmd << DI::endl;
                response.push(FixedString128("ERROR"));
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
            FixedString128 respFs;
            respFs = response.pop();
            sendToHost(respFs);
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
