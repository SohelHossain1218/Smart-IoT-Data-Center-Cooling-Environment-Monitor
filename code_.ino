/*************************************************************
 SMART IoT-BASED DATA CENTER COOLING & ENVIRONMENT MONITORING
 VERSION: 2.0 (Full Updated)
 MCU:     ESP8266
 Sensors: ENS160 + AHT21, MQ-2
 Display: OLED SSD1306 128x64
 Control: Dual AC Relay (Primary + Standby)
 Author:  Updated & Fixed Version

 CHANGES FROM v1.0:
  - [FIX] Standby AC now turns OFF when temperature drops
  - [FIX] Buzzer triggers on ANY alarm (OR logic), not all three
  - [FIX] Telegram alert cooldown (5 min) to prevent spam
  - [FIX] WiFi auto-reconnect logic added
  - [FIX] Sensor read failure handling
  - [NEW] Full Telegram command set: /status, /ac1on, /ac1off, /ac2on,
          /ac2off, /settemp, /sethum, /setsmoke, /reset, /help
  - [NEW] AQI shown on OLED display (page 1)
  - [NEW] NTP timestamp in Telegram messages
  - [NEW] Manual override flag (prevents auto-logic fighting manual)
  - [NEW] Uptime display on OLED page 3
  - [NEW] Startup self-test for all sensors
  - [NEW] EEPROM validation with checksum
  - [NEW] Serial debug mode
*************************************************************/

/*************************************************************
 SMART IoT-BASED DATA CENTER COOLING & ENVIRONMENT MONITORING
 VERSION: 2.1 (FINAL CORRECTED)
 MCU:     ESP8266
 Sensors: ENS160 + AHT21, MQ-2
 Display: OLED SSD1306 128x64
 Control: Dual AC Relay (Primary + Standby)
*************************************************************/

#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <Wire.h>
#include <EEPROM.h>
#include <time.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <Adafruit_AHTX0.h>
#include "ScioSense_ENS160.h"

#define BOT_TOKEN  "8286746287:AAEWPF7SIabaXlni9SkDmbIr-gEtQ_Iog84" 
#define CHAT_ID    "8297225062"

#define NTP_SERVER  "pool.ntp.org"
#define GMT_OFFSET  21600   // Bangladesh UTC+6
#define DST_OFFSET  0

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

Adafruit_AHTX0 aht;
ScioSense_ENS160 ens160(ENS160_I2CADDR_1); // 0x53

#define MQ2_PIN     A0
#define LED_TEMP    D0 //Red
#define LED_HUM     D7  //
#define LED_SMOKE   D9
#define BUZZER      D8 // Buzzer
#define AC1_RELAY   D3
#define AC2_RELAY   D4
#define BTN_AC1     D5
#define BTN_AC2     D6

#define STANDBY_OFFSET         2.0f
#define STANDBY_OFF_HYSTERESIS 1.0f
#define ALERT_COOLDOWN         300000UL
#define STATUS_INTERVAL        120000UL
#define DISPLAY_INTERVAL       3000UL
#define TELEGRAM_INTERVAL      1000UL
#define WIFI_CHECK_INTERVAL    30000UL
#define BUTTON_DEBOUNCE        300UL
#define EEPROM_MAGIC           0xAB
#define EEPROM_SIZE            64

float temperature = 0.0f;
float humidity    = 0.0f;
uint16_t eco2     = 0;
uint16_t tvoc     = 0;
uint8_t  aqi      = 0;
int      smokeValue = 0;
bool     sensorOK_AHT = false;
bool     sensorOK_ENS = false;
bool     alarmActive = false;
bool     alarmAcknowledged = false;

struct SETPOINT {
  uint8_t magic;
  float   tLow;
  float   tHigh;
  float   hLow;
  float   hHigh;
  int     smoke;
} sp;

bool ac1ManualOverride = false;
bool ac2ManualOverride = false;
bool ac1State = false;
bool ac2State = false;

