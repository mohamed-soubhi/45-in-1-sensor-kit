/*
 * Project 1: Environmental Hazard Guardian (Multi-Sensor Fusion)
 * -------------------------------------------------------------
 * Target Board: Arduino Uno (ATmega328P)
 * Kit: Hosyond 45-in-1 Sensor Kit
 * 
 * Fused Sensors:
 *  - Module #34: Flame Sensor (Analog AO & Digital DO)
 *  - Module #13: Analog Temperature Sensor (NTC Thermistor / LM35)
 *  - Module #37: High Sensitivity Sound Detection Sensor (Analog AO & Digital DO)
 * 
 * Actuators & Indicators:
 *  - Module #01: RGB LED (PWM pins for smooth color transitions)
 *  - Module #11: Active Buzzer (Pulsed alarm siren)
 *  - Module #30: 5V Relay Module (Emergency exhaust / Power cutoff)
 * 
 * Sensor Fusion Principle:
 *  Traditional single-sensor alarms suffer from false positives and thermal lag:
 *   - Optical IR flame sensors can trigger from direct sunlight or halogen reflections.
 *   - Thermal sensors alone take seconds to minutes to heat up to an absolute limit.
 *   - Acoustic sensors alone trigger on hand claps or ambient noises.
 *  
 *  This sketch implements a Multi-Factor Risk Assessment (Sensor Fusion):
 *   1. Measures raw IR flame radiation intensity (Analog A0).
 *   2. Computes the instantaneous temperature derivative dT/dt (rate of rise) on A1.
 *   3. Analyzes sound pressure spikes on A2 and digital sound pulses on D2.
 *   4. Combines inputs into a dynamic Risk Score (0 - 100%).
 *   5. Executes non-blocking state machine: NORMAL -> ADVISORY -> WARNING -> EMERGENCY.
 */

// ======================== PIN DEFINITIONS ========================
// Sensors
const int PIN_FLAME_ANALOG = A0;   // Flame Sensor AO (lower value = stronger flame)
const int PIN_FLAME_DIGITAL = 4;   // Flame Sensor DO (active LOW)
const int PIN_TEMP_ANALOG  = A1;   // Analog Temperature Sensor AO
const int PIN_SOUND_ANALOG = A2;   // High Sensitivity Sound Sensor AO
const int PIN_SOUND_DIGITAL = 2;   // Sound Sensor DO (interrupt capable on Uno)

// Actuators
const int PIN_RGB_RED      = 9;    // RGB LED Red (PWM)
const int PIN_RGB_GREEN    = 10;   // RGB LED Green (PWM)
const int PIN_RGB_BLUE     = 11;   // RGB LED Blue (PWM)
const int PIN_BUZZER       = 8;    // Active Buzzer
const int PIN_RELAY        = 7;    // 5V Relay (Active LOW on most kit relays)

// ===================== SYSTEM STATES & CONSTANTS =================
enum SystemState {
  STATE_NORMAL,    // All levels within ambient baseline
  STATE_ADVISORY,  // Single parameter elevated; monitoring closely
  STATE_WARNING,   // Multiple moderate anomalies detected
  STATE_EMERGENCY  // Confirmed fire/explosion hazard; relay cut-off & siren
};

SystemState currentState = STATE_NORMAL;

// Calibration & Baselines
float baselineFlame = 1023.0;
float baselineTemp  = 25.0;
float baselineSound = 512.0;

// Rolling Temperature Buffer for Derivative Calculation (dT/dt)
const int TEMP_HISTORY_SIZE = 5;
float tempHistory[TEMP_HISTORY_SIZE];
int tempHistoryIdx = 0;
unsigned long lastTempSampleTime = 0;
const unsigned long TEMP_SAMPLE_INTERVAL = 200; // ms

// Risk Weights (Sum = 1.0)
const float WEIGHT_FLAME = 0.50;  // Optical flame detection weight
const float WEIGHT_TEMP_RATE = 0.35; // Thermal rise rate weight
const float WEIGHT_SOUND = 0.15;  // Acoustic anomaly weight

// Sound Event Counter
volatile unsigned long soundTriggerCount = 0;
volatile unsigned long lastSoundInterruptTime = 0;

// Non-blocking Timers
unsigned long lastTelemetryTime = 0;
const unsigned long TELEMETRY_INTERVAL = 500; // ms

unsigned long lastBuzzerToggleTime = 0;
bool buzzerState = false;

// ======================== INTERRUPT SERVICE ======================
void onSoundDetected() {
  unsigned long now = millis();
  if (now - lastSoundInterruptTime > 50) { // Debounce 50ms
    soundTriggerCount++;
    lastSoundInterruptTime = now;
  }
}

