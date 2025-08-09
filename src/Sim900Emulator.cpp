#include "Sim900Emulator.h"
#include "credentials.h"
#include "DebugInterface.h"
#include <WiFi.h>
#include <ArduinoHA.h>

static inline Print& beginl(Print &stream) {
    static constexpr const char name[] = "EMU";
    return beginl<name>(stream);
}

Emulator emulator;
WiFiClient client;
HADevice device;
HAMqtt mqtt(client, device);

HASensor alarmControl("alarmControl");

void setup() {
    Serial.begin(MONITOR_BAUD);
    Serial << beginl << "Emulator v" << VERSION << " started" << DebugInterface::endl;

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
    Serial << beginl << "Connecting to WiFi..." << DebugInterface::endl;
    int timeout = 30; // seconds
    while (timeout-- > 0 && WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial << ".";
    }
    emulator.wifiConnected = (WiFi.status() == WL_CONNECTED);
    if (!emulator.wifiConnected) {
        Serial << beginl << "Failed to connect to WiFi" << DebugInterface::endl;
        return;
    }
    Serial << beginl << "WiFi connected" << DebugInterface::endl;
    Serial << beginl << "SSID: " << WiFi.SSID() << DebugInterface::endl;
    Serial << beginl << "IP address: " << WiFi.localIP() << DebugInterface::endl;

    if (emulator.wifiConnected) {
        byte mac[6];
        WiFi.macAddress(mac);
        device.setUniqueId(mac, sizeof(mac));
        
        device.setName("Sim900Emulator");
        device.setSoftwareVersion(VERSION.c_str());
        device.setManufacturer("NotYourHome");
        alarmControl.setIcon("mdi:alarm-panel");
        alarmControl.setName("alarmcontrol_status");
        mqtt.begin(BROKER_ADDR, BROKER_USERNAME, BROKER_PASSWORD);
        emulator.mqttConnected = mqtt.isConnected();
        Serial << beginl << "MQTT connected: " << emulator.mqttConnected << DebugInterface::endl;
    }
    emulator.init();
}

void loop() {
    if (emulator.wifiConnected) {
        mqtt.loop();
    }
    emulator.loop();
}

void Emulator::init() {
   sim900.init();
}

void Emulator::loop() {
    sim900.loop();
    if (sim900.messageAvailable()) {
        String msg = sim900.getMessage();
        Serial << beginl << blue<< "Received message: " << msg << DebugInterface::endl;
        int start = 0;
        while (start < msg.length()) {
            int end = msg.indexOf('|', start);
            String part;
            if (end == -1) {
                part = msg.substring(start);
                start = msg.length();
            } else {
                part = msg.substring(start, end);
                start = end + 1;
            }
            part.trim();
            if (part.length() > 0) {
                Serial << beginl << "Processing part: " << part << DebugInterface::endl;
                if (part.startsWith("Scharf")) {
                    alarmControl.setValue("armed");
                    Serial << beginl << blue << "Alarm armed" << DebugInterface::endl;
                } else if (part.startsWith("Unscharf")) {
                    alarmControl.setValue("disarmed");
                    Serial << beginl << green << "Alarm disarmed" << DebugInterface::endl;
                } else if (part.startsWith("Einbruch")) {
                    alarmControl.setValue("triggered");
                    Serial << beginl << red << "Alarm triggered" << DebugInterface::endl;
                }
            }
        }
    }
}