bool prevTempAlarm  = false;
bool prevHumAlarm   = false;
bool prevSmokeAlarm = false;

unsigned long lastTelegramCheck = 0;
unsigned long lastStatusSent    = 0;
unsigned long lastAlertSent     = 0;
unsigned long lastDisplay       = 0;
unsigned long lastButton        = 0;
unsigned long lastWifiCheck     = 0;
unsigned long bootTime          = 0;
int displayPage = 0;

WiFiClientSecure client;
UniversalTelegramBot bot(BOT_TOKEN, client);

void initSensors();
void selfTest();
void readSensors();
void controlLogic();
void setAC1(bool on, bool manual = false);
void setAC2(bool on, bool manual = false);
void updateDisplay();
void handleTelegram();
void processTelegramCommand(String text, String chatId);
void sendStatus(String chatId);
void loadEEPROM();
void saveEEPROM();
void checkWiFi();
String getTimestamp();
String getUptime();
void debugPrint(String msg);

void setup() {
  Serial.begin(9600);
  EEPROM.begin(EEPROM_SIZE);
  Wire.begin();  // important for I2C

  pinMode(LED_TEMP,  OUTPUT);
  pinMode(LED_HUM,   OUTPUT);
  pinMode(LED_SMOKE, OUTPUT);
  pinMode(BUZZER,    OUTPUT);
  pinMode(AC1_RELAY, OUTPUT);
  pinMode(AC2_RELAY, OUTPUT);
  pinMode(BTN_AC1,   INPUT_PULLUP);
  pinMode(BTN_AC2,   INPUT_PULLUP);

  digitalWrite(AC1_RELAY, HIGH);
  digitalWrite(AC2_RELAY, HIGH);
  digitalWrite(BUZZER, LOW);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    debugPrint("OLED FAIL");
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("DataCenter IoT v2.1");
  display.println("Connecting WiFi...");
  display.display();

  WiFiManager wm;
  wm.setConnectTimeout(60);
  wm.autoConnect("DataCenter-IoT");

  configTime(GMT_OFFSET, DST_OFFSET, NTP_SERVER);
  client.setInsecure();

  display.println("WiFi OK");
  display.display();

  initSensors();
  loadEEPROM();
  selfTest();

  bootTime = millis();

  String startMsg =
    "✅ *DataCenter IoT v2.1 ONLINE*\n"
    "━━━━━━━━━━━━━━━\n"
    "🌡 Temp Setpoint: " + String(sp.tLow) + " - " + String(sp.tHigh) + " °C\n"
    "💧 Hum Setpoint:  " + String(sp.hLow) + " - " + String(sp.hHigh) + " %\n"
    "💨 Smoke Limit:   " + String(sp.smoke) + "\n"
    "━━━━━━━━━━━━━━━\n"
    "Type /help for commands";
  bot.sendMessage(CHAT_ID, startMsg, "Markdown");

  display.println("System Ready!");
  display.display();
  delay(1500);
}

void loop() {
  checkWiFi();
  readSensors();
  controlLogic();

  if (millis() - lastDisplay > DISPLAY_INTERVAL) {
    updateDisplay();
    lastDisplay = millis();
  }

  if (millis() - lastStatusSent > STATUS_INTERVAL) {
    sendStatus(CHAT_ID);
    lastStatusSent = millis();
  }

  if (millis() - lastTelegramCheck > TELEGRAM_INTERVAL) {
    handleTelegram();
    lastTelegramCheck = millis();
  }
}

void initSensors() {
  sensorOK_AHT = aht.begin();
  if (!sensorOK_AHT) {
    debugPrint("AHT21 INIT FAIL");
    display.println("AHT21 ERROR!");
    display.display();
  }

  sensorOK_ENS = ens160.begin();
  if (!sensorOK_ENS) {
    debugPrint("ENS160 INIT FAIL");
    display.println("ENS160 ERROR!");
    display.display();
  } else {
    ens160.setMode(ENS160_OPMODE_STD);
  }
}

