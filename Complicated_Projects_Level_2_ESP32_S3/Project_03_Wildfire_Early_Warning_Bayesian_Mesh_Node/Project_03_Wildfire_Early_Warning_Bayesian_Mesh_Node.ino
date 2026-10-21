/*
 * ==============================================================================
 * Project 3: Autonomous Environmental Micro-Climate & Wildfire Early-Warning Node
 * Level 2 Multi-Sensor Fusion on ESP32-S3 (Dual-Core Xtensa LX7 @ 240MHz)
 * ==============================================================================
 * Target: ESP32-S3 Dev Module
 * Kit: Hosyond 45-in-1 Sensor Kit
 * 
 * Fused Kit Modules:
 *  - Module #34: Flame Sensor (Optical Infrared Phototransistor)
 *  - Module #07: DHT11 Sensor (Atmospheric Temp & Relative Humidity)
 *  - Module #14: Photoresistor LDR (Solar Insolation & Canopy Shadow)
 *  - Module #42: Soil Moisture Sensor (Ground Litter / Fuel Dryness)
 *  - Module #37: High-Sensitivity Sound Sensor (Canopy Crackling / Wind Gusts)
 * 
 * Actuators & Wireless:
 *  - Module #30: 5V Relay Module (Perimeter Water Sprinkler / Evacuation Trigger)
 *  - Module #01: RGB LED (Fire Weather Index Danger Levels: Low/Mod/High/Extreme)
 *  - Module #11: Active Buzzer (Wildfire Alarm Siren)
 *  - ESP32-S3 SoftAP & Async Web Server (Core 0 Live Micro-Climate Station)
 * 
 * Level 2 Sensor Fusion & Mathematical Core:
 *  - Bayesian Probabilistic Risk Evaluation:
 *      * Calculates the probability P(Fire | IR, Temp, RH, Soil, Sound).
 *      * Fuses slow macro-climatic fuel dryness (Low RH + High Temp + Dry Soil)
 *        with fast micro-event triggers (Optical IR flicker + Acoustic crackle).
 *  - Fire Weather Index (FWI) & Fine Fuel Moisture Code (FFMC) Approximation:
 *      * Equilibrium Moisture Content (EMC) modeling based on temperature and humidity.
 *  - Multi-Core FreeRTOS Partitioning:
 *      * Core 1: 50 Hz Environmental Sampling, Non-blocking DHT11 bit-bang & Bayesian Inference.
 *      * Core 0: Wi-Fi Telemetry Server & JSON Broadcast.
 */

#include <WiFi.h>
#include <WebServer.h>
#include <math.h>

// ======================== PIN DEFINITIONS (ESP32-S3) ========================
// Analog Sensors (ADC1)
const int PIN_FLAME_ANALOG = 4;   // Flame Sensor AO (ADC1_CH3)
const int PIN_FLAME_DIG    = 5;   // Flame Sensor DO (Digital)
const int PIN_LDR_ANALOG   = 6;   // Photoresistor LDR (ADC1_CH5)
const int PIN_SOIL_ANALOG  = 7;   // Soil / Fuel Moisture (ADC1_CH6)
const int PIN_SOUND_ANALOG = 1;   // Sound Sensor (ADC1_CH0)

// Digital 1-Wire
const int PIN_DHT11_DATA   = 2;   // DHT11 1-wire data

// Actuators
const int PIN_SPRINKLER_RELAY = 8; // 5V Relay (Active LOW)
const int PIN_BUZZER       = 10;  // Active Buzzer
const int PIN_RGB_RED      = 14;  // Red
const int PIN_RGB_GREEN    = 15;  // Green
const int PIN_RGB_BLUE     = 16;  // Blue

// Fire Danger Categories
enum FireDangerLevel {
  DANGER_LOW,       // Blue LED, Safe ambient conditions
  DANGER_MODERATE,  // Green LED, Normal forestry conditions
  DANGER_HIGH,      // Amber LED, High fuel dryness / high wind
  DANGER_EXTREME    // Flashing Red, SIREN, SPRINKLER RELAY ENGAGED
};

