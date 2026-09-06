/**
 * @file CrimsonKestrel_v1_15_2.ino
 * @brief Professional Solar Telemetry Node with WDT, OTA Rollback, MQTT LWT, and Retained MQTT Debug Sleep Override
 * @target ESP32-C6 (RISC-V, N16 16MB Flash)
 * @author Marshall Wingerson (Senior Embedded Systems Engineer)
 * 
 * Production-Grade Architecture (Phase 1 MVP - Decoupled Metrics & Versioning):
 * 1. Hardware Watchdog Timer (WDT) enabled with a 15-second timeout to recover from Wi-Fi or I2C hangs.
 * 2. OTA Rollback Guard active. If a wireless firmware update fails to boot or connect, the system
 *    will automatically roll back to the last known stable firmware partition.
 * 3. MQTT Last Will & Testament (LWT) configured on the status topic to guarantee offline visibility.
 * 4. Non-blocking network connectivity with a strict connection timeout to prevent battery drain.
 * 5. WS2812 Addressable RGB NeoPixel status reporting for bench and field diagnostics.
 * 6. Structured JSON telemetry payload transmitting temperature, humidity, voltage, and current.
 * 7. Retained MQTT Debug Override:
 *    - Realizes the exact "Single-Shot Boot Pattern" used at runtime (reboots, reconnects, samples).
 *    - Uses an external state pattern via MQTT retained messages.
 *    - If the "cmd/weather_node_01/debug" topic is retained "ON", the deep sleep duration drops
 *      from 5 minutes to 5 seconds, allowing the developer to trace full connection sequences rapidly.
 */

#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <Adafruit_INA219.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>
#include <Adafruit_NeoPixel.h>
#include <esp_task_wdt.h>
#include <esp_ota_ops.h>

// ==========================================
// --- USER CONFIGURATION BLOCK -----------
// ==========================================
#if __has_include("secrets.h")
  #include "secrets.h"         // Pulls actual credentials locally [cite: 235]
#else
  #include "secrets.example.h" // Fallback template so repo compiles out-of-the-box [cite: 235]
#endif

#define MQTT_PORT           1883
#define MQTT_CLIENT_ID      "weather_node_01"

// MQTT Status & Telemetry Topics
#define FIRMWARE_VERSION    "1.15.3"
#define MQTT_TOPIC_WEATHER  "telemetry/weather_node_01/weather"
#define MQTT_TOPIC_SYSTEM   "telemetry/weather_node_01/system"
#define MQTT_TOPIC_STATUS   "telemetry/weather_node_01/status"
#define MQTT_CMD_OTA_TOPIC  "cmd/weather_node_01/ota"
#define MQTT_CMD_DEBUG_TOPIC "cmd/weather_node_01/debug" // Debug sleep override topic
#define MQTT_CMD_SLEEP_TOPIC "cmd/weather_node_01/sleep"
#define MQTT_TOPIC_LOG      "telemetry/weather_node_01/log"

// Operational Durations and Timeouts
#define SLEEP_DURATION_SEC  10       // Default deep sleep duration: 5 minutes (300 seconds)
#define OTA_LISTEN_MS       2000      // Time window to listen for incoming command packets (ms)
#define WDT_TIMEOUT_SEC     15        // Hardware Watchdog Timeout (seconds)
#define NET_TIMEOUT_MS      10000     // Network connection timeout (ms)
#define OTA_WATCHDOG_MS     300000    // 5-minute safety timeout inside active OTA loop

// Pin Configurations
#define DHTPIN              1         // GPIO1: Safe general-purpose pin (no strapping conflicts)
#define DHTTYPE             DHT22
#define ONBOARD_LED         8         // GPIO8: WS2812 addressable RGB LED on Meshnology Board
#define I2C_SDA             19        // GPIO19: SDA for INA219
#define I2C_SCL             20        // GPIO20: SCL for INA219

// ==========================================
// --- GLOBAL OBJECTS & STATE --------------
// ==========================================
DHT dht(DHTPIN, DHTTYPE);
Adafruit_INA219 ina219;
WiFiClient espClient;
PubSubClient mqttClient(espClient);
Adafruit_NeoPixel statusLED(1, ONBOARD_LED, NEO_GRB + NEO_KHZ800);

// Dynamic sleep tracking (resets to 300s default on boot, modified by retained MQTT callback)
uint32_t activeSleepDurationSec = SLEEP_DURATION_SEC; 

// State Flags
volatile bool otaModeActive = false;
volatile bool debugModeActive = false;
unsigned long otaWindowStartTime = 0;

