/*
 * Project 4: Multi-Layer Security Sentinel & Inertial Anti-Tamper System
 * ----------------------------------------------------------------------
 * Target Board: Arduino Uno (ATmega328P)
 * Kit: Hosyond 45-in-1 Sensor Kit
 * 
 * Fused Sensors:
 *  - Module #40: GY-521 (MPU-6050 I2C: A4 SDA, A5 SCL) - 6-DOF Accelerometer & Gyroscope
 *  - Module #16: Shock / Vibration Sensor (Digital D2, Interrupt 0) - Impact Detection
 *  - Module #33: Metal Touch Sensor (Digital D3, Interrupt 1) - Unauthorized Contact
 *  - Module #21: Tilt Switch (Digital D4) - Discrete Gravitational Tip Sensor (LOW = Tilted)
 * 
 * Actuators & Indicators:
 *  - Module #01: RGB LED (Pins D11 Red, D10 Blue, D9 Green) - Status Beacon
 *  - Module #11: Active Buzzer (Digital D8) - Intrusion Siren
 *  - Module #30: 5V Relay Module (Digital D7) - Lock Solenoid / Security Strobe
 * 
 * Sensor Fusion Matrix:
 *  1. Touch Layer: Physical hand contact with chassis (Module #33).
 *  2. Mechanical Shock Layer: High-frequency micro-impact pulses (Module #16).
 *  3. Angular Tilt Layer: Structural tilt via kit switch (Module #21, LOW when tilted).
 *  4. Inertial Vector Layer: 3D acceleration vector delta via MPU-6050.
 *  
 *  Multi-Threat Evaluation with State Latching:
 *   - SEC_ARMED_NORMAL: Green LED, Siren silent, Relay locked.
 *   - SEC_TAMPER_WARNING: Amber LED, 150ms chirp, latched for 3s calm before auto-return.
 *   - SEC_ALARM_CRITICAL: Strobe Red/Blue, Siren pulse, Relay tripped for 8 seconds.
 */

#include <Wire.h>

// ======================== PIN ASSIGNMENTS ========================
const int PIN_SHOCK_SENSOR = 2;    // Shock Sensor DO (Hardware INT0)
const int PIN_TOUCH_SENSOR = 3;    // Metal Touch DO (Hardware INT1)
const int PIN_TILT_SWITCH  = 4;    // Tilt Switch (Kit #21: LOW = Tilted)

// Actuators
const int PIN_RELAY_LOCK   = 7;    // 5V Relay (Active LOW)
const int PIN_ALARM_BUZZER = 8;    // Active Buzzer
const int PIN_RGB_RED      = 11;   // PWM
const int PIN_RGB_BLUE     = 10;   // PWM
const int PIN_RGB_GREEN    = 9;    // PWM

// MPU-6050 I2C Address
const int MPU_ADDR = 0x68;

// ======================== SYSTEM STATES ==========================
enum SecurityState {
  SEC_ARMED_NORMAL,    // Fully armed, monitoring perimeter (Green LED)
  SEC_TAMPER_WARNING,  // Minor touch or vibration (Amber LED, short chirp)
  SEC_ALARM_CRITICAL   // Violent shock / confirmed tilt breach (Strobe Red/Blue + Siren)
};

SecurityState systemState = SEC_ARMED_NORMAL;

// Interrupt Flags
volatile bool shockDetected = false;
volatile unsigned long lastShockInterruptTime = 0;

volatile bool touchDetected = false;
volatile unsigned long lastTouchInterruptTime = 0;

// Baseline Inertial Reference
int16_t baseAx = 0, baseAy = 0, baseAz = 0;
bool mpuAvailable = false;

// Non-blocking Timing
unsigned long lastSensorPollTime = 0;
const unsigned long POLL_INTERVAL = 50; // 20 Hz update rate

unsigned long lastTelemetryTime = 0;
unsigned long alarmStartTime = 0;
const unsigned long ALARM_HOLD_DURATION = 8000; // Siren holds for 8s after breach

unsigned long lastBuzzerToggleTime = 0;
bool buzzerToggle = false;

// ======================== INTERRUPTS =============================
void isrShock() {
  unsigned long now = millis();
  if (now - lastShockInterruptTime > 80) { // 80ms debounce
    shockDetected = true;
    lastShockInterruptTime = now;
  }
}

void isrTouch() {
  unsigned long now = millis();
  if (now - lastTouchInterruptTime > 150) { // 150ms debounce
    touchDetected = true;
    lastTouchInterruptTime = now;
  }
}

