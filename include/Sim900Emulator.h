#pragma once

#include <Arduino.h>
#include <HardwareSerial.h>
#include <PrintStream.h>
#include "Sim900.h"

const String VERSION = "1.0.0";

#define WIFI_SSID     "NotYourHome"
#define WIFI_PASSWORD "NotYourPassword"

#define BROKER_ADDR IPAddress(192,168,0,17)
#define BROKER_USERNAME     "user" // replace with your credentials
#define BROKER_PASSWORD     "pass"

// USB UART for debugging
constexpr unsigned long MONITOR_BAUD = 115200;

class Emulator {
public:
    Emulator(){};
    void init();
    void loop();

private:
    Sim900 sim900;
};
