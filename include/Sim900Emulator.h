#pragma once

#include <Arduino.h>
#include <HardwareSerial.h>
#include <PrintStream.h>
#include "Sim900.h"
#include "LEDControl.h"
#include "version.h"
#include "FixedString.h"
#include <ArduinoHA.h>

// min. update interval for MQTT status sensor
constexpr unsigned long MQTT_KEEP_ALIVE = 60000;  // milliseconds

 // built-in LED pin
constexpr uint8_t LED_PIN = 2;

// status LED behavior:
// - off: initializing / no WiFi
// - slow flash: WiFi connecting
// - fast flash: WiFi connected, MQTT connecting
// - on: MQTT connected
// - three quick flashes: status update sent (keep-alive or on change)

class Emulator {
public:
    Emulator(){};
    void init();
    void loop();

    enum class Command {
        GetStatus,
        ArmAway,
        ArmHome,
        Disarm
    };

    enum class CommandState {
        Unknown,
        Armed,
        Disarmed
    };

    bool sendCommand(const Command cmd);
    CommandState parseCommandResponse(const FixedString128 &msg);

    // status LED
    LEDControl led{LED_PIN};

private:

    // note: pin and key must be configured in the SA2700 alarm system
    static constexpr char smsPin[] = "1207";
    static constexpr char smsKey[] = "PROG";

    Sim900 sim900;
    FixedString128 currentStatus = FixedString128("N/A");
    unsigned long lastUpdate = 0;

    bool mqttConnected = false;

};
