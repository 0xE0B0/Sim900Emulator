# pragma once

#include <Arduino.h>
#include <HardwareSerial.h>
#include <PrintStream.h>
#include <FIFObuf.h>

class Sim900 {
public:
    Sim900(){};
    void init();
    void loop();
    void splitCommands();
    void sendToHost(String msg);

private:
    // Alarm System UART
    static constexpr int MODEM_TX = 16;
    static constexpr int MODEM_RX = 17;
    static constexpr unsigned long MODEM_BAUD = 9600;

    static constexpr int responseDelay = 100; // delay in milliseconds before sending a response
    HardwareSerial ModemSerial{1};

    // note: pin and key must be configured in the SA2700 alarm system
    const String smsPin = "1207";
    const String smsKey = "PROG";
    const String smsId = "1";
    const String phoneNumber = "+4915773807779";

    String rxBuffer = "";
    FIFObuf<String> commands = FIFObuf<String>(16); // buffer for commands received from the host
    FIFObuf<String> response = FIFObuf<String>(16); // buffer for responses to be sent to the host
    String smsNumber = "";
    String smsRxBuffer = ""; // SMS modem to host
    String smsTxBuffer = ""; // SMS host to modem

    bool receiveSMS = false; // true if the modem is waiting for an SMS body
    bool textEnd = false;    // true if the last character was a text end (Ctrl+Z)
    unsigned long delayCount = 0;
    enum class ModemState {
        Idle,
        ReceiveCommand,
        ProcessCommand,
        WaitToSend,
        SendResponse
    } state = ModemState::Idle;

};