// Struct to store telemetry package
struct TelemetryData {
  float temperature_c;
  float humidity_pct;
  float bus_voltage_v;
  float load_current_ma;
  float shunt_voltage_mv;
  bool sensors_valid;
};

// Forward Declarations
void setLEDColor(uint8_t r, uint8_t g, uint8_t b);
void enterDeepSleep(uint32_t seconds);
bool connectWiFi();
bool connectMQTT();
TelemetryData readSensors();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void setupOTA();
void initWatchdog();
void feedWatchdog();
void logMsg(const String& msg);

// ==========================================
// --- MAIN INITIALIZATION & SETUP ----------
// ==========================================
void setup() {
  // 1. Boot Console & Status LED
  Serial.begin(115200);
  delay(250);
  Serial.println(F("\n========================================="));
  Serial.println(F("Waking up... Initializing CrimsonKestrel Edge Node"));
  Serial.println(F("Firmware Version: 1.15.4 (Robust Callback & Byte Diagnostics)"));
  Serial.println(F("========================================="));

  statusLED.begin();
  statusLED.setBrightness(40); // Set moderate brightness to protect battery
  setLEDColor(150, 75, 0);     // SOLID ORANGE: System booting/initializing

  // 2. Initialize Hardware Watchdog
  initWatchdog();

  // 3. Initialize Sensors
  dht.begin();
  Serial.println(F("[SYSTEM] DHT22 initialized. Waiting 2s for sensor stabilization..."));
  delay(2000); // Required cold-boot stabilization delay for DHT22 to avoid NaN
  feedWatchdog();

  Wire.begin(I2C_SDA, I2C_SCL);
  bool hasCurrentSensor = ina219.begin();
  if (!hasCurrentSensor) {
    Serial.println(F("[ERROR] INA219 current monitor not detected! Checking wiring."));
    setLEDColor(150, 0, 0);    // SOLID RED: Hardware failure state
    delay(1000);
  } else {
    Serial.println(F("[SYSTEM] INA219 current monitor initialized successfully."));
  }
  feedWatchdog();

  // 4. Sample Sensors (Single-shot execution)
  TelemetryData data = readSensors();
  feedWatchdog();

  // 5. Power Down INA219 to save power during sleep
  if (hasCurrentSensor) {
    Wire.beginTransmission(0x40);
    Wire.write(0x00);          // Configuration Register
    Wire.write(0x00);          // MSB (Power Down mode)
    Wire.write(0x00);          // LSB (Power Down mode)
    Wire.endTransmission();
    Serial.println(F("[POWER] INA219 placed in I2C Power-Down Mode (<15uA)."));
  }
  feedWatchdog();

  // 6. Connect & Transmit over Wi-Fi
  if (data.sensors_valid) {
    if (connectWiFi()) {
      feedWatchdog();
      mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
      mqttClient.setCallback(mqttCallback);

      if (connectMQTT()) {
        feedWatchdog();
        // 1. Serialize and publish Weather telemetry
        StaticJsonDocument<192> weatherDoc;
        weatherDoc["client_id"] = MQTT_CLIENT_ID;

        JsonObject env = weatherDoc.createNestedObject("environment");
        env["temperature_c"] = round(data.temperature_c * 10.0) / 10.0;
        env["humidity_pct"] = round(data.humidity_pct * 10.0) / 10.0;

        char weather_buffer[192];
        serializeJson(weatherDoc, weather_buffer);
        logMsg("[MQTT] Publishing weather package: " + String(weather_buffer));

        bool weather_pub_ok = mqttClient.publish(MQTT_TOPIC_WEATHER, weather_buffer);
        feedWatchdog();

        // 2. Serialize and publish System/Power telemetry
        StaticJsonDocument<256> systemDoc;
        systemDoc["client_id"] = MQTT_CLIENT_ID;

        JsonObject sysObj = systemDoc.createNestedObject("system");
        sysObj["version"] = FIRMWARE_VERSION;
        sysObj["uptime_ms"] = millis();

        JsonObject power = systemDoc.createNestedObject("power");
        power["bus_voltage_v"] = round(data.bus_voltage_v * 100.0) / 100.0;
        power["load_current_ma"] = round(data.load_current_ma * 10.0) / 10.0;
        power["shunt_voltage_mv"] = round(data.shunt_voltage_mv * 100.0) / 100.0;

        char system_buffer[256];
        serializeJson(systemDoc, system_buffer);
        logMsg("[MQTT] Publishing system package: " + String(system_buffer));

        bool system_pub_ok = mqttClient.publish(MQTT_TOPIC_SYSTEM, system_buffer);
        feedWatchdog();

        // Visual feedback based on overall success
        if (weather_pub_ok && system_pub_ok) {
          logMsg("[MQTT] Nested CrimsonKestrel telemetry successfully published.");
          setLEDColor(0, 150, 0); // FLASH GREEN: Direct transmit success
          delay(500);
        } else {
          logMsg("[ERROR] One or more MQTT publishes failed.");
          setLEDColor(150, 0, 0); // SOLID RED: MQTT error
          delay(1000);
        }
        feedWatchdog();

        // 7. Active Listening Window for Command Packets
        Serial.printf("[SYSTEM] Listening for commands on '%s' and '%s' for %d ms...\n", 
                      MQTT_CMD_OTA_TOPIC, MQTT_CMD_DEBUG_TOPIC, OTA_LISTEN_MS);
        
        unsigned long listenStart = millis();
        while (millis() - listenStart < OTA_LISTEN_MS) {
          mqttClient.loop();
          delay(10);
          feedWatchdog();
          
          // Visual feedback depending on state
          if (otaModeActive) {
            break;
          } else if (debugModeActive) {
            // Pulse soft white/cyan in active debug mode
            uint8_t dPulse = (millis() % 500 < 250) ? 100 : 20;
            setLEDColor(dPulse, dPulse, dPulse);
          } else {
            setLEDColor(100, 0, 100); // SOLID PURPLE/MAGENTA: Idle listening window
          }
        }
      }
    }
  } else {
    Serial.println(F("[ERROR] Telemetry invalid. Aborting network pipeline to protect battery."));
    setLEDColor(150, 0, 0);       // SOLID RED: Validation failed
    delay(2000);
  }

  // 8. State Verification: Enter active OTA Mode loop or Deep Sleep
  if (otaModeActive) {
    setupOTA();
    otaWindowStartTime = millis();
    Serial.println(F("[OTA] Maintenance Mode engaged. Waiting for wireless firmware upload..."));
    setLEDColor(0, 0, 150);       // SOLID BLUE: Ready for OTA stream
  } else {
    // If the sketch booted successfully, validated metrics, and didn't crash,
    // we mark it as "valid" to prevent the bootloader from triggering an OTA rollback on next reset.
    esp_ota_mark_app_valid_cancel_rollback();
    enterDeepSleep(activeSleepDurationSec);
  }
}