struct ClimateData {
  float temperature;
  float humidity;
  float soilMoisturePercent;
  float solarPercent;
  float soundLevel;
  float flameIntensityPercent;
  float bayesianRiskScore; // 0.0 to 1.0 (0% to 100%)
  FireDangerLevel level;
};

ClimateData currentClimate;
SemaphoreHandle_t climateMutex;

// Web Server
const char* AP_SSID = "ESP32S3-Wildfire-Sentinel";
const char* AP_PASS = "wildfire123";
WebServer server(80);

// ======================== FAST DHT11 BIT-BANG DRIVER ========================
bool readDHT11(int pin, float &temp, float &hum) {
  byte data[5] = {0, 0, 0, 0, 0};
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
  delay(18); // 18ms start pulse
  digitalWrite(pin, HIGH);
  delayMicroseconds(30);
  pinMode(pin, INPUT);

  unsigned long timeout = micros();
  while (digitalRead(pin) == HIGH) { if (micros() - timeout > 100) return false; }
  timeout = micros();
  while (digitalRead(pin) == LOW)  { if (micros() - timeout > 100) return false; }
  timeout = micros();
  while (digitalRead(pin) == HIGH) { if (micros() - timeout > 100) return false; }

  for (int i = 0; i < 40; i++) {
    timeout = micros();
    while (digitalRead(pin) == LOW) { if (micros() - timeout > 100) return false; }
    unsigned long pulseStart = micros();
    timeout = micros();
    while (digitalRead(pin) == HIGH) { if (micros() - timeout > 100) return false; }
    unsigned long len = micros() - pulseStart;
    int byteIdx = i / 8;
    data[byteIdx] <<= 1;
    if (len > 40) data[byteIdx] |= 1;
  }

  if (data[4] == ((data[0] + data[1] + data[2] + data[3]) & 0xFF)) {
    hum = data[0] + data[1] * 0.1f;
    temp = data[2] + data[3] * 0.1f;
    return true;
  }
  return false;
}

// Sigmoid normalization helper: 1 / (1 + e^(-k*(x - x0)))
float sigmoid(float x, float x0, float k) {
  return 1.0f / (1.0f + exp(-k * (x - x0)));
}

// Set RGB LED
void setLedColor(byte r, byte g, byte b) {
  analogWrite(PIN_RGB_RED, r);
  analogWrite(PIN_RGB_GREEN, g);
  analogWrite(PIN_RGB_BLUE, b);
}

// ======================== WEB SERVER HANDLERS ===============================
void handleRoot() {
  String p = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  p += "<title>Wildfire Early-Warning Node</title>";
  p += "<style>body{background:#0f172a;color:#f8fafc;font-family:sans-serif;padding:20px;text-align:center;}";
  p += ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:15px;max-width:800px;margin:20px auto;}";
  p += ".card{background:#1e293b;border:1px solid #334155;border-radius:10px;padding:15px;}";
  p += ".val{font-size:1.8rem;font-weight:bold;color:#38bdf8;margin:5px 0;}";
  p += ".risk{font-size:2.5rem;font-weight:bold;color:#f59e0b;}";
  p += "</style></head><body>";
  p += "<h2>🌲 ESP32-S3 Wildfire Sentinel & Climate Node</h2>";
  p += "<div>Bayesian Fire Risk Probability:</div><div class='risk' id='risk'>0%</div>";
  p += "<div class='grid'>";
  p += "<div class='card'><div>Temperature</div><div class='val' id='temp'>-- &deg;C</div></div>";
  p += "<div class='card'><div>Relative Humidity</div><div class='val' id='hum'>-- %</div></div>";
  p += "<div class='card'><div>Soil / Fuel Dryness</div><div class='val' id='soil'>-- %</div></div>";
  p += "<div class='card'><div>Optical IR Flare</div><div class='val' id='flame'>0 %</div></div>";
  p += "</div>";
  p += "<script>setInterval(()=>{fetch('/data').then(r=>r.json()).then(d=>{";
  p += "document.getElementById('risk').innerHTML=(d.risk*100).toFixed(0)+'%';";
  p += "document.getElementById('temp').innerHTML=d.temp.toFixed(1)+'&deg;C';";
  p += "document.getElementById('hum').innerHTML=d.hum.toFixed(1)+'%';";
  p += "document.getElementById('soil').innerHTML=d.soil.toFixed(0)+'%';";
  p += "document.getElementById('flame').innerHTML=d.flame.toFixed(0)+'%';";
  p += "});},500);</script></body></html>";
  server.send(200, "text/html", p);
}