// ===================== HELPER FUNCTIONS ==========================
// Convert NTC thermistor ADC reading to degrees Celsius (Steinhart-Hart approximation)
float readTemperatureC(int pin) {
  int rawADC = analogRead(pin);
  if (rawADC <= 0) rawADC = 1;
  if (rawADC >= 1023) rawADC = 1022;

  // Assuming standard 10k NTC thermistor with 10k pull-up/down
  float resistance = 10000.0 * (1023.0 / (float)rawADC - 1.0);
  float steinhart;
  steinhart = resistance / 10000.0;     // (R/Ro)
  steinhart = log(steinhart);           // ln(R/Ro)
  steinhart /= 3950.0;                  // 1/B * ln(R/Ro)
  steinhart += 1.0 / (25.0 + 273.15);   // + (1/To)
  steinhart = 1.0 / steinhart;          // Invert
  steinhart -= 273.15;                  // Convert to Celsius

  // Sanity check
  if (steinhart < -40.0 || steinhart > 125.0) {
    // Fallback linear scaling for simple analog temp modules (e.g. KY-013 / LM35)
    return (rawADC * 5.0 / 1024.0) * 100.0;
  }
  return steinhart;
}

// Calculate rate of temperature change (deg C per second)
float calculateTemperatureRate(float currentTemp) {
  unsigned long now = millis();
  if (now - lastTempSampleTime >= TEMP_SAMPLE_INTERVAL) {
    lastTempSampleTime = now;
    tempHistory[tempHistoryIdx] = currentTemp;
    tempHistoryIdx = (tempHistoryIdx + 1) % TEMP_HISTORY_SIZE;
  }

  // Calculate delta over buffer
  int oldestIdx = tempHistoryIdx; // Oldest entry is current write position
  float oldestTemp = tempHistory[oldestIdx];
  float dtSeconds = (TEMP_HISTORY_SIZE * TEMP_SAMPLE_INTERVAL) / 1000.0;
  float rate = (currentTemp - oldestTemp) / dtSeconds;
  return (rate > 0) ? rate : 0.0; // We only care about positive heating rate
}

// Set RGB LED Color (Common Cathode: 255=ON, 0=OFF)
void setRgbColor(byte r, byte g, byte b) {
  analogWrite(PIN_RGB_RED, r);
  analogWrite(PIN_RGB_GREEN, g);
  analogWrite(PIN_RGB_BLUE, b);
}

// ======================== SETUP ==================================
void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000); // Allow USB initialization

  Serial.println(F("\n======================================================="));
  Serial.println(F("  PROJECT 1: MULTI-SENSOR ENVIRONMENTAL HAZARD GUARDIAN"));
  Serial.println(F("======================================================="));

  // Configure Pins
  pinMode(PIN_FLAME_DIGITAL, INPUT);
  pinMode(PIN_SOUND_DIGITAL, INPUT);
  pinMode(PIN_RGB_RED, OUTPUT);
  pinMode(PIN_RGB_GREEN, OUTPUT);
  pinMode(PIN_RGB_BLUE, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_RELAY, OUTPUT);

  // Initial Safe States
  digitalWrite(PIN_BUZZER, LOW);
  digitalWrite(PIN_RELAY, HIGH); // Relay OFF (standard active LOW kit relay)
  setRgbColor(0, 255, 0);        // Green = System Initializing

  // Attach Hardware Interrupt for high-speed acoustic spike detection
  attachInterrupt(digitalPinToInterrupt(PIN_SOUND_DIGITAL), onSoundDetected, FALLING);

  // Baseline Calibration (sample 10 readings over 500ms)
  Serial.print(F("Calibrating ambient baselines... "));
  float flameSum = 0;
  float tempSum = 0;
  float soundSum = 0;
  for (int i = 0; i < 10; i++) {
    flameSum += analogRead(PIN_FLAME_ANALOG);
    tempSum  += readTemperatureC(PIN_TEMP_ANALOG);
    soundSum += analogRead(PIN_SOUND_ANALOG);
    delay(50);
  }
  baselineFlame = flameSum / 10.0;
  baselineTemp  = tempSum / 10.0;
  baselineSound = soundSum / 10.0;

  for (int i = 0; i < TEMP_HISTORY_SIZE; i++) {
    tempHistory[i] = baselineTemp;
  }

  Serial.println(F("DONE"));
  Serial.print(F("  -> Ambient Flame Raw ADC: ")); Serial.println(baselineFlame);
  Serial.print(F("  -> Ambient Temp:          ")); Serial.print(baselineTemp); Serial.println(F(" °C"));
  Serial.print(F("  -> Ambient Sound Raw ADC: ")); Serial.println(baselineSound);
  Serial.println(F("Guardian online. Monitoring sensor fusion matrix...\n"));
}

