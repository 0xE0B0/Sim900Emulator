#include "Sim900Emulator.h"
#include "credentials.h"
#include "DebugInterface.h"
#include <WiFi.h>

// debug output module identifier
static inline Print& beginl(Print &stream) {
    static constexpr const char name[] = "EMU";
    return beginl<name>(stream);
}

Emulator emulator;
WiFiClient client;
HADevice device;
HAMqtt mqtt(client, device);

// note: HAMqtt must be initialized before any sensors
HASensor status("alarmcontrol_status");
HASensor source("alarmcontrol_source");
HASensor message("alarmcontrol_message");

HAButton updateCmd("alarmcontrol_update");
HAButton armCmd("alarmcontrol_arm");
HAButton disarmCmd("alarmcontrol_disarm");

// button command callback
static void onButtonCommand(HAButton* sender) {
    if (sender == &updateCmd) {
        emulator.sendCommand(Emulator::Command::GetStatus);
    } else if (sender == &armCmd) {
        emulator.sendCommand(Emulator::Command::ArmAway);
    } else if (sender == &disarmCmd) {
        emulator.sendCommand(Emulator::Command::Disarm);
    }
}

void setup() {
    Serial.begin(MONITOR_BAUD);
    Serial << beginl << "Emulator v" << VERSION << " started" << DI::endl;
    emulator.init();

#ifdef NETWORK_SSID_SCAN
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);
    Serial.println("Scanning...");
    int n = WiFi.scanNetworks();
    for (int i = 0; i < n; ++i) {
        Serial.println(WiFi.SSID(i));
    }
#endif

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    WiFi.setSleep(false);
    Serial << beginl << "Connecting to WiFi..." << DI::endl;
    emulator.led.setState(LEDControl::LedState::LED_FLASH_SLOW);

    WiFi.onEvent([](WiFiEvent_t event) {
        // handle WiFi events
        if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
            // connected and got IP
            Serial << beginl << "WiFi connected" << DI::endl;
            Serial << beginl << "SSID: " << WiFi.SSID() << DI::endl;
            Serial << beginl << "IP address: " << WiFi.localIP() << DI::endl; 
            emulator.led.setState(LEDControl::LedState::LED_FLASH_FAST);
            // start MQTT connection
            if (mqtt.begin(BROKER_ADDR, BROKER_USERNAME, BROKER_PASSWORD))
                Serial << beginl << "MQTT connecting... " << DI::endl;
        } else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
            // lost connection
            Serial << beginl << red << "Lost WiFi connection" << DI::endl;
            emulator.led.setState(LEDControl::LedState::LED_FLASH_SLOW);
        }
    });

    byte mac[6];
    WiFi.macAddress(mac);
    device.setUniqueId(mac, sizeof(mac));
    device.setName("Sim900Emulator");
    device.setSoftwareVersion(VERSION);
    device.setManufacturer("NotYourHome");
    device.setAvailability(true);
    device.enableSharedAvailability();
    device.enableLastWill();

}

void loop() {
    static bool mqttStartup = false;
    static bool mqttConnected = false;
    if (WiFi.status() == WL_CONNECTED) {
        mqtt.loop();
        bool connected = mqtt.isConnected();
        if (mqttConnected != connected) {
            mqttConnected = connected;
            if (mqttConnected) {
                emulator.led.setState(LEDControl::LedState::LED_ON);
            } else {
                emulator.led.setState(LEDControl::LedState::LED_FLASH_FAST);
            }
            Serial << beginl << (mqttConnected ? green : red) << "MQTT " << (mqttConnected ? "connected" : "disconnected") << DI::endl;
            if (!mqttStartup) {
                // publish initial state
                mqttStartup = true;
                status.setValue("N/A");
                source.setValue("N/A");
                message.setValue("N/A");
            }
        }
    }
    emulator.loop();
}

void Emulator::init() {

    sim900.init();

    status.setName("Status");
    source.setName("Source");
    message.setName("Message");
    updateCmd.setName("Update Status");
    armCmd.setName("Arm");
    disarmCmd.setName("Disarm");

    status.setIcon("mdi:alarm-panel");
    source.setIcon("mdi:source-branch");
    message.setIcon("mdi:message-text");
    updateCmd.setIcon("mdi:refresh");
    armCmd.setIcon("mdi:shield-lock");
    disarmCmd.setIcon("mdi:shield-lock-open");

    updateCmd.onCommand(onButtonCommand);
    armCmd.onCommand(onButtonCommand);
    disarmCmd.onCommand(onButtonCommand);

    // request initial status
    sendCommand(Command::GetStatus);

}

bool Emulator::sendCommand(const Command cmd) {
    FixedString128 fs;
        fs = smsKey;
        fs += " ";
        fs += smsPin;
        fs += " ";
    switch (cmd) {
        case Command::GetStatus:
            fs += "MOD?:";
            break;
        case Command::ArmAway:
            fs += "MODE:A";
            break;
        case Command::ArmHome:
            fs += "MODE:H";
            break;
        case Command::Disarm:
            fs += "MODE:D";
            break;
        default:
            return false;
    }
    return sim900.sendMessage(fs);
}

Emulator::CommandState Emulator::parseCommandResponse(const FixedString128 &msg) {
    char *modPos = strstr(msg.c_str(), "MOD?:");
    if (modPos == nullptr) {
        modPos = strstr(msg.c_str(), "MODE:");
    }
    if (modPos == nullptr) {
        return CommandState::Unknown;
    }
    modPos += 5; // skip "MOD?:" or "MODE:"
    if (*modPos == 'A' && *(modPos + 1) == '\0') {
        return CommandState::Armed;
    } else if (*modPos == 'H' && *(modPos + 1) == '\0') {
        return CommandState::Armed;
    } else if (*modPos == 'D' && *(modPos + 1) == '\0') {
        return CommandState::Disarmed;
    }
    return CommandState::Unknown;
}

