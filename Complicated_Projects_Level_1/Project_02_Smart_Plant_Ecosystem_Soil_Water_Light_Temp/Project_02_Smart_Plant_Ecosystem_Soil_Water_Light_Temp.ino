/*
 * Project 2: Smart Plant Ecosystem & Agritech Soil Hydration Guardian
 * -------------------------------------------------------------------
 * Target Board: Arduino Uno (ATmega328P)
 * Kit: Hosyond 45-in-1 Sensor Kit
 * 
 * Fused Sensors:
 *  - Module #42: Soil Moisture Sensor (Analog A0) [Kit: High ADC = Dry, Low ADC = Wet]
 *  - Module #39: Water Level Sensor (Analog A1) - Reservoir Monitor
 *  - Module #14: Photoresistor LDR Module (Analog A2) [Inverted: High ADC = Bright Sun]
 *  - Module #07: DHT11 Sensor (Digital D4) - Ambient Temp & Humidity
 * 
 * Actuators & Indicators:
 *  - Module #30: 5V Relay Module (Digital D7) - Submersible Water Pump Simulation
 *  - Module #08: Double Color LED (Pins D9 Red, D10 Green) - Health Status
 *  - Module #12: Passive Buzzer (Digital D8) - Low Reservoir Alert Tones
 * 
 * Sensor Fusion Logic:
 *  1. Corrected Polarity: Soil sensor reads dry at high ADC (matching kit #42 reference).
 *     Photoresistor is inverted so higher value = brighter sun (matching kit #14 reference).
 *  2. Evapotranspiration Index: Dynamic watering duration modulated by ambient temp/humidity.
 *  3. Anti-Scorch Interlock: Suspends routine watering under direct intense midday sun (>75%).
 *  4. Dry-Run Pump Protection: Locks out pump when reservoir is depleted (<15%).
 *  5. Soil Hysteresis: Stops watering once soil reaches saturation threshold.
 */

// ======================== PIN ASSIGNMENTS ========================
const int PIN_SOIL_MOISTURE = A0;   // Soil Moisture (High = Dry, Low = Wet)
const int PIN_WATER_LEVEL   = A1;   // Reservoir Level (0=Empty, >500=Full)
const int PIN_PHOTORESISTOR = A2;   // Ambient Light (Raw is high in dark)
const int PIN_DHT11_DATA    = 4;    // DHT11 1-wire communication pin
const int PIN_PUMP_RELAY    = 7;    // 5V Relay (Active LOW)
const int PIN_BUZZER        = 8;    // Passive Buzzer tone pin
const int PIN_LED_RED       = 9;    // Double Color LED - Red
const int PIN_LED_GREEN     = 10;   // Double Color LED - Green

// ==================== CALIBRATION & THRESHOLDS ===================
// Aligned with Kit #42 reference sketch: >= 800 is dry
const int SOIL_DRY_THRESHOLD       = 750;  // Above this ADC = Dry soil (Needs water)
const int SOIL_SATURATED_THRESHOLD = 450;  // Below this ADC = Saturated soil (Stop watering)
const int RESERVOIR_EMPTY_LIMIT    = 180;  // Below this ADC = Reservoir is empty!
const int SUNLIGHT_SCORCH_LIMIT    = 750;  // Inverted lux above this = Direct midday sun

// ===================== STATE MACHINE =============================
enum EcosystemState {
  ECO_OPTIMAL,        // Soil moist, reservoir OK, green LED
  ECO_NEED_WATER,     // Soil dry, waiting for optimal light conditions
  ECO_WATERING,       // Relay active, pump running, amber LED
  ECO_RESERVOIR_LOW,  // Reservoir empty! Pump locked out, red LED + buzzer alert
  ECO_SUN_SUSPENDED   // Soil dry but waiting for direct midday sun to pass
};

EcosystemState systemState = ECO_OPTIMAL;

// Non-blocking Timers
unsigned long lastSensorReadTime = 0;
const unsigned long SENSOR_INTERVAL = 1000; // ms

unsigned long wateringStartTime = 0;
unsigned long calculatedWateringDuration = 4000; // Dynamically computed from evapotranspiration
const unsigned long WATERING_COOLDOWN = 15000;   // 15 seconds absorption delay
unsigned long lastWateringFinishTime = 0;

unsigned long lastBuzzerChirpTime = 0;

// DHT11 Simplified Non-blocking Driver
struct DHTData {
  float temperature;
  float humidity;
  bool valid;
};