// ======================== MAIN LOOP ==============================
void loop() {
  unsigned long currentMillis = millis();

  // 1. Read Raw Sensors
  int rawFlame = analogRead(PIN_FLAME_ANALOG);
  bool flameDigitalActive = (digitalRead(PIN_FLAME_DIGITAL) == LOW);
  float currentTemp = readTemperatureC(PIN_TEMP_ANALOG);
  float tempRate = calculateTemperatureRate(currentTemp); // °C / sec
  int rawSound = analogRead(PIN_SOUND_ANALOG);
  int soundDeviation = abs(rawSound - (int)baselineSound);

  // 2. Individual Risk Sub-scores Normalized (0.0 to 1.0)
  // Flame: ADC drops when flame is detected (1023 = dark, < 100 = fire close)
  float flameScore = 0.0;
  if (rawFlame < baselineFlame) {
    flameScore = (baselineFlame - (float)rawFlame) / baselineFlame;
    if (flameScore > 1.0) flameScore = 1.0;
  }
  if (flameDigitalActive && flameScore < 0.6) flameScore = 0.6; // Digital trigger guarantees min 60%

  // Thermal Rise Rate: > 0.5 °C/sec is suspicious, > 2.0 °C/sec is critical
  float tempRateScore = (tempRate > 0) ? (tempRate / 2.0) : 0.0;
  if (tempRateScore > 1.0) tempRateScore = 1.0;

  // Sound Anomaly Score
  float soundScore = (float)soundDeviation / 350.0;
  if (soundTriggerCount > 0) {
    soundScore += (soundTriggerCount * 0.2);
    soundTriggerCount = 0; // Reset after consuming
  }
  if (soundScore > 1.0) soundScore = 1.0;

  // 3. Sensor Fusion Algorithm: Weighted Multi-Factor Score
  float compositeRisk = (flameScore * WEIGHT_FLAME) + 
                        (tempRateScore * WEIGHT_TEMP_RATE) + 
                        (soundScore * WEIGHT_SOUND);
  
  // Fast-track Rule: If flame optical signature is extremely strong (>0.85) AND temp is rising, jump to 1.0
  if (flameScore > 0.85 && tempRate > 0.2) {
    compositeRisk = max(compositeRisk, 0.95);
  }

  // 4. State Machine Evaluation
  if (compositeRisk < 0.25) {
    currentState = STATE_NORMAL;
  } else if (compositeRisk < 0.55) {
    currentState = STATE_ADVISORY;
  } else if (compositeRisk < 0.80) {
    currentState = STATE_WARNING;
  } else {
    currentState = STATE_EMERGENCY;
  }

  // 5. Actuator Management based on State
  switch (currentState) {
    case STATE_NORMAL:
      setRgbColor(0, 255, 0);        // Solid Green
      digitalWrite(PIN_BUZZER, LOW);
      digitalWrite(PIN_RELAY, HIGH); // Safe: Relay idle
      break;

    case STATE_ADVISORY:
      setRgbColor(0, 180, 255);      // Cyan / Cool Blue
      digitalWrite(PIN_BUZZER, LOW);
      digitalWrite(PIN_RELAY, HIGH);
      break;

    case STATE_WARNING:
      setRgbColor(255, 140, 0);      // Amber / Orange
      digitalWrite(PIN_RELAY, HIGH); // Keep monitoring
      // Intermittent soft beep every 500ms
      if (currentMillis - lastBuzzerToggleTime > 500) {
        lastBuzzerToggleTime = currentMillis;
        buzzerState = !buzzerState;
        digitalWrite(PIN_BUZZER, buzzerState ? HIGH : LOW);
      }
      break;

    case STATE_EMERGENCY:
      // Rapid Flashing Red & Pulsed Loud Siren
      if (currentMillis - lastBuzzerToggleTime > 120) {
        lastBuzzerToggleTime = currentMillis;
        buzzerState = !buzzerState;
        digitalWrite(PIN_BUZZER, buzzerState ? HIGH : LOW);
        if (buzzerState) {
          setRgbColor(255, 0, 0);    // Bright Red
        } else {
          setRgbColor(0, 0, 0);      // Off (Strobe effect)
        }
      }
      // Trigger Relay to cut power / engage emergency ventilation
      digitalWrite(PIN_RELAY, LOW); // Active
      break;
  }

  // 6. Serial Telemetry Broadcast (Every 500ms)
  if (currentMillis - lastTelemetryTime >= TELEMETRY_INTERVAL) {
    lastTelemetryTime = currentMillis;
    Serial.print(F("[TELEMETRY] Flame: "));
    Serial.print(rawFlame);
    Serial.print(F(" (")); Serial.print((int)(flameScore * 100)); Serial.print(F("%) | "));
    Serial.print(F("Temp: "));
    Serial.print(currentTemp, 1);
    Serial.print(F("C (Rate: ")); Serial.print(tempRate, 2); Serial.print(F("C/s) | "));
    Serial.print(F("Sound: "));
    Serial.print(rawSound);
    Serial.print(F(" | Risk: "));
    Serial.print((int)(compositeRisk * 100));
    Serial.print(F("% -> STATE: "));

    switch (currentState) {
      case STATE_NORMAL:    Serial.println(F("NORMAL [OK]")); break;
      case STATE_ADVISORY:  Serial.println(F("ADVISORY [CHECK]")); break;
      case STATE_WARNING:   Serial.println(F("WARNING [CAUTION]")); break;
      case STATE_EMERGENCY: Serial.println(F("CRITICAL EMERGENCY! [EVACUATE]")); break;
    }
  }
}