void Emulator::loop() {
    sim900.loop();
    led.loop();
    bool sendUpdate = false;
    FixedString128 msg;
    if (sim900.getMessage(msg)) {
        Serial << beginl << blue << "MQTT message: " << msg << DI::endl;
        message.setValue(msg.c_str());
        // parse message as tuples: source|status|source|status|...
        // e.g. "FB Handsender|Scharf"
        //      "BW Flur|Einbruch"
        //      "BW Wohnzimmer|Einbruch|BW Kueche|Einbruch"
        //      "Confirmed|PROG 1207 MODE?:A"  (response to command)
        std::vector<FixedString128> sources;
        FixedString128 statusValue;
        const char* s = msg.c_str();
        int len = msg.length();
        int pos = 0;
        int srcIdx = 0;
        while (pos < len) {
            // parse source
            int start = pos;
            while (pos < len && s[pos] != '|') ++pos;
            if (pos >= len) break; // no status follows
            int srcEnd = pos - 1;
            // trim source
            while (start <= srcEnd && isspace((unsigned char)s[start])) ++start;
            while (srcEnd >= start && isspace((unsigned char)s[srcEnd])) --srcEnd;
            int srcLen = srcEnd >= start ? (srcEnd - start + 1) : 0;
            ++pos; // skip '|'
            // parse status
            int statStart = pos;
            while (pos < len && s[pos] != '|') ++pos;
            int statEnd = pos - 1;
            // trim status
            while (statStart <= statEnd && isspace((unsigned char)s[statStart])) ++statStart;
            while (statEnd >= statStart && isspace((unsigned char)s[statEnd])) --statEnd;
            int statLen = statEnd >= statStart ? (statEnd - statStart + 1) : 0;

            if (srcLen > 0 && statLen > 0) {
                FixedString128 fs;
                int copyLen = srcLen;
                if (copyLen >= (int)sizeof(fs.buf)) copyLen = sizeof(fs.buf) - 1;
                memcpy(fs.buf, s + start, copyLen);
                fs.buf[copyLen] = '\0';
                fs.len = copyLen;
                sources.push_back(fs);

                FixedString128 statusBuf;
                int copyStat = statLen;
                if (copyStat > 127) copyStat = 127;
                memcpy(statusBuf.buf, s + statStart, copyStat);
                statusBuf.buf[copyStat] = '\0';
                statusBuf.len = copyStat;

                if (srcIdx == 0) {
                    statusValue.set(statusBuf);
                } else if (strcmp(statusValue.c_str(), statusBuf) != 0) {
                    // log inconsistency with original strings for clarity
                    FixedString128 tmpSrc;
                    int tl = copyLen < 128 ? copyLen : 128;
                    memcpy(tmpSrc.buf, s + start, tl);
                    tmpSrc.buf[tl] = '\0';
                    tmpSrc.len = tl;
                    Serial << beginl << red << "Inconsistent status for source: " << tmpSrc.c_str() << ", expected: " << statusValue.c_str() << ", got: " << statusBuf.c_str() << DI::endl;
                }
                // log parsed pair
                FixedString128 tmpSrcLog;
                int l = copyLen < 128 ? copyLen : 128;
                memcpy(tmpSrcLog.buf, s + start, l);
                tmpSrcLog.buf[l] = '\0';
                tmpSrcLog.len = l;
                Serial << beginl << "Parsed: [" << tmpSrcLog.c_str() << "] [" << statusBuf.c_str() << "]" << DI::endl;
            }
            ++srcIdx;
        }
        // note: observe order, set source first, then status,
        // to allow HA to read up-to-date sources when triggering on status change
        if (!sources.empty()) {
            // check if this is a response to a previous command
            if (sources.size() == 1 && sources[0].startsWith("Confirmed")) {
                sources.clear();
                sources.push_back(FixedString128("Kommandobestaetigung"));
                auto cmdState = parseCommandResponse(statusValue);
                Serial << beginl << "Command response, state: " << (int)cmdState << DI::endl;
                switch(cmdState) {
                    case CommandState::Armed:
                        statusValue.set("Scharf");
                        break;
                    case CommandState::Disarmed:
                        statusValue.set("Unscharf");
                        break;
                    case CommandState::Unknown:
                        statusValue.set("Unbekannt");
                        break;
                }
            }
            // join sources with comma into a FixedString128
            FixedString128 sourcesList;
            for (size_t i = 0; i < sources.size(); ++i) {
                if (i > 0) sourcesList.append(", ");
                sourcesList.append(sources[i].c_str());
            }
            source.setValue(sourcesList.c_str());
            Serial << beginl << green << "MQTT Sources: " << sourcesList.c_str() << DI::endl;
        }
        if (statusValue.length() > 0) {
            currentStatus.set(statusValue.c_str());
            sendUpdate = true;
        }
    }
    if ((millis() - lastUpdate) > MQTT_KEEP_ALIVE) {
        lastUpdate = millis();
        sendUpdate = true;
    }
    if (sendUpdate) {
        status.setValue(currentStatus.c_str());
        Serial << beginl << green << "MQTT Status: " << currentStatus << DI::endl;
        led.indicate(3);  // flash LED to indicate status update
    }
}
