#pragma once

#include <Arduino.h>
#include <HardwareSerial.h>
#include <PrintStream.h>
#include "Sim900.h"

const String VERSION = "1.0.0";

// USB UART for debugging
constexpr unsigned long MONITOR_BAUD = 115200;

class Emulator {
public:
    Emulator(){};
    void init();
    void loop();

    bool wifiConnected = false;
    bool mqttConnected = false;
private:
    Sim900 sim900;

};
