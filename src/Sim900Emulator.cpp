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
    device.setSoftwareVersion(VERSION);
    device.setManufacturer("NotYourHome");
    device.setAvailability(true);
    device.enableSharedAvailability();
    device.enableLastWill();

    mqtt.begin(BROKER_ADDR, BROKER_USERNAME, BROKER_PASSWORD);
    Serial << beginl << "MQTT connecting... " << DI::endl;

    // initialize HA sensor metadata/value after MQTT/HA are initialized to avoid
    // dereferencing uninitialized internals inside the HASensor implementation
    message.setValue("N/A");
    message.setName("Message");
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
    bool sendUpdate = false;
    if (sim900.messageAvailable()) {
    FixedString128 msg;
    if (!sim900.getMessage(msg)) {
        return;
    }
        Serial << beginl << blue << "MQTT message: " << msg << DI::endl;
        message.setValue(msg.c_str());
        // parse message as tuples: source|status|source|status|...
        // e.g. "FB Handsender|Scharf"
        //      "BW Flur|Einbruch"
        //      "BW Wohnzimmer|Einbruch|BW Kueche|Einbruch"
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
    }
}