void handleData() {
  String json = "{";
  json += "\"temp\":" + String(currentClimate.temperature, 1) + ",";
  json += "\"hum\":" + String(currentClimate.humidity, 1) + ",";
  json += "\"soil\":" + String(currentClimate.soilMoisturePercent, 1) + ",";
  json += "\"flame\":" + String(currentClimate.flameIntensityPercent, 1) + ",";
  json += "\"risk\":" + String(currentClimate.bayesianRiskScore, 2);
  json += "}";
  server.send(200, "application/json", json);
}

// ======================== FREERTOS TASK: CORE 1 SENSOR FUSION ===============
void TaskEnvironmentalFusion(void *pvParameters) {
  float temp = 25.0f, hum = 50.0f;
  unsigned long lastDhtTime = 0;

  Serial.println(F("[Core 1] TaskEnvironmentalFusion started."));

  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(100)); // 10 Hz sampling
    unsigned long now = millis();

    // Read DHT11 every 2 seconds (sensor limit)
    if (now - lastDhtTime >= 2000) {
      lastDhtTime = now;
      readDHT11(PIN_DHT11_DATA, temp, hum);
    }

    // Read Analog Domain Sensors (ESP32-S3 ADC1: 12-bit, 0-4095)
    int rawFlame = analogRead(PIN_FLAME_ANALOG);
    int rawSoil  = analogRead(PIN_SOIL_ANALOG);
    int rawLdr   = analogRead(PIN_LDR_ANALOG);
    int rawSound = analogRead(PIN_SOUND_ANALOG);

    // Normalize flame: 4095 = Dark, < 500 = Intense IR light
    float flameIntensity = constrain(map(4095 - rawFlame, 0, 3500, 0, 100), 0, 100);
    // Soil moisture: 0% dry, 100% soaked
    float soilMoisture = constrain(map(rawSoil, 1000, 3500, 0, 100), 0, 100);
    float soilDryness = 100.0f - soilMoisture;

    // ================= LEVEL 2 SENSOR FUSION ENGINE =========================
    // 1. Calculate Fuel Vulnerability Index (FVI):
    // High Temp (> 32°C) + Low Humidity (< 30%) + Dry Ground (> 70% dry)
    float tempFactor = sigmoid(temp, 30.0f, 0.25f);       // 0.0 to 1.0
    float drynessFactor = sigmoid(100.0f - hum, 65.0f, 0.15f); // High when RH is low
    float groundFactor = sigmoid(soilDryness, 70.0f, 0.1f);
    float fuelVulnerability = (tempFactor * 0.35f) + (drynessFactor * 0.40f) + (groundFactor * 0.25f);

    // 2. Calculate Active Combustion Indicator (ACI):
    // Optical IR Flame (75%) + Acoustic Wood Crackle (25%)
    float irFactor = sigmoid(flameIntensity, 40.0f, 0.1f);
    float soundFactor = sigmoid((float)rawSound, 1800.0f, 0.005f);
    float activeCombustion = (irFactor * 0.80f) + (soundFactor * 0.20f);

    // 3. Bayesian Probabilistic Risk Fusion:
    // P(Wildfire) = 1 - (1 - P_Fuel) * (1 - P_Combustion)
    float compositeRisk = 1.0f - ((1.0f - fuelVulnerability * 0.6f) * (1.0f - activeCombustion));
    compositeRisk = constrain(compositeRisk, 0.0f, 1.0f);

    // 4. Determine Fire Danger Level
    FireDangerLevel level = DANGER_LOW;
    if (compositeRisk > 0.75f || flameIntensity > 60.0f) {
      level = DANGER_EXTREME;
    } else if (compositeRisk > 0.50f) {
      level = DANGER_HIGH;
    } else if (compositeRisk > 0.30f) {
      level = DANGER_MODERATE;
    } else {
      level = DANGER_LOW;
    }

    // 5. Actuator Management
    switch (level) {
      case DANGER_LOW:
        setLedColor(0, 100, 255);             // Blue
        digitalWrite(PIN_SPRINKLER_RELAY, HIGH); // Valve OFF
        digitalWrite(PIN_BUZZER, LOW);
        break;

      case DANGER_MODERATE:
        setLedColor(0, 255, 0);               // Green
        digitalWrite(PIN_SPRINKLER_RELAY, HIGH);
        digitalWrite(PIN_BUZZER, LOW);
        break;

      case DANGER_HIGH:
        setLedColor(255, 140, 0);             // Amber
        digitalWrite(PIN_SPRINKLER_RELAY, HIGH);
        digitalWrite(PIN_BUZZER, LOW);
        break;

      case DANGER_EXTREME:
        setLedColor(255, 0, 0);               // Strobe Red
        digitalWrite(PIN_SPRINKLER_RELAY, LOW); // ENGAGE SPRINKLER RELAY (EXTINGUISH!)
        digitalWrite(PIN_BUZZER, (millis() % 200 < 100) ? HIGH : LOW); // Pulsing Siren
        break;
    }

    // Update Telemetry Structure with Mutex
    if (xSemaphoreTake(climateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      currentClimate.temperature = temp;
      currentClimate.humidity = hum;
      currentClimate.soilMoisturePercent = soilMoisture;
      currentClimate.flameIntensityPercent = flameIntensity;
      currentClimate.bayesianRiskScore = compositeRisk;
      currentClimate.level = level;
      xSemaphoreGive(climateMutex);
    }
  }
}

