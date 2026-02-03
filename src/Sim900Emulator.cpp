#include <FS.h>  // must be included first
#include <SPIFFS.h>
#include "Sim900Emulator.h"
#include "DebugInterface.h"
#include <WiFi.h>
#include <WifiManager.h>
#include <ArduinoJson.h>

#define STRINGIFY(x) #x
#define STRINGIFY_EXP(x) STRINGIFY(x)

#define VERSION STRINGIFY_EXP(EMU_VERSION_MAJOR) "." STRINGIFY_EXP(EMU_VERSION_MINOR) "." STRINGIFY_EXP(EMU_VERSION_SUB)

// Access point name
static constexpr const char* APName = "Sim900Emulator_AP";
// Configuration file path in SPIFFS filesystem
static constexpr const char* configFilePath = "/config.json";

struct WifiSettings {
    char apName[20] = "";
    char apPassword[20] = "";
};

struct MqttSettings {
    char clientId[20] = "Sim900Emulator";
    char hostName[40] = "192.168.178.220";
    char port[6] = "1883";
    char user[20] = "mqtt-user";
    char password[20];
    char clientId_id[15] = "mqtt_client_id";
    char hostName_id[14] = "mqtt_hostname";
    char port_id[10] = "mqtt_port";
    char user_id[10] = "mqtt_user";
    char password_id[14] = "mqtt_password";
};

// debug output module identifier
static inline Print& beginl(Print &stream) {
    static constexpr const char name[] = "EMU";
    return beginl<name>(stream);
}

Emulator emulator;
WiFiManager wifiManager;
MqttSettings mqttSettings;
WifiSettings wifiSettings;
WiFiClient client;
HADevice device;
HAMqtt mqtt(client, device);

// MQTT settings for WiFiManager web portal
WiFiManagerParameter custom_mqtt_client_id("client_id", "MQTT Client ID", mqttSettings.clientId, sizeof(mqttSettings.clientId));
WiFiManagerParameter custom_mqtt_server("server", "MQTT Server", mqttSettings.hostName, sizeof(mqttSettings.hostName));
WiFiManagerParameter custom_mqtt_port("port", "MQTT Port", mqttSettings.port, sizeof(mqttSettings.port));
WiFiManagerParameter custom_mqtt_user("user", "MQTT User", mqttSettings.user, sizeof(mqttSettings.user));
WiFiManagerParameter custom_mqtt_pass("pass", "MQTT Password", mqttSettings.password, sizeof(mqttSettings.password));
// Firmware version info for web portal
WiFiManagerParameter fwInfo("<p>Firmware Version: v" VERSION "<br>Build Time: " __TIMESTAMP__ "</p>");

// note: HAMqtt must be initialized before any sensors
HASensor status("alarmcontrol_status");
HASensor source("alarmcontrol_source");
HASensor message("alarmcontrol_message");

HAButton updateCmd("alarmcontrol_update");
HAButton armCmd("alarmcontrol_arm");
HAButton disarmCmd("alarmcontrol_disarm");

// HA button command callback
static void buttonCommand_cb(HAButton* sender) {
    if (sender == &updateCmd) {
        emulator.sendCommand(Emulator::Command::GetStatus);
    } else if (sender == &armCmd) {
        emulator.sendCommand(Emulator::Command::ArmAway);
    } else if (sender == &disarmCmd) {
        emulator.sendCommand(Emulator::Command::Disarm);
    }
}

// Webportal callback notifying about the need to save config
void saveConfig_cb () {
    Serial << beginl << blue << "Saving MQTT credentials to config file" << DI::endl;
    strcpy(mqttSettings.clientId, custom_mqtt_client_id.getValue());
    strcpy(mqttSettings.hostName, custom_mqtt_server.getValue());
    strcpy(mqttSettings.port, custom_mqtt_port.getValue());
    strcpy(mqttSettings.user, custom_mqtt_user.getValue());
    strcpy(mqttSettings.password, custom_mqtt_pass.getValue());

    JsonDocument json;
    json[mqttSettings.clientId_id] = mqttSettings.clientId;
    json[mqttSettings.hostName_id] = mqttSettings.hostName;
    json[mqttSettings.port_id] = mqttSettings.port;
    json[mqttSettings.user_id] = mqttSettings.user;
    json[mqttSettings.password_id] = mqttSettings.password;
    File configFile = SPIFFS.open(configFilePath, "w");
    if (!configFile) {
        Serial << beginl << red << "Failed to save config file " << configFilePath << DI::endl;
    } else {
        Serial << beginl << green << "Config file " << configFilePath << " saved" << DI::endl;
        serializeJson(json, Serial);
        serializeJson(json, configFile);
        configFile.close();
    }
}