// Sequenced 16-bit register reader helper
static inline int16_t readWire16() {
  uint8_t h = Wire.read();
  uint8_t l = Wire.read();
  return ((int16_t)h << 8) | l;
}

// ======================== MPU-6050 DRIVER ========================
void initMPU6050() {
  Wire.begin();
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B); // PWR_MGMT_1 register
  Wire.write(0);    // Wake up MPU-6050
  byte error = Wire.endTransmission();

  if (error == 0) {
    mpuAvailable = true;
    Serial.println(F(">> MPU-6050 6-DOF Inertial Sensor Initialized Successfully."));
    
    // Calibrate neutral gravity vector (10 samples)
    long axSum = 0, aySum = 0, azSum = 0;
    for (int i = 0; i < 10; i++) {
      Wire.beginTransmission(MPU_ADDR);
      Wire.write(0x3B);
      Wire.endTransmission(false);
      Wire.requestFrom(MPU_ADDR, 6, true);
      if (Wire.available() >= 6) {
        axSum += readWire16();
        aySum += readWire16();
        azSum += readWire16();
      }
      delay(20);
    }
    baseAx = axSum / 10;
    baseAy = aySum / 10;
    baseAz = azSum / 10;
  } else {
    mpuAvailable = false;
    Serial.println(F(">> WARNING: MPU-6050 Not Found on I2C bus (A4/A5). Using discrete sensors."));
  }
}

// Read raw acceleration delta from baseline
float getInertialJerk() {
  if (!mpuAvailable) return 0.0;

  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);
  Wire.endTransmission(false);
  Wire.requestFrom(MPU_ADDR, 6, true);

  if (Wire.available() < 6) return 0.0;

  int16_t ax = readWire16();
  int16_t ay = readWire16();
  int16_t az = readWire16();

  // Compute vector magnitude delta normalized to G (16384 LSB = 1g in +/-2g range)
  float dAx = (ax - baseAx) / 16384.0;
  float dAy = (ay - baseAy) / 16384.0;
  float dAz = (az - baseAz) / 16384.0;

  return sqrt(dAx * dAx + dAy * dAy + dAz * dAz);
}

// Set RGB LED Color
void setRgbColor(byte r, byte g, byte b) {
  analogWrite(PIN_RGB_RED, r);
  analogWrite(PIN_RGB_GREEN, g);
  analogWrite(PIN_RGB_BLUE, b);
}

// ======================== SETUP ==================================
void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000);

  Serial.println(F("\n======================================================="));
  Serial.println(F("  PROJECT 4: MULTI-LAYER SECURITY SENTINEL & IMU FUSION"));
  Serial.println(F("======================================================="));

  pinMode(PIN_SHOCK_SENSOR, INPUT);
  pinMode(PIN_TOUCH_SENSOR, INPUT);
  pinMode(PIN_TILT_SWITCH, INPUT_PULLUP);

  pinMode(PIN_RELAY_LOCK, OUTPUT);
  pinMode(PIN_ALARM_BUZZER, OUTPUT);
  pinMode(PIN_RGB_RED, OUTPUT);
  pinMode(PIN_RGB_GREEN, OUTPUT);
  pinMode(PIN_RGB_BLUE, OUTPUT);

  // Safe Armed Initial State
  digitalWrite(PIN_RELAY_LOCK, HIGH); // Armed (Relay inactive)
  digitalWrite(PIN_ALARM_BUZZER, LOW);
  setRgbColor(0, 255, 0); // Solid Green

  // Attach Hardware Interrupts for microsecond response
  attachInterrupt(digitalPinToInterrupt(PIN_SHOCK_SENSOR), isrShock, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_TOUCH_SENSOR), isrTouch, RISING);

  // Initialize I2C IMU
  initMPU6050();

  Serial.println(F("Security Sentinel ARMED. Monitoring 4 intrusion layers...\n"));
}