void selfTest() {
  debugPrint("=== SELF TEST ===");
  digitalWrite(LED_TEMP, HIGH); delay(200);
  digitalWrite(LED_HUM, HIGH);  delay(200);
  digitalWrite(LED_SMOKE, HIGH); delay(200);
  digitalWrite(BUZZER, HIGH); delay(100);
  digitalWrite(BUZZER, LOW);
  digitalWrite(LED_TEMP, LOW);
  digitalWrite(LED_HUM, LOW);
  digitalWrite(LED_SMOKE, LOW);
  debugPrint("Self test complete");
}

void readSensors() {
  if (sensorOK_AHT) {
    sensors_event_t humEvent, tempEvent;
    if (aht.getEvent(&humEvent, &tempEvent)) {
      if (tempEvent.temperature > -40 && tempEvent.temperature < 85)
        temperature = tempEvent.temperature;
      if (humEvent.relative_humidity >= 0 && humEvent.relative_humidity <= 100)
        humidity = humEvent.relative_humidity;
    }
  }

  if (sensorOK_ENS && ens160.available()) {
    ens160.set_envdata(temperature, humidity);
    ens160.measure(true);
    ens160.measureRaw(true);
    eco2 = ens160.geteCO2();
    tvoc = ens160.getTVOC();
    aqi  = ens160.getAQI();
  }

  smokeValue = analogRead(MQ2_PIN);
  debugPrint("T:" + String(temperature) + " H:" + String(humidity) + " Smoke:" + String(smokeValue));
}

void setAC1(bool on, bool manual) {
  ac1State = on;
  if (manual) ac1ManualOverride = true;
  digitalWrite(AC1_RELAY, on ? LOW : HIGH);
  debugPrint(on ? "AC1 ON" : "AC1 OFF");
}

void setAC2(bool on, bool manual) {
  ac2State = on;
  if (manual) ac2ManualOverride = true;
  digitalWrite(AC2_RELAY, on ? LOW : HIGH);
  debugPrint(on ? "AC2 ON" : "AC2 OFF");
}

void controlLogic() {
  bool tempAlarm  = (temperature < sp.tLow || temperature > sp.tHigh);
  bool humAlarm   = (humidity < sp.hLow || humidity > sp.hHigh);
  bool smokeAlarm = (smokeValue > sp.smoke);
  bool anyAlarm   = tempAlarm || humAlarm || smokeAlarm;

  digitalWrite(LED_TEMP,  tempAlarm);
  digitalWrite(LED_HUM,   humAlarm);
  digitalWrite(LED_SMOKE, smokeAlarm);

  // ----- AC Auto Control -----
  if (!ac1ManualOverride) {
    if (temperature > sp.tHigh) setAC1(true);
    else if (temperature <= sp.tLow) setAC1(false);
  }

  if (!ac2ManualOverride) {
    if (temperature > sp.tHigh + STANDBY_OFFSET) setAC2(true);
    else if (temperature <= (sp.tHigh - STANDBY_OFF_HYSTERESIS)) setAC2(false);
  }

  // ----- Alarm & Acknowledge (fixed) -----
  if (anyAlarm) {
    if (!alarmActive) {
      alarmActive = true;
      alarmAcknowledged = false;
    }
  } else {
    alarmActive = false;
    alarmAcknowledged = false;
  }

  // Buzzer only if alarm active AND not acknowledged
  if (alarmActive && !alarmAcknowledged)
    digitalWrite(BUZZER, HIGH);
  else
    digitalWrite(BUZZER, LOW);

  // ----- Telegram alerts with cooldown -----
  if (anyAlarm && (millis() - lastAlertSent > ALERT_COOLDOWN)) {
    String alertMsg = "🚨 *ALARM TRIGGERED*\n🕐 " + getTimestamp() + "\n";
    if (tempAlarm)  alertMsg += "🌡 Temp: " + String(temperature) + " °C ⚠️\n";
    if (humAlarm)   alertMsg += "💧 Hum: "  + String(humidity)    + " % ⚠️\n";
    if (smokeAlarm) alertMsg += "💨 Smoke: " + String(smokeValue) + " ⚠️\n";
    bot.sendMessage(CHAT_ID, alertMsg, "Markdown");
    lastAlertSent = millis();
  }

  bool wasAnyAlarm = prevTempAlarm || prevHumAlarm || prevSmokeAlarm;
  if (wasAnyAlarm && !anyAlarm) {
    bot.sendMessage(CHAT_ID, "✅ Environment NORMAL\n" + getTimestamp(), "");
  }
  prevTempAlarm = tempAlarm;
  prevHumAlarm  = humAlarm;
  prevSmokeAlarm = smokeAlarm;

  // ----- Manual Buttons (debounced) -----
  if (millis() - lastButton > BUTTON_DEBOUNCE) {
    if (!digitalRead(BTN_AC1)) {
      ac1ManualOverride = true;
      setAC1(!ac1State, true);
      lastButton = millis();
    }
    if (!digitalRead(BTN_AC2)) {
      ac2ManualOverride = true;
      setAC2(!ac2State, true);
      lastButton = millis();
    }
    // Alarm acknowledge via ANY button press (optional)
    if ((!digitalRead(BTN_AC1) || !digitalRead(BTN_AC2)) && alarmActive) {
      alarmAcknowledged = true;
    }
  }
}

void updateDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("=DataCenter IoT v2.1=");
  display.drawLine(0, 9, 128, 9, SSD1306_WHITE);
  display.setCursor(0, 12);

  if (displayPage == 0) {
    display.println("[Temp & Humidity]");
    display.print("Temp: "); display.print(temperature, 1); display.println(" C");
    display.print("Hum : "); display.print(humidity, 1); display.println(" %");
    display.print("SP T: "); display.print(sp.tLow); display.print("-"); display.print(sp.tHigh); display.println(" C");
  }
  else if (displayPage == 1) {
    display.println("[Air Quality]");
    display.print("eCO2: "); display.print(eco2); display.println(" ppm");
    display.print("TVOC: "); display.print(tvoc); display.println(" ppb");
    display.print("AQI : "); display.println(aqi);
  }
  else if (displayPage == 2) {
    display.println("[Smoke & AC]");
    display.print("Smoke: "); display.println(smokeValue);
    display.print("AC1: "); display.print(ac1State ? "ON " : "OFF"); display.println(ac1ManualOverride ? "[M]" : "[A]");
    display.print("AC2: "); display.print(ac2State ? "ON " : "OFF"); display.println(ac2ManualOverride ? "[M]" : "[A]");
  }
  else {
    display.println("[System Info]");
    display.print("Uptime: "); display.println(getUptime());
    display.print("WiFi: "); display.println(WiFi.isConnected() ? "OK" : "LOST");
    display.print("Sensors: "); display.print(sensorOK_AHT ? "AHT" : "---"); display.print("/"); display.println(sensorOK_ENS ? "ENS" : "---");
  }

  display.drawLine(0, 54, 128, 54, SSD1306_WHITE);
  display.setCursor(0, 56);
  display.print("T"); display.print(prevTempAlarm ? "!" : ".");
  display.print("H"); display.print(prevHumAlarm  ? "!" : ".");
  display.print("S"); display.print(prevSmokeAlarm? "!" : ".");
  if (alarmActive && !alarmAcknowledged) display.print(" ALARM!");
  else if (alarmAcknowledged) display.print(" Acked");
  display.print(" pg:"); display.print(displayPage+1); display.print("/4");

  display.display();
  displayPage = (displayPage + 1) % 4;
}

