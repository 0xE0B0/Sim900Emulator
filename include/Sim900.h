# pragma once

#include <Arduino.h>
#include <HardwareSerial.h>
#include <PrintStream.h>
#include <FIFObuf.h>
#include "FixedString.h"

class Sim900 {
public:
    Sim900(){};
    void init();
    void loop();
    void splitCommands();
    void sendToHost(const FixedString128 &msg);

    inline bool messageAvailable() {
        return msgRxBuffer.size() > 0;
    }
    
    inline bool getMessage(FixedString128 &out) {
        if (messageAvailable()) {
            out = msgRxBuffer.pop();
            return true;
        }
        out.clear();
        return false;
    }

    inline bool sendMessage(const FixedString128 &msg) {
        return msgTxBuffer.push(msg);
    }

private:
    // Alarm System UART
    static constexpr int MODEM_TX = 16;
    static constexpr int MODEM_RX = 17;
    static constexpr unsigned long MODEM_BAUD = 9600;

    static constexpr int responseDelay = 100; // delay in milliseconds before sending a response
    HardwareSerial ModemSerial{1};

    static constexpr char smsId[] = "1";
    static constexpr char phoneNumber[] = "+4915773807779";

    FixedString128 rxBuffer; 
    FIFObuf<FixedString128> commands = FIFObuf<FixedString128>(16); // buffer for commands received from the host
    FIFObuf<FixedString128> response = FIFObuf<FixedString128>(16); // buffer for responses to be sent to the host
    // sms processing buffers
    FixedString128 smsNumber; 
    FixedString128 smsRxBuffer; // SMS host to modem
    FixedString128 smsTxBuffer; // SMS modem to host
    FIFObuf<FixedString128> msgRxBuffer = FIFObuf<FixedString128>(16);  // buffer for received SMS messages
    FIFObuf<FixedString128> msgTxBuffer = FIFObuf<FixedString128>(16);  // buffer for transmitted SMS messages

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
