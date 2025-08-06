#include "Sim900Emulator.h"
#include "credentials.h"

#include <WiFi.h>
#include <ArduinoHA.h>

Emulator emulator;
WiFiClient client;
HADevice device;
HAMqtt mqtt(client, device);

HASensor alarmControl("alarmControl");

void setup() {
    Serial.begin(MONITOR_BAUD);
    Serial << "Sim900 Emulator v" << VERSION << " started" << endl;

    // WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    // WiFi.setSleep(false);
    // Serial << "Connecting to WiFi";
    // int timeout = 30; // seconds
    // while (timeout-- > 0 && WiFi.status() != WL_CONNECTED) {
    //     delay(1000);
    //     Serial << ".";
    // }
    // emulator.wifiConnected = (WiFi.status() == WL_CONNECTED);
    // if (!emulator.wifiConnected) {
    //     Serial << "Failed to connect to WiFi";
    //     return;
    // }
    // Serial << "WiFi connected";

    // byte mac[6];
    // WiFi.macAddress(mac);
    // device.setUniqueId(mac, sizeof(mac));
    
    // device.setName("Sim900Emulator");
    // device.setSoftwareVersion(VERSION.c_str());
    // device.setManufacturer("NotYourHome");
    // alarmControl.setIcon("mdi:home");
    // alarmControl.setName("alarmcontrol_status");
    // mqtt.begin(BROKER_ADDR, BROKER_USERNAME, BROKER_PASSWORD);
    emulator.init();
}

void loop() {
    // mqtt.loop();
    emulator.loop();

    // alarmControl.setValue("armed");
    // Unlike the other device types, the HASensor doesn't store the previous value that was set.
    // It means that the MQTT message is produced each time the setValue method is called.
}

void Emulator::init() {
   sim900.init();
}

void Emulator::loop() {
    sim900.loop();
}