// ==========================================
// --- CORE LOOP (OTA EXCLUSIVE) -----------
// ==========================================
void loop() {
  if (otaModeActive) {
    ArduinoOTA.handle();
    mqttClient.loop(); // Call MQTT loop to listen for dynamic cancel commands ("OFF")
    feedWatchdog();

    // Breathe Blue R&D LED effect during active OTA waiting window
    uint32_t pulse = (sin(millis() / 300.0) * 75) + 75;
    setLEDColor(0, 0, pulse); 

    // Safety watchdog: Prevent indefinite battery drain if upload is aborted
    if (millis() - otaWindowStartTime > OTA_WATCHDOG_MS) {
      logMsg("[OTA] Maintenance window expired without upload. Sleeping...");
      otaModeActive = false;
    }
    delay(5);
  } else {
    // Exit ramp triggered! If otaModeActive was toggled false by MQTT or timeout
    logMsg("[SYSTEM] OTA loop exited. Entering deep sleep...");
    esp_ota_mark_app_valid_cancel_rollback();
    enterDeepSleep(activeSleepDurationSec);
  }
}

// ==========================================
// --- HARDWARE READINGS --------------------
// ==========================================
TelemetryData readSensors() {
  TelemetryData data;
  data.sensors_valid = true;

  // Read DHT22
  data.humidity_pct = dht.readHumidity();
  data.temperature_c = dht.readTemperature();

  // Read INA219
  data.bus_voltage_v = ina219.getBusVoltage_V();
  data.shunt_voltage_mv = ina219.getShuntVoltage_mV();
  data.load_current_ma = ina219.getCurrent_mA();

  // Verify readings against sanity ranges
  if (isnan(data.humidity_pct) || isnan(data.temperature_c) || data.humidity_pct < 0.0 || data.humidity_pct > 100.0) {
    Serial.println(F("[ERROR] DHT22 sensor readings are out-of-bounds or NaN!"));
    data.sensors_valid = false;
  } else {
    Serial.printf("[SENSOR] DHT22 Temp: %.1f C | Humidity: %.1f %%\n", data.temperature_c, data.humidity_pct);
  }

  // Validate Bus Voltage
  if (isnan(data.bus_voltage_v) || data.bus_voltage_v < 0.0 || data.bus_voltage_v > 26.0) {
    Serial.println(F("[ERROR] INA219 bus voltage readings are anomalous or NaN!"));
    data.sensors_valid = false;
  } else {
    Serial.printf("[POWER] Bus Voltage: %.2f V | Load Current: %.1f mA\n", data.bus_voltage_v, data.load_current_ma);
  }

  return data;
}