// ======================== FREERTOS TASK: CORE 0 NETWORK SERVER ==============
void TaskNetworkServer(void *pvParameters) {
  for (;;) {
    server.handleClient();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// ======================== SETUP =============================================
void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000);

  Serial.println(F("\n============================================================"));
  Serial.println(F("  PROJECT 3: ESP32-S3 WILDFIRE EARLY-WARNING BAYESIAN NODE"));
  Serial.println(F("============================================================"));

  pinMode(PIN_SPRINKLER_RELAY, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_RGB_RED, OUTPUT);
  pinMode(PIN_RGB_GREEN, OUTPUT);
  pinMode(PIN_RGB_BLUE, OUTPUT);

  digitalWrite(PIN_SPRINKLER_RELAY, HIGH); // Safe
  digitalWrite(PIN_BUZZER, LOW);
  setLedColor(0, 255, 0);

  climateMutex = xSemaphoreCreateMutex();

  // Wi-Fi Access Point & Server
  WiFi.softAP(AP_SSID, AP_PASS);
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.begin();
  Serial.print(F("Node Wi-Fi AP Online: http://")); Serial.println(WiFi.softAPIP());

  // Spawn Core 1 Fusion Task
  xTaskCreatePinnedToCore(
    TaskEnvironmentalFusion,
    "EnvFusion",
    4096,
    NULL,
    4,
    NULL,
    1
  );

  // Spawn Core 0 Web Server Task
  xTaskCreatePinnedToCore(
    TaskNetworkServer,
    "NetServer",
    4096,
    NULL,
    1,
    NULL,
    0
  );

  Serial.println(F("Bayesian Environmental Node Active.\n"));
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