// ======================== MAIN LOOP ==============================
void loop() {
  unsigned long currentMillis = millis();

  // Periodic Sensor Evaluation (20 Hz)
  if (currentMillis - lastSensorPollTime >= POLL_INTERVAL) {
    lastSensorPollTime = currentMillis;

    // 1. Read Discrete Tilt Switch (Kit #21: LOW when tilted, HIGH when upright)
    bool isTilted = (digitalRead(PIN_TILT_SWITCH) == LOW);

    // 2. Read IMU Acceleration Jerk
    float imuJerk = getInertialJerk();

    // 3. Sensor Fusion Threat Matrix
    bool isViolentImpact = shockDetected || (imuJerk > 0.65);
    bool isSevereTilt    = isTilted || (imuJerk > 0.35);
    bool isTouchDetected = touchDetected;

    // Reset volatile flags after latching
    shockDetected = false;
    touchDetected = false;

    // 4. State Machine Progression with proper latching
    if (systemState == SEC_ARMED_NORMAL) {
      if (isViolentImpact || (isSevereTilt && isTouchDetected)) {
        systemState = SEC_ALARM_CRITICAL;
        alarmStartTime = currentMillis;
        Serial.println(F(">> [SECURITY BREACH] Confirmed Violent Impact / Forced Tampering!"));
      } else if (isTouchDetected || isSevereTilt) {
        systemState = SEC_TAMPER_WARNING;
        alarmStartTime = currentMillis;
        Serial.println(F(">> [TAMPER WARNING] Surface touch or angular shift detected."));
      }
    } else if (systemState == SEC_TAMPER_WARNING) {
      // While in warning, escalate if impact or combined touch+tilt occurs
      if (isViolentImpact || (isSevereTilt && isTouchDetected)) {
        systemState = SEC_ALARM_CRITICAL;
        alarmStartTime = currentMillis;
        Serial.println(F(">> [SECURITY BREACH] Tamper escalated to Critical Alarm!"));
      } else if (currentMillis - alarmStartTime > 3000) {
        // Auto-return after 3 seconds of calm
        systemState = SEC_ARMED_NORMAL;
        Serial.println(F(">> Tamper warning cleared. Returning to Armed Normal."));
      }
    } else if (systemState == SEC_ALARM_CRITICAL) {
      if (currentMillis - alarmStartTime > ALARM_HOLD_DURATION) {
        systemState = SEC_ARMED_NORMAL;
        Serial.println(F(">> Alarm Timed Out. Re-arming Sentinel..."));
      }
    }

    // 5. Diagnostics Telemetry (Every 300ms)
    if (currentMillis - lastTelemetryTime >= 300) {
      lastTelemetryTime = currentMillis;
      Serial.print(F("[SENTINEL] Tilt: "));
      Serial.print(isTilted ? F("TILTED") : F("LEVEL"));
      Serial.print(F(" | IMU Jerk: "));
      Serial.print(imuJerk, 2);
      Serial.print(F("G | Shock: "));
      Serial.print(isViolentImpact ? F("HIT!") : F("NONE"));
      Serial.print(F(" | Touch: "));
      Serial.print(isTouchDetected ? F("TOUCH") : F("CLEAR"));
      Serial.print(F(" | Status: "));

      switch (systemState) {
        case SEC_ARMED_NORMAL:   Serial.println(F("ARMED [Secure]")); break;
        case SEC_TAMPER_WARNING: Serial.println(F("WARNING [Tampering Suspected]")); break;
        case SEC_ALARM_CRITICAL: Serial.println(F("CRITICAL INTRUSION BREACH!!")); break;
      }
    }
  }

  // 6. Actuator State Management
  switch (systemState) {
    case SEC_ARMED_NORMAL:
      setRgbColor(0, 255, 0);              // Green = Secure
      digitalWrite(PIN_ALARM_BUZZER, LOW); // Silent
      digitalWrite(PIN_RELAY_LOCK, HIGH);  // Lock secure
      break;

    case SEC_TAMPER_WARNING:
      setRgbColor(255, 140, 0);            // Amber Warning
      digitalWrite(PIN_RELAY_LOCK, HIGH);  // Hold lock
      // Single 150ms chirp on entry
      if (currentMillis - alarmStartTime < 150) {
        digitalWrite(PIN_ALARM_BUZZER, HIGH);
      } else {
        digitalWrite(PIN_ALARM_BUZZER, LOW);
      }
      break;

    case SEC_ALARM_CRITICAL:
      digitalWrite(PIN_RELAY_LOCK, LOW); // Active emergency trip

      // Pulsing strobe siren
      if (currentMillis - lastBuzzerToggleTime > 100) {
        lastBuzzerToggleTime = currentMillis;
        buzzerToggle = !buzzerToggle;
        digitalWrite(PIN_ALARM_BUZZER, buzzerToggle ? HIGH : LOW);
        if (buzzerToggle) {
          setRgbColor(255, 0, 0); // Strobe Red
        } else {
          setRgbColor(0, 0, 255); // Strobe Blue
        }
      }
      break;
  }
}