// ==========================================
// --- NETWORK PIPELINES --------------------
// ==========================================
bool connectWiFi() {
  Serial.print(F("[WIFI] Establishing connection to "));
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < NET_TIMEOUT_MS) {
    // Pulse Yellow LED to show connection attempt
    uint8_t yellowPulse = (millis() % 1000 < 500) ? 150 : 20;
    setLEDColor(yellowPulse, yellowPulse / 2, 0); 
    delay(250);
    Serial.print(F("."));
    feedWatchdog();
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(F("\n[WIFI] Connected."));
    Serial.print(F("[WIFI] Node IP: "));
    Serial.println(WiFi.localIP());
    return true;
  } else {
    Serial.println(F("\n[ERROR] Wi-Fi connection timed out!"));
    setLEDColor(150, 0, 0); // SOLID RED: Network Timeout
    delay(1000);
    return false;
  }
}

bool connectMQTT() {
  unsigned long startAttemptTime = millis();
  while (!mqttClient.connected() && millis() - startAttemptTime < NET_TIMEOUT_MS) {
    Serial.print(F("[MQTT] Contacting broker... "));
    
    // Connect with Last Will and Testament (LWT) configured
    // LWT Topic: tele/weather_node_01/status, Payload: "offline", QoS: 1, Retained: true
    if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_TOPIC_STATUS, 1, true, "offline")) {
      Serial.println(F("Connected."));
      
      // Publish "online" status immediately upon successful connection
      mqttClient.publish(MQTT_TOPIC_STATUS, "online", true);
      
      // Subscribe to triggers
      mqttClient.subscribe(MQTT_CMD_OTA_TOPIC);
      mqttClient.subscribe(MQTT_CMD_DEBUG_TOPIC); // Subscribe to debug override topic
      mqttClient.subscribe(MQTT_CMD_SLEEP_TOPIC); // Subscribe to dynamic sleep interval topic
      return true;
    } else {
      Serial.print(F("failed, rc="));
      Serial.print(mqttClient.state());
      Serial.println(F(". Retrying in 1.5 seconds..."));
      
      setLEDColor(150, 0, 0); // Fast Red Flash to signal broker connection issues
      delay(300);
      setLEDColor(100, 0, 100);
      delay(1200);
      feedWatchdog();
    }
  }

  if (!mqttClient.connected()) {
    Serial.println(F("\n[ERROR] MQTT connection timed out!"));
    return false;
  }
  return true;
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // 1. Create a safe copy of the payload
  char message[length + 1];
  for (unsigned int i = 0; i < length; i++) {
    message[i] = (char)payload[i];
  }
  message[length] = '\0';

  // 2. Build a diagnostic string of the payload's raw bytes (decimal ASCII values)
  String diag = "[MQTT Callback] Raw payload bytes (len=" + String(length) + "): [";
  for (unsigned int i = 0; i < length; i++) {
    diag += String((int)payload[i]);
    if (i < length - 1) diag += ", ";
  }
  diag += "]";
  logMsg(diag);

  // 3. Log raw message arrival
  logMsg("[MQTT Callback] Message arrived [" + String(topic) + "]: \"" + String(message) + "\"");

  // 4. Robust trimming of leading and trailing whitespace/newlines/control characters
  char* start = message;
  while (*start && (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n')) {
    start++;
  }
  int cleanLen = strlen(start);
  char* end = start + cleanLen - 1;
  while (end >= start && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) {
    *end = '\0';
    end--;
  }

  // 5. Convert to String and make Uppercase for robust comparison
  String cleanMsg = String(start);
  cleanMsg.toUpperCase();
  logMsg("[MQTT Callback] Cleaned, uppercase payload: \"" + cleanMsg + "\"");

  // 6. Topic Routing & State Changes
  if (strcmp(topic, MQTT_CMD_OTA_TOPIC) == 0) {
    if (cleanMsg == "ON" || cleanMsg == "TRUE" || cleanMsg == "1") {
      logMsg("[MQTT Callback] OTA bypass triggered! Suspend sleep.");
      otaModeActive = true;
    } else if (cleanMsg == "OFF" || cleanMsg == "FALSE" || cleanMsg == "0") {
      logMsg("[MQTT Callback] OTA mode deactivated! Resuming sleep path.");
      otaModeActive = false;
    }
  } 
  else if (strcmp(topic, MQTT_CMD_DEBUG_TOPIC) == 0) {
    if (cleanMsg == "ON" || cleanMsg == "TRUE" || cleanMsg == "1") {
      logMsg("[MQTT Callback] Retained Debug mode active. Sleeping for 5s instead of dynamic/default interval.");
      activeSleepDurationSec = 5; // Force next sleep state to 5 seconds
      debugModeActive = true;
    } else {
      logMsg("[MQTT Callback] Debug mode inactive. Restoring sleep interval.");
      activeSleepDurationSec = SLEEP_DURATION_SEC; // Fallback to default
      debugModeActive = false;
    }
  }
  else if (strcmp(topic, MQTT_CMD_SLEEP_TOPIC) == 0) {
    uint32_t parsedSleep = atoi(start);
    if (parsedSleep > 0 && parsedSleep <= 86400) { // Safety limit: 1 day max
      activeSleepDurationSec = parsedSleep;
      logMsg("[MQTT Callback] Dynamic sleep interval updated to " + String(activeSleepDurationSec) + " seconds.");
    } else {
      logMsg("[ERROR] Invalid sleep duration payload: " + String(start));
    }
  }
}