DHTData readDHT11(int pin) {
  DHTData result = {25.0, 50.0, false};
  byte data[5] = {0, 0, 0, 0, 0};

  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
  delay(20);
  digitalWrite(pin, HIGH);
  delayMicroseconds(30);
  pinMode(pin, INPUT);

  unsigned long timeout = micros();
  while (digitalRead(pin) == HIGH) { if (micros() - timeout > 100) return result; }
  timeout = micros();
  while (digitalRead(pin) == LOW)  { if (micros() - timeout > 100) return result; }
  timeout = micros();
  while (digitalRead(pin) == HIGH) { if (micros() - timeout > 100) return result; }

  for (int i = 0; i < 40; i++) {
    timeout = micros();
    while (digitalRead(pin) == LOW) { if (micros() - timeout > 100) return result; }
    unsigned long pulseStart = micros();
    timeout = micros();
    while (digitalRead(pin) == HIGH) { if (micros() - timeout > 100) return result; }
    unsigned long pulseLen = micros() - pulseStart;
    int byteIdx = i / 8;
    data[byteIdx] <<= 1;
    if (pulseLen > 40) data[byteIdx] |= 1;
  }

  if (data[4] == ((data[0] + data[1] + data[2] + data[3]) & 0xFF)) {
    result.humidity = data[0] + (data[1] * 0.1);
    result.temperature = data[2] + (data[3] * 0.1);
    result.valid = true;
  }
  return result;
}

void setStatusLed(bool redOn, bool greenOn) {
  digitalWrite(PIN_LED_RED, redOn ? HIGH : LOW);
  digitalWrite(PIN_LED_GREEN, greenOn ? HIGH : LOW);
}

void playAlarmTone(int freq, int durationMs) {
  tone(PIN_BUZZER, freq, durationMs);
}

// ======================== SETUP ==================================
void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000);

  Serial.println(F("\n======================================================="));
  Serial.println(F("  PROJECT 2: SMART PLANT ECOSYSTEM & SENSOR FUSION HUB"));
  Serial.println(F("======================================================="));

  pinMode(PIN_PUMP_RELAY, OUTPUT);
  pinMode(PIN_LED_RED, OUTPUT);
  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  // Safe Defaults
  digitalWrite(PIN_PUMP_RELAY, HIGH); // Relay OFF (Active LOW)
  setStatusLed(false, true);          // Green = OK
  noTone(PIN_BUZZER);

  Serial.println(F("Sensors Initialized. Beginning Autonomous Irrigation Loop...\n"));
}

// ======================== MAIN LOOP ==============================
void loop() {
  unsigned long currentMillis = millis();

  // Periodic Sensor Sampling
  if (currentMillis - lastSensorReadTime >= SENSOR_INTERVAL) {
    lastSensorReadTime = currentMillis;

    // 1. Read Raw Sensors
    int rawSoilMoisture = analogRead(PIN_SOIL_MOISTURE); // High = Dry, Low = Wet
    int rawWaterLevel   = analogRead(PIN_WATER_LEVEL);
    int rawLdr          = analogRead(PIN_PHOTORESISTOR); // Kit #14: High = Dark
    int rawSunlight     = 1023 - rawLdr;                 // Inverted: High = Bright sun
    DHTData dht         = readDHT11(PIN_DHT11_DATA);

    // Normalize Soil Moisture (0% = Bone Dry, 100% = Saturated)
    float soilPercent = 100.0 - constrain(map(rawSoilMoisture, 350, 850, 0, 100), 0, 100);
    // Normalize Water Reservoir (0% = Dry, 100% = Full)
    float reservoirPercent = constrain(map(rawWaterLevel, 100, 700, 0, 100), 0, 100);
    // Normalize Sunlight
    float sunlightPercent = constrain(map(rawSunlight, 50, 950, 0, 100), 0, 100);

    // 2. Sensor Fusion Logic Matrix
    bool isReservoirCritical = (rawWaterLevel < RESERVOIR_EMPTY_LIMIT);
    bool isSoilDry           = (rawSoilMoisture >= SOIL_DRY_THRESHOLD);
    bool isSoilSaturated     = (rawSoilMoisture <= SOIL_SATURATED_THRESHOLD);
    bool isIntenseSunlight   = (rawSunlight >= SUNLIGHT_SCORCH_LIMIT);

    // Evapotranspiration Multiplier
    float waterDemandFactor = 1.0;
    if (dht.valid) {
      if (dht.temperature > 28.0) waterDemandFactor += 0.3;
      if (dht.humidity < 40.0)    waterDemandFactor += 0.2;
    }
    // Calculate dynamic watering duration (between 2.5s and 6.0s)
    calculatedWateringDuration = constrain((unsigned long)(3500 * waterDemandFactor), 2500, 6000);

    // 3. State Machine Transitions
    if (isReservoirCritical) {
      systemState = ECO_RESERVOIR_LOW;
    } else if (systemState == ECO_WATERING) {
      // Check if soil has become saturated early (hysteresis cutoff)
      if (isSoilSaturated) {
        digitalWrite(PIN_PUMP_RELAY, HIGH); // Turn pump OFF
        lastWateringFinishTime = currentMillis;
        systemState = ECO_OPTIMAL;
        setStatusLed(false, true);
        Serial.println(F(">> [ACTION] Soil Saturated reached. Shutting down pump early."));
      }
    } else if (isSoilDry) {
      if (currentMillis - lastWateringFinishTime > WATERING_COOLDOWN) {
        if (isIntenseSunlight && soilPercent > 10.0) {
          // Delay routine watering under scorching sun to prevent leaf burns
          systemState = ECO_SUN_SUSPENDED;
        } else {
          systemState = ECO_NEED_WATER;
        }
      } else {
        systemState = ECO_OPTIMAL; // In absorption cooldown
      }
    } else {
      systemState = ECO_OPTIMAL;
    }

    // 4. State Execution
    switch (systemState) {
      case ECO_OPTIMAL:
        digitalWrite(PIN_PUMP_RELAY, HIGH); // Pump OFF
        setStatusLed(false, true);          // Solid Green
        break;

      case ECO_SUN_SUSPENDED:
        digitalWrite(PIN_PUMP_RELAY, HIGH); // Pump OFF
        setStatusLed(true, true);           // Orange warning: watering deferred
        break;

      case ECO_NEED_WATER:
        systemState = ECO_WATERING;
        wateringStartTime = currentMillis;
        digitalWrite(PIN_PUMP_RELAY, LOW);  // Pump ON (Active LOW)
        setStatusLed(true, true);           // Amber
        Serial.print(F(">> [ACTION] Engaging Pump! Scheduled Duration: "));
        Serial.print(calculatedWateringDuration);
        Serial.println(F(" ms"));
        break;

      case ECO_WATERING:
        break;

      case ECO_RESERVOIR_LOW:
        digitalWrite(PIN_PUMP_RELAY, HIGH); // Force Pump OFF (Dry-run lockout)
        setStatusLed(true, false);          // Red Alert
        if (currentMillis - lastBuzzerChirpTime > 2000) {
          lastBuzzerChirpTime = currentMillis;
          playAlarmTone(1200, 250);
        }
        break;
    }

    // 5. Diagnostics Telemetry
    Serial.print(F("[ECO-HUB] Soil: "));
    Serial.print((int)soilPercent);
    Serial.print(F("% (ADC: ")); Serial.print(rawSoilMoisture); Serial.print(F(") | "));
    Serial.print(F("Res: "));
    Serial.print((int)reservoirPercent);
    Serial.print(F("% | Sun: "));
    Serial.print((int)sunlightPercent);
    Serial.print(F("% | Temp: "));
    Serial.print(dht.temperature, 1);
    Serial.print(F("C | Hum: "));
    Serial.print(dht.humidity, 1);
    Serial.print(F("% | State: "));

    switch (systemState) {
      case ECO_OPTIMAL:       Serial.println(F("OPTIMAL [Normal]")); break;
      case ECO_SUN_SUSPENDED: Serial.println(F("SUN_SUSPENDED [Anti-Scorch Delay]")); break;
      case ECO_NEED_WATER:    Serial.println(F("NEED_WATER [Queued]")); break;
      case ECO_WATERING:      Serial.println(F("WATERING [Pump Running]")); break;
      case ECO_RESERVOIR_LOW: Serial.println(F("ALERT: RESERVOIR EMPTY! [Pump Locked]")); break;
    }
  }

  // 6. Non-blocking Watering Duration Controller
  if (systemState == ECO_WATERING) {
    if (currentMillis - wateringStartTime >= calculatedWateringDuration) {
      digitalWrite(PIN_PUMP_RELAY, HIGH); // Turn pump OFF
      lastWateringFinishTime = currentMillis;
      systemState = ECO_OPTIMAL;
      setStatusLed(false, true);
      Serial.println(F(">> [ACTION] Watering Duration Expired. Entering Absorption Cooldown."));
    }
  }
}