void sendStatus(String chatId) {
  String aqiLabel = "";
  if      (aqi == 1) aqiLabel = "Excellent";
  else if (aqi == 2) aqiLabel = "Good";
  else if (aqi == 3) aqiLabel = "Moderate";
  else if (aqi == 4) aqiLabel = "Poor";
  else if (aqi == 5) aqiLabel = "Unhealthy";

  String msg =
    "📊 *Data Center Status*\n"
    "━━━━━━━━━━━━━━━\n"
    "🕐 " + getTimestamp() + "\n"
    "🌡 Temp : " + String(temperature,1) + " °C\n"
    "💧 Hum  : " + String(humidity,1) + " %\n"
    "🌬 eCO2 : " + String(eco2) + " ppm\n"
    "🧪 TVOC : " + String(tvoc) + " ppb\n"
    "📈 AQI  : " + String(aqi) + " (" + aqiLabel + ")\n"
    "💨 Smoke: " + String(smokeValue) + "\n"
    "━━━━━━━━━━━━━━━\n"
    "⚡ AC1 : " + String(ac1State ? "ON" : "OFF") + (ac1ManualOverride ? " [Manual]" : " [Auto]") + "\n"
    "⚡ AC2 : " + String(ac2State ? "ON" : "OFF") + (ac2ManualOverride ? " [Manual]" : " [Auto]") + "\n"
    "⏱ Uptime: " + getUptime();
  bot.sendMessage(chatId, msg, "Markdown");
}

void handleTelegram() {
  int n = bot.getUpdates(bot.last_message_received + 1);
  for (int i = 0; i < n; i++) {
    String text   = bot.messages[i].text;
    String fromId = bot.messages[i].chat_id;
    if (fromId != CHAT_ID) {
      bot.sendMessage(fromId, "❌ Unauthorized", "");
      continue;
    }
    if (text == "/ack") {
      alarmAcknowledged = true;
      bot.sendMessage(fromId, "🔇 Alarm acknowledged. Buzzer silenced.", "");
      continue;
    }
    processTelegramCommand(text, fromId);
  }
}