// ==========================================
// --- OVER-THE-AIR CONFIGURATION ----------
// ==========================================
void setupOTA() {
  // 1. RFC 1035 mDNS Hostname compliance (Convert illegal underscores to hyphens to prevent ESP-IDF crash)
  String dnsName = String(MQTT_CLIENT_ID);
  dnsName.replace("_", "-");
  ArduinoOTA.setHostname(dnsName.c_str());

  // 2. Configure OTA network port & security password from secrets.h
  ArduinoOTA.setPort(OTA_PORT);
  ArduinoOTA.setPassword(OTA_PASSWORD);

  ArduinoOTA.onStart([]() {
    Serial.println(F("[OTA] Broadcast started. Receiving flash payload..."));
    setLEDColor(0, 150, 150); // SOLID CYAN: Active flash write in progress
  });
  ArduinoOTA.onEnd([]() {
    Serial.println(F("\n[OTA] Flash transfer completed. Rebooting..."));
    setLEDColor(0, 255, 0);   // SOLID GREEN: Flash successful
    delay(1000);
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("[OTA] Transmitting: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("[OTA] Error[%u] encountered! ", error);
    setLEDColor(255, 0, 0);   // SOLID RED: Flash failed
    delay(2000);
  });

  ArduinoOTA.begin();
  Serial.println(F("[OTA] Wireless network server active."));
}

// ==========================================
// --- WATCHDOG CONTROLS ---------------------
// ==========================================
void initWatchdog() {
#if CONFIG_IDF_TARGET_ESP32C6
  // Native ESP32-C6 core configuration for Arduino v3.0+
  esp_task_wdt_config_t wdt_config = {
    .timeout_ms = WDT_TIMEOUT_SEC * 1000,
    .idle_core_mask = 1 << 0, // Monitor core 0 (single core processor)
    .trigger_panic = true     // Reset chip on failure
  };
  esp_task_wdt_init(&wdt_config);
#else
  // Fallback compatibility for older board packages
  esp_task_wdt_init(WDT_TIMEOUT_SEC, true);
#endif
  esp_task_wdt_add(NULL);     // Add current task (main thread) to watchdog loop
  Serial.println(F("[SYSTEM] Hardware Watchdog Timer initialized."));
}

void feedWatchdog() {
  esp_task_wdt_reset();
}

// ==========================================
// --- POWER MANAGEMENT & SYSTEM DEEP SLEEP -
// ==========================================
void enterDeepSleep(uint32_t seconds) {
  Serial.print(F("[SYSTEM] Powering down... Entering Deep Sleep for "));
  Serial.print(seconds);
  Serial.println(F(" seconds."));

  // Power off NeoPixel completely to save battery
  setLEDColor(0, 0, 0);
  statusLED.show();

  // Disconnect Wi-Fi nicely
  WiFi.disconnect(true);
  delay(10);

  // Configure timer wake-up and sleep
  esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
  esp_deep_sleep_start();
}

/**
 * @brief Utility to print messages to Serial and publish to MQTT log stream
 */
void logMsg(const String& msg) {
  Serial.println(msg);
  if (mqttClient.connected()) {
    mqttClient.publish(MQTT_TOPIC_LOG, msg.c_str());
  }
}

/**
 * @brief Utility to push color values to WS2812 status NeoPixel
 */
void setLEDColor(uint8_t r, uint8_t g, uint8_t b) {
  statusLED.setPixelColor(0, statusLED.Color(r, g, b));
  statusLED.show();
}
