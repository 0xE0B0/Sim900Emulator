# Sim 900 Emulator
This is an ESP32 project to interface a Blaupunkt SA2700 Alarmsystem

Sim900Emulator is an ESP32-based emulator that simulates a SIM900-like alarm system modem (e.g. Blaupunkt SA2700 or SA2500) and publishes status/messages to MQTT for Home Assistant integration.

This repository contains firmware that runs on an ESP32, emulates modem responses over a hardware UART, and exposes sensors via MQTT discovery for easy use with Home Assistant.

## Features

- Target: ESP32 (uses UART1 for the alarm-system interface)
- MQTT discovery compatible with Home Assistant
- Debug output via USB serial

## Quick start

1. Rename `include/credentials_template.h` to `include/credentials.h` and fill in your Wi‑Fi and MQTT broker credentials (see example below).
2. Optionally set modem UART pins in `include/Sim900.h` (defaults shown below).
3. Build and upload with PlatformIO or VS Code PlatformIO extension.

## Configure Wi‑Fi and MQTT

Copy and edit the template file:

`include/credentials_template.h` (example)

```cpp
#pragma once
// wifi and MQTT credentials for the Sim900Emulator

#define WIFI_SSID           "YOUR_SSID"
#define WIFI_PASSWORD       "YOUR_WIFI_PASSWORD"

#define BROKER_ADDR         IPAddress(192,168,1,100) // set to your broker's IP
#define BROKER_USERNAME     "mqtt_user"
#define BROKER_PASSWORD     "mqtt_password"

```

After editing, rename the file to `include/credentials.h`.
Do not commit `include/credentials.h` to a public repository.

## Modem UART pins and baud rate (ESP32)

The emulator uses a hardware UART to simulate the alarm system modem. Defaults are defined in `include/Sim900.h`:

```cpp
static constexpr int MODEM_TX = 16;        // ESP32 pin used as TX (connect to alarm RX)
static constexpr int MODEM_RX = 17;        // ESP32 pin used as RX (connect to alarm TX)
static constexpr unsigned long MODEM_BAUD = 9600;
```

Wiring:
- Disconnect the Sim900 RX/TX lines from the alarm controller.
- Connect the alarm controller TX to the ESP32 `MODEM_RX` pin.
- Connect the alarm controller RX to the ESP32 `MODEM_TX` pin.

## Serial / Debug

- USB debug serial: `MONITOR_BAUD` is set in `include/Sim900Emulator.h` (default 115200).
- The emulator prints RX/TX and internal state to the monitor for troubleshooting. 
- The debug output includes escape sequences to color the messages.
- `useAnsiColor = true` in `include/DebugInterface.h` injects ANSI color codes at compile time. To remove colors, set it to false and recompile.
If colors don't appear, the serial monitor/terminal likely doesn't support ANSI escapes - use a compatible terminal or disable colors.
- VSCode Terminal supports it, specify `monitor_raw = yes` in `platformio.ini` to enable.

## Build & upload (PlatformIO)

From the project root:

```powershell
# build
platformio run

# build + upload
platformio run --target upload

# monitor
platformio device monitor
```

Or use the PlatformIO tasks in VS Code.

## Home Assistant Integration

- The firmware publishes MQTT discovery payloads so sensors are auto-created in Home Assistant.

- Example discovery topic: `homeassistant/sensor/<device_id>/alarmcontrol_status/config`.

- State topic example: `aha/<device_id>/alarmcontrol_status/stat_t`.

- If a sensor appears but shows no state, check that the discovery JSON's `stat_t` matches the topic you publish to and remove invalid fields (e.g., do not use `unit_of_meas: "string"`).

## Troubleshooting

- Wi‑Fi not connecting: open serial monitor and check logs.

- MQTT connection errors: verify `BROKER_ADDR`, `BROKER_USERNAME`, and `BROKER_PASSWORD`.

- Wrong pins/baud: confirm `MODEM_TX`/`MODEM_RX` and `MODEM_BAUD` match your device.

## License
See `LICENSE` in the project root.

