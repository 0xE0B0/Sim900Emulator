#pragma once

#include <Arduino.h>
#include <HardwareSerial.h>
#include <PrintStream.h>
#include "Sim900.h"
#include <ArduinoHA.h>

const String VERSION = "1.0.2";

// USB UART for debugging
constexpr unsigned long MONITOR_BAUD = 115200;

// min. update interval for MQTT status sensor
constexpr unsigned long MQTT_KEEP_ALIVE = 60000;  // milliseconds

class Emulator {
public:
    Emulator(){};
    void init();
    void loop();

private:
    Sim900 sim900;
    String currentStatus = "N/A";
    unsigned long lastUpdate = 0;
};
