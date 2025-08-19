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

void setup() {
    Serial.begin(MONITOR_BAUD);
    Serial << beginl << "Emulator v" << VERSION << " started" << DI::endl;

    // WiFi.mode(WIFI_STA);
    // WiFi.disconnect();
    // delay(100);
    // Serial.println("Scanning...");
    // int n = WiFi.scanNetworks();
    // for (int i = 0; i < n; ++i) {
    //     Serial.println(WiFi.SSID(i));
    // }

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    WiFi.setSleep(false);
    Serial << beginl << "Connecting to WiFi..." << DI::endl;
    int timeout = 60; // seconds
    while (timeout-- > 0 && WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial << ".";
    }
    if (WiFi.status() != WL_CONNECTED) {
        Serial << beginl << red << "Failed to connect to WiFi, stopping" << DI::endl;
        return;
    }
    Serial << beginl << "WiFi connected" << DI::endl;
    Serial << beginl << "SSID: " << WiFi.SSID() << DI::endl;
    Serial << beginl << "IP address: " << WiFi.localIP() << DI::endl;
    emulator.init();

    byte mac[6];
    WiFi.macAddress(mac);
    device.setUniqueId(mac, sizeof(mac));
    
    device.setName("Sim900Emulator");
    device.setSoftwareVersion(VERSION.c_str());
    device.setManufacturer("NotYourHome");
    device.setAvailability(true);
    device.enableSharedAvailability();
    device.enableLastWill();

    mqtt.begin(BROKER_ADDR, BROKER_USERNAME, BROKER_PASSWORD);
    Serial << beginl << "MQTT connecting... " << DI::endl;
}

void loop() {
    static bool mqttConnected = false;
    if (WiFi.status() == WL_CONNECTED) {
        mqtt.loop();
        if (mqttConnected != mqtt.isConnected()) {
            mqttConnected = mqtt.isConnected();
            Serial << beginl << (mqttConnected ? green : red) << "MQTT " << (mqttConnected ? "connected" : "disconnected") << DI::endl;
            if (mqttConnected) {
                // publish initial state
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

    status.setIcon("mdi:alarm-panel");
    source.setIcon("mdi:source-branch");
    message.setIcon("mdi:message-text");

}

void Emulator::loop() {
    sim900.loop();
    if (sim900.messageAvailable()) {
        String msg = sim900.getMessage();
        Serial << beginl << blue << "MQTT message: " << msg << DI::endl;
        message.setValue(msg.c_str());
        // parse message as tuples: source|status|source|status|...
        // e.g. "FB Handsender|Scharf"
        //      "BW Flur|Einbruch"
        //      "BW Wohnzimmer|Einbruch|BW Kueche|Einbruch"
        std::vector<String> sources;
        String statusValue;
        int start = 0;
        int srcIdx = 0;
        while (start < msg.length()) {
            // get source
            int end = msg.indexOf('|', start);
            String sourcePart;
            if (end == -1) {
                break;
            } else {
                sourcePart = msg.substring(start, end);
                start = end + 1;
            }
            sourcePart.trim();
            // get status
            end = msg.indexOf('|', start);
            String statusPart;
            if (end == -1) {
                statusPart = msg.substring(start);
                start = msg.length();
            } else {
                statusPart = msg.substring(start, end);
                start = end + 1;
            }
            statusPart.trim();

            if (sourcePart.length() > 0 && statusPart.length() > 0) {
                sources.push_back(sourcePart);
                if (srcIdx == 0) {
                    statusValue = statusPart;
                } else if (statusValue != statusPart) {
                    Serial << beginl << red << "Inconsistent status for source: " << sourcePart << ", expected: " << statusValue << ", got: " << statusPart << DI::endl;
                }
                Serial << beginl << "Parsed: [" << sourcePart << "] [" << statusPart << "]" << DI::endl;
            }
            srcIdx++;
        }
        // note: observe order, set source first, then status,
        // to allow HA to read up-to-date sources when triggering on status change
        if (!sources.empty()) {
            // join sources with comma
            String sourcesList;
            for (size_t i = 0; i < sources.size(); ++i) {
                if (i > 0) sourcesList += ", ";
                sourcesList += sources[i];
            }
            source.setValue(sourcesList.c_str());
            Serial << beginl << green << "MQTT Sources: " << sourcesList << DI::endl;
        }
        if (statusValue.length() > 0) {
            status.setValue(statusValue.c_str());
            Serial << beginl << green << "MQTT Status: " << statusValue << DI::endl;
        }
    }
}
