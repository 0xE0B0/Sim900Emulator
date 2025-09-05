#pragma once

#include <Arduino.h>
#include <HardwareSerial.h>
#include <PrintStream.h>
#include "Sim900.h"
#include "FixedString.h"
#include <ArduinoHA.h>

static constexpr char VERSION[] = "1.0.4";

// USB UART for debugging
constexpr unsigned long MONITOR_BAUD = 115200;

// min. update interval for MQTT status sensor
constexpr unsigned long MQTT_KEEP_ALIVE = 60000;  // milliseconds

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

private:

    // note: pin and key must be configured in the SA2700 alarm system
    static constexpr char smsPin[] = "1207";
    static constexpr char smsKey[] = "PROG";

    Sim900 sim900;
    FixedString128 currentStatus = FixedString128("N/A");
    unsigned long lastUpdate = 0;
};
