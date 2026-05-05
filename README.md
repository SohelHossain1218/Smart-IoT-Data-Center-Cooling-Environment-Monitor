# Smart IoT Data Center Cooling & Environment Monitor

## Overview
This project is an **ESP8266-based** environmental monitoring and cooling control system for a small data center or server room. It reads temperature, humidity, air quality (eCO₂, TVOC, AQI) and smoke level, then automatically controls two AC units (Primary + Standby) to maintain optimal conditions. Alerts are sent via **Telegram**, and you can manually override ACs or change setpoints remotely.

## Features
- 🌡️ **Sensors:** AHT21 (temp/hum), ENS160 (eCO₂/TVOC/AQI), MQ-2 (smoke)
- 🖥️ **Display:** OLED SSD1306 128x64 – 4 information pages
- ❄️ **Dual AC Control:** Primary AC + Standby AC with failsafe offset
- 🔔 **Alarm System:** 3 status LEDs + buzzer (mutable via button or `/ack` command)
- 📱 **Telegram Bot** – full remote control and status monitoring
- 💾 **Non-volatile memory:** Setpoints saved in EEPROM with validation
- ⏱️ **NTP timestamp** on all alerts and status messages
- 🔁 **Auto WiFi reconnect** + WiFiManager for easy setup
- 🛠️ **Manual override buttons** for both AC units

## Hardware Required
| Component | Quantity | Typical Pins (ESP8266) |
|-----------|----------|------------------------|
| ESP8266 (NodeMCU / Wemos D1) | 1 | - |
| AHT21 (temperature & humidity) | 1 | D1 (SCL), D2 (SDA) |
| ENS160 (air quality) | 1 | D1 (SCL), D2 (SDA) |
| MQ-2 (smoke/gas sensor) | 1 | A0 |
| OLED SSD1306 (128x64 I2C) | 1 | D1 (SCL), D2 (SDA) |
| 5V Relay module (active low) | 2 | D3 (AC1), D4 (AC2) |
| LEDs (3 colors) | 3 | D0, D7, D9 |
| Passive buzzer | 1 | D8 |
| Push buttons | 2 | D5, D6 |
| 5V power supply | 1 | - |

> **Note:** D9 is the RX pin (GPIO3) – safe to use as digital output for an LED.

## Pin Mapping
```
LED_TEMP      → D0 (GPIO16)
LED_HUM       → D7 (GPIO13)
LED_SMOKE     → D9 (GPIO3)
BUZZER        → D8 (GPIO15)
BTN_AC1       → D5 (GPIO14)
BTN_AC2       → D6 (GPIO12)
AC1_RELAY     → D3 (GPIO0)  // Active LOW: LOW=ON, HIGH=OFF
AC2_RELAY     → D4 (GPIO2)  // Active LOW
I2C SCL       → D1 (GPIO5)
I2C SDA       → D2 (GPIO4)
MQ-2 analog   → A0
```

## Telegram Commands
After you create a bot with `@BotFather`, use these commands:

| Command | Description |
|---------|-------------|
| `/status` | Show all sensor data (temp, humidity, eCO₂, TVOC, AQI, smoke, AC states) |
| `/ac1on`  | Turn ON Primary AC (manual override) |
| `/ac1off` | Turn OFF Primary AC |
| `/ac2on`  | Turn ON Standby AC |
| `/ac2off` | Turn OFF Standby AC |
| `/auto`   | Return to automatic control (cancel manual override) |
| `/settemp <LOW> <HIGH>` | Set temperature range, e.g. `/settemp 22 26` |
| `/sethum <LOW> <HIGH>`  | Set humidity range, e.g. `/sethum 45 65` |
| `/setsmoke <VALUE>`     | Set smoke threshold, e.g. `/setsmoke 400` |
| `/getsp`   | Show current setpoints |
| `/reset`   | Restore defaults (22-26°C, 45-60%, smoke=400) |
| `/uptime`  | Show system uptime |
| `/ack`     | Mute buzzer (acknowledge alarm) |
| `/help`    | Display help message |

## How It Works
- **Normal Auto Mode:**  
  Primary AC turns ON when temperature exceeds `tHigh` and OFF when temperature drops below `tLow`.  
  Standby AC turns ON when `temp > tHigh + 2°C` and OFF when `temp < tHigh - 1°C`.

- **Alarms:**  
  Any parameter out of its setpoint range triggers:  
  - Corresponding LED lights up.  
  - Buzzer sounds (can be silenced by pressing any button or sending `/ack`).  
  - Telegram alert is sent (cooldown 5 minutes).  
  - After all values return to normal, a `"Environment NORMAL"` message is sent.

- **Manual Override:**  
  Press physical buttons or send `/ac1on`, `/ac2on`, etc. The system disables auto-control for that AC until `/auto` is used.

- **Display Pages (rotate every 3 seconds):**  
  1. Temperature & Humidity + setpoints  
  2. Air Quality (eCO₂, TVOC, AQI)  
  3. Smoke value + AC states (with manual flag)  
  4. System info (uptime, WiFi status, sensor health)

## Setup & Installation
1. **Install required libraries** (Arduino IDE):  
   - WiFiManager  
   - Adafruit GFX + SSD1306  
   - Adafruit AHTX0  
   - ScioSense ENS160  
   - UniversalTelegramBot  
   - ArduinoJson (version 6)

2. **Create Telegram Bot** and get token + your chat ID.

3. **Update the code** with your bot token and chat ID:
   ```cpp
   #define BOT_TOKEN  "your_bot_token"
   #define CHAT_ID    "your_chat_id"
   ```

4. **Upload the code** to ESP8266.

5. **First boot:** Connect to the open AP `DataCenter-IoT` and enter your WiFi credentials.

6. **Test:** Send `/status` to your bot – you should receive sensor readings.

## Credits & License
- Author: [Your Name]
- License: MIT  
- Feel free to modify and improve.