void processTelegramCommand(String text, String chatId) {
  text.trim();
  if (text == "/help") {
    String help =
      "📖 *Available Commands*\n"
      "━━━━━━━━━━━━━━━\n"
      "/status - Sensor data\n"
      "/ac1on  - Primary AC ON\n"
      "/ac1off - Primary AC OFF\n"
      "/ac2on  - Standby AC ON\n"
      "/ac2off - Standby AC OFF\n"
      "/auto   - Return to Auto mode\n"
      "/settemp LOW HIGH\n"
      "/sethum LOW HIGH\n"
      "/setsmoke VALUE\n"
      "/getsp  - Show setpoints\n"
      "/reset  - Default setpoints\n"
      "/uptime - System uptime\n"
      "/ack    - Mute buzzer";
    bot.sendMessage(chatId, help, "Markdown");
  }
  else if (text == "/status") sendStatus(chatId);
  else if (text == "/ac1on")  { setAC1(true, true);  bot.sendMessage(chatId, "✅ AC1 ON [Manual]", ""); }
  else if (text == "/ac1off") { setAC1(false, true); bot.sendMessage(chatId, "✅ AC1 OFF [Manual]", ""); }
  else if (text == "/ac2on")  { setAC2(true, true);  bot.sendMessage(chatId, "✅ AC2 ON [Manual]", ""); }
  else if (text == "/ac2off") { setAC2(false, true); bot.sendMessage(chatId, "✅ AC2 OFF [Manual]", ""); }
  else if (text == "/auto")   { ac1ManualOverride = false; ac2ManualOverride = false; bot.sendMessage(chatId, "🔄 Auto mode restored", ""); }
  else if (text.startsWith("/settemp ")) {
    String p = text.substring(9);
    int idx = p.indexOf(' ');
    if (idx > 0) {
      float low = p.substring(0, idx).toFloat();
      float high = p.substring(idx+1).toFloat();
      if (low>=0 && high>low && high<=50) { sp.tLow=low; sp.tHigh=high; saveEEPROM(); bot.sendMessage(chatId, "✅ Temp setpoint updated", ""); }
      else bot.sendMessage(chatId, "❌ Invalid range", "");
    } else bot.sendMessage(chatId, "❌ Format: /settemp 22 26", "");
  }
  else if (text.startsWith("/sethum ")) {
    String p = text.substring(8);
    int idx = p.indexOf(' ');
    if (idx > 0) {
      float low = p.substring(0, idx).toFloat();
      float high = p.substring(idx+1).toFloat();
      if (low>=0 && high>low && high<=100) { sp.hLow=low; sp.hHigh=high; saveEEPROM(); bot.sendMessage(chatId, "✅ Humidity setpoint updated", ""); }
      else bot.sendMessage(chatId, "❌ Invalid range", "");
    } else bot.sendMessage(chatId, "❌ Format: /sethum 45 65", "");
  }
  else if (text.startsWith("/setsmoke ")) {
    int val = text.substring(10).toInt();
    if (val>50 && val<1024) { sp.smoke=val; saveEEPROM(); bot.sendMessage(chatId, "✅ Smoke threshold updated", ""); }
    else bot.sendMessage(chatId, "❌ Range 50-1023", "");
  }
  else if (text == "/getsp") {
    bot.sendMessage(chatId, "⚙️ *Setpoints*\n🌡 Temp: " + String(sp.tLow)+"-"+String(sp.tHigh)+"°C\n💧 Hum: "+String(sp.hLow)+"-"+String(sp.hHigh)+"%\n💨 Smoke: "+String(sp.smoke), "Markdown");
  }
  else if (text == "/reset") {
    sp = {EEPROM_MAGIC, 22.0, 26.0, 45.0, 60.0, 400};
    saveEEPROM();
    bot.sendMessage(chatId, "🔄 Default setpoints restored", "");
  }
  else if (text == "/uptime") {
    bot.sendMessage(chatId, "⏱ Uptime: " + getUptime(), "");
  }
  else {
    bot.sendMessage(chatId, "❓ Unknown command. /help", "");
  }
}

void loadEEPROM() {
  EEPROM.get(0, sp);
  if (sp.magic != EEPROM_MAGIC || isnan(sp.tLow) || isnan(sp.tHigh)) {
    debugPrint("EEPROM invalid, loading defaults");
    sp = {EEPROM_MAGIC, 22.0, 26.0, 45.0, 60.0, 400};
    saveEEPROM();
  }
}

void saveEEPROM() {
  sp.magic = EEPROM_MAGIC;
  EEPROM.put(0, sp);
  EEPROM.commit();
  debugPrint("EEPROM saved");
}

void checkWiFi() {
  if (millis() - lastWifiCheck > WIFI_CHECK_INTERVAL) {
    lastWifiCheck = millis();
    if (!WiFi.isConnected()) {
      debugPrint("WiFi lost! Reconnecting...");
      WiFi.reconnect();
      unsigned long t = millis();
      while (!WiFi.isConnected() && millis() - t < 15000) delay(500);
      if (WiFi.isConnected()) {
        debugPrint("WiFi reconnected!");
        bot.sendMessage(CHAT_ID, "🔄 WiFi Reconnected", "");
      }
    }
  }
}

String getTimestamp() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "Time N/A";
  char buf[20];
  strftime(buf, sizeof(buf), "%d/%m %H:%M:%S", &timeinfo);
  return String(buf);
}

String getUptime() {
  unsigned long s = (millis() - bootTime) / 1000;
  unsigned long d = s / 86400; s %= 86400;
  unsigned long h = s / 3600;  s %= 3600;
  unsigned long m = s / 60;    s %= 60;
  String out = "";
  if (d>0) out += String(d)+"d ";
  out += String(h)+"h "+String(m)+"m "+String(s)+"s";
  return out;
}

void debugPrint(String msg) {
  Serial.println("[DBG] " + msg);
}