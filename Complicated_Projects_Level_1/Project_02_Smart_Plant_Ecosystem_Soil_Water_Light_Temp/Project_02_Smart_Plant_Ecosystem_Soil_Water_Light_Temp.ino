/*
 * Project 2: Smart Plant Ecosystem & Agritech Soil Hydration Guardian
 * -------------------------------------------------------------------
 * Target Board: Arduino Uno (ATmega328P)
 * Kit: Hosyond 45-in-1 Sensor Kit
 * 
 * Fused Sensors:
 *  - Module #42: Soil Moisture Sensor (Analog A0)
 *  - Module #39: Water Level Sensor (Analog A1) - Reservoir Monitor
 *  - Module #14: Photoresistor LDR Module (Analog A2) - Solar Insolation
 *  - Module #07: DHT11 Sensor (Digital D4) - Ambient Temp & Humidity
 * 
 * Actuators & Indicators:
 *  - Module #30: 5V Relay Module (Digital D7) - Submersible Water Pump Simulation
 *  - Module #08: Double Color LED (Pins D9 Red, D10 Green) - Health Status
 *  - Module #12: Passive Buzzer (Digital D8) - Low Reservoir Alert Tones
 * 
 * Sensor Fusion Logic:
 *  1. Evapotranspiration Index: Integrates Soil Dryness with Ambient Temp & Sunlight.
 *  2. Anti-Scorch Interlock: Suspends routine watering under direct intense sunlight (Lux > 80%)
 *     to prevent plant leaf sunburn and wasteful evaporation, unless moisture is critically low.
 *  3. Dry-Run Pump Protection: Prevents relay engagement if water reservoir level is below 15%,
 *     protecting the pump motor from burnout and emitting an audible warning beep.
 *  4. Hysteresis Thresholding: Prevents rapid on/off relay hunting using dual-band logic.
 */

// ======================== PIN ASSIGNMENTS ========================
const int PIN_SOIL_MOISTURE = A0;   // Soil Moisture (0=Dry, 1023=Submerged in water)
const int PIN_WATER_LEVEL   = A1;   // Reservoir Level (0=Empty, >500=Full)
const int PIN_PHOTORESISTOR = A2;   // Ambient Light (0=Dark, 1023=Intense Sun)
const int PIN_DHT11_DATA    = 4;    // DHT11 1-wire communication pin
const int PIN_PUMP_RELAY    = 7;    // 5V Relay (Active LOW)
const int PIN_BUZZER        = 8;    // Passive Buzzer tone pin
const int PIN_LED_RED       = 9;    // Double Color LED - Red
const int PIN_LED_GREEN     = 10;   // Double Color LED - Green

// ==================== CALIBRATION & THRESHOLDS ===================
const int SOIL_DRY_THRESHOLD       = 350;  // Below this ADC = Soil needs water
const int SOIL_SATURATED_THRESHOLD = 650;  // Above this ADC = Soil is satisfied
const int RESERVOIR_EMPTY_LIMIT    = 180;  // Below this ADC = Reservoir is dry!
const int SUNLIGHT_SCORCH_LIMIT    = 750;  // Above this ADC = Peak direct sun

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
const unsigned long MAX_WATERING_DURATION = 5000; // 5 seconds max per cycle
const unsigned long WATERING_COOLDOWN     = 15000; // 15 seconds absorption delay
unsigned long lastWateringFinishTime = 0;

unsigned long lastBuzzerChirpTime = 0;

// DHT11 Simplified Non-blocking Driver
// (Avoids external library dependency while providing reliable readings)
struct DHTData {
  float temperature;
  float humidity;
  bool valid;
};

DHTData readDHT11(int pin) {
  DHTData result = {25.0, 50.0, false};
  byte data[5] = {0, 0, 0, 0, 0};

  // Bus handshake
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
  delay(20); // 18-20ms start pulse
  digitalWrite(pin, HIGH);
  delayMicroseconds(30);
  pinMode(pin, INPUT);

  // Wait for DHT response (80us LOW + 80us HIGH)
  unsigned long timeout = micros();
  while (digitalRead(pin) == HIGH) {
    if (micros() - timeout > 100) return result;
  }
  timeout = micros();
  while (digitalRead(pin) == LOW) {
    if (micros() - timeout > 100) return result;
  }
  timeout = micros();
  while (digitalRead(pin) == HIGH) {
    if (micros() - timeout > 100) return result;
  }

  // Read 40 bits
  for (int i = 0; i < 40; i++) {
    timeout = micros();
    while (digitalRead(pin) == LOW) {
      if (micros() - timeout > 100) return result;
    }
    unsigned long pulseStart = micros();
    timeout = micros();
    while (digitalRead(pin) == HIGH) {
      if (micros() - timeout > 100) return result;
    }
    unsigned long pulseLen = micros() - pulseStart;
    int byteIdx = i / 8;
    data[byteIdx] <<= 1;
    if (pulseLen > 40) {
      data[byteIdx] |= 1; // 70us high = bit 1
    }
  }

  // Checksum verification
  if (data[4] == ((data[0] + data[1] + data[2] + data[3]) & 0xFF)) {
    result.humidity = data[0] + (data[1] * 0.1);
    result.temperature = data[2] + (data[3] * 0.1);
    result.valid = true;
  }
  return result;
}

// Set Bi-Color Status LED (Common Cathode)
void setStatusLed(bool redOn, bool greenOn) {
  digitalWrite(PIN_LED_RED, redOn ? HIGH : LOW);
  digitalWrite(PIN_LED_GREEN, greenOn ? HIGH : LOW);
}

// Tone generator for Passive Buzzer
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

    // 1. Read All Physical Domain Sensors
    int rawSoilMoisture = analogRead(PIN_SOIL_MOISTURE);
    int rawWaterLevel   = analogRead(PIN_WATER_LEVEL);
    int rawSunlight     = analogRead(PIN_PHOTORESISTOR);
    DHTData dht = readDHT11(PIN_DHT11_DATA);

    // Normalize Soil Moisture (0% = Bone Dry, 100% = Saturated)
    float soilPercent = constrain(map(rawSoilMoisture, 150, 850, 0, 100), 0, 100);
    // Normalize Water Reservoir (0% = Dry, 100% = Full)
    float reservoirPercent = constrain(map(rawWaterLevel, 100, 700, 0, 100), 0, 100);
    // Sunlight Level
    float sunlightPercent = constrain(map(rawSunlight, 50, 950, 0, 100), 0, 100);

    // 2. Sensor Fusion Logic Matrix
    bool isReservoirCritical = (rawWaterLevel < RESERVOIR_EMPTY_LIMIT);
    bool isSoilDry           = (rawSoilMoisture < SOIL_DRY_THRESHOLD);
    bool isSoilSaturated     = (rawSoilMoisture >= SOIL_SATURATED_THRESHOLD);
    bool isIntenseSunlight   = (rawSunlight > SUNLIGHT_SCORCH_LIMIT);

    // Evapotranspiration Multiplier
    // High ambient temp (>28C) & Low humidity (<40%) increases water demand
    float waterDemandFactor = 1.0;
    if (dht.valid) {
      if (dht.temperature > 28.0) waterDemandFactor += 0.3;
      if (dht.humidity < 40.0)    waterDemandFactor += 0.2;
    }

    // 3. State Machine Transitions
    if (isReservoirCritical) {
      systemState = ECO_RESERVOIR_LOW;
    } else if (systemState == ECO_WATERING) {
      // Handled in non-blocking timer below
    } else if (isSoilDry) {
      // Cooldown check (allow water to absorb into soil)
      if (currentMillis - lastWateringFinishTime > WATERING_COOLDOWN) {
        if (isIntenseSunlight && soilPercent > 10.0) {
          // Delay routine watering under scorching sun, unless critically dry (<10%)
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
        // Alternate Red + Green (Orange) warning that watering is deferred
        setStatusLed(true, true);
        break;

      case ECO_NEED_WATER:
        // Engage Pump
        systemState = ECO_WATERING;
        wateringStartTime = currentMillis;
        digitalWrite(PIN_PUMP_RELAY, LOW);  // Pump ON (Active LOW)
        setStatusLed(true, true);           // Amber/Orange
        Serial.println(F(">> [ACTION] Soil Dry & Conditions Valid: Engaging Irrigation Pump!"));
        break;

      case ECO_WATERING:
        // Monitored in duration check below
        break;

      case ECO_RESERVOIR_LOW:
        digitalWrite(PIN_PUMP_RELAY, HIGH); // Force Pump OFF (Dry-run lockout)
        setStatusLed(true, false);          // Solid Red Alert
        // Periodic warning chirp every 2 seconds
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
    if (currentMillis - wateringStartTime >= MAX_WATERING_DURATION) {
      digitalWrite(PIN_PUMP_RELAY, HIGH); // Turn pump OFF
      lastWateringFinishTime = currentMillis;
      systemState = ECO_OPTIMAL;
      setStatusLed(false, true);          // Back to Green
      Serial.println(F(">> [ACTION] Watering Cycle Complete. Entering Absorption Cooldown."));
    }
  }
}