void initializeWifiManager() {

    // Read MQTT settings from config file
    if (SPIFFS.begin(true)) {
        if (!SPIFFS.exists(configFilePath)) {
            Serial << beginl << yellow << "Creating empty config file " << configFilePath << DI::endl;
            SPIFFS.open(configFilePath, "w").close();
        } else {
            File configFile = SPIFFS.open(configFilePath, "r");
            if (configFile) {
                size_t size = configFile.size();
                std::unique_ptr<char[]> buf(new char[size + 1]);
                configFile.readBytes(buf.get(), size);
                JsonDocument json;
                auto error = deserializeJson(json, buf.get());
                serializeJson(json, Serial);
                if (error) {
                    Serial << beginl << red << "Failed to read config file " << configFilePath << DI::endl;
                } else {
                    strcpy(mqttSettings.clientId, json[mqttSettings.clientId_id]);
                    strcpy(mqttSettings.hostName, json[mqttSettings.hostName_id]);
                    strcpy(mqttSettings.port, json[mqttSettings.port_id]);
                    strcpy(mqttSettings.user, json[mqttSettings.user_id]);
                    strcpy(mqttSettings.password, json[mqttSettings.password_id]);
                }
                Serial << beginl << green << "Config file " << configFilePath << " loaded" << DI::endl;
                configFile.close();
            }
        }
    } else {
        Serial << beginl << red << "Failed to mount file system" << DI::endl;
    }

    wifiManager.addParameter(&custom_mqtt_client_id);
    wifiManager.addParameter(&custom_mqtt_server);
    wifiManager.addParameter(&custom_mqtt_port);
    wifiManager.addParameter(&custom_mqtt_user);
    wifiManager.addParameter(&custom_mqtt_pass);
    wifiManager.addParameter(&fwInfo);

    // note: callbacks are only called when changing wifi settings,
    // *not* on custom param changes and clicking "save" button
    // -> enter <ip>/wifisave in the web browser to trigger the callback
    wifiManager.setPreSaveConfigCallback(saveConfig_cb);

    wifiManager.setBreakAfterConfig(false);
    wifiManager.setConfigPortalTimeout(60);
    wifiManager.setConnectTimeout(60);
    wifiManager.setConfigPortalBlocking(false);
    wifiManager.setDarkMode(true);
    std::vector<const char*> menuItems = {"wifi", "info", "param", "sep", "update"};
    wifiManager.setMenu(menuItems);
    if (!wifiManager.autoConnect(APName)) {
        Serial << beginl << yellow << "started config portal in AP mode, IP: " << WiFi.softAPIP() << DI::endl;
    } else  {
        Serial << beginl << green << "started config portal with WiFi connection, IP: " << WiFi.localIP() << DI::endl;
        wifiManager.startWebPortal();
    }
}

void setup() {
    Serial.begin(debugBaudRate);
    Serial << magenta << F("Sim900Emulator v");
    Serial << magenta << VERSION;
    Serial << magenta << F(" (") <<  __TIMESTAMP__  << F(")") << DI::endl;

    emulator.init();
    emulator.led.setState(LEDControl::LedState::LED_FLASH_SLOW);

    Serial << beginl << green << "Connecting to WiFi..." << DI::endl;
    WiFi.mode(WIFI_MODE_STA);
    WiFi.setSleep(false);
    initializeWifiManager();

    byte mac[6];
    WiFi.macAddress(mac);
    device.setUniqueId(mac, sizeof(mac));
    device.setName(mqttSettings.clientId);
    device.setSoftwareVersion(VERSION);
    device.setManufacturer("NotYourHome");
    device.setAvailability(true);
    device.enableSharedAvailability();
    device.enableLastWill();
    
}

void loop() {
    static bool wifiConnected = false;
    static bool mqttStartup = false;
    static bool mqttConnected = false;
    if (WiFi.status() == WL_CONNECTED) {
        if (!wifiConnected) {
            wifiConnected = true;
            Serial << beginl << green << "WiFi connected" << DI::endl;
            Serial << beginl << "SSID: " << WiFi.SSID() << DI::endl;
            Serial << beginl << "IP address: " << WiFi.localIP() << DI::endl; 
            emulator.led.setState(LEDControl::LedState::LED_FLASH_FAST);
            // start MQTT connection
            if (mqtt.begin(mqttSettings.hostName, atoi(mqttSettings.port), mqttSettings.user, mqttSettings.password))
                Serial << beginl << green << "MQTT connecting... " << DI::endl;
        }
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
    } else {
        if (wifiConnected) {
            wifiConnected = false;
            mqttConnected = false;
            Serial << beginl << red << "Lost WiFi connection" << DI::endl;
            emulator.led.setState(LEDControl::LedState::LED_FLASH_SLOW);
        }
    }
    emulator.loop();
    wifiManager.process();
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

    updateCmd.onCommand(buttonCommand_cb);
    armCmd.onCommand(buttonCommand_cb);
    disarmCmd.onCommand(buttonCommand_cb);

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
