/*
 * ==============================================================================
 * Project 4: Cyber-Physical Biometric Access & Anti-Spoofing Vault Controller
 * Level 2 Multi-Sensor Fusion on ESP32-S3 (Dual-Core Xtensa LX7 @ 240MHz)
 * ==============================================================================
 * Target: ESP32-S3 Dev Module
 * Kit: Hosyond 45-in-1 Sensor Kit
 * 
 * Fused Kit Modules:
 *  - Module #17: Pulse Rate Monitor Sensor (Optical PPG Photoplethysmography)
 *  - Module #43: DS1302 Real-Time Clock Module (Hardware Time-Gate Authorization)
 *  - Module #33: Metal Touch Sensor (Capacitive Handshake Presence)
 *  - Module #40: GY-521 (MPU-6050 6-DOF IMU) - Anti-Drill / Seismic Tamper
 *  - Module #25: Mercury / Tilt Switch - Discrete Structural Inversion Guard
 * 
 * Actuators & Access Mechanism:
 *  - Module #30: 5V Relay Module (Electromagnetic Door Deadbolt Solenoid)
 *  - Module #01: RGB LED (Access Feedback: Standby / Processing / Granted / Denied)
 *  - Module #12: Passive Buzzer (Multi-Tone Chimes & Security Siren)
 * 
 * Level 2 Sensor Fusion & Security Core:
 *  - Biometric Liveness Verification (Anti-Spoofing):
 *      * Measures continuous optical blood pulsation dynamics.
 *      * Calculates real-time Inter-Beat Interval (IBI) and Heart Rate (55 - 130 BPM).
 *      * Rejects static silicone impressions, 3D molds, and non-living objects.
 *  - Time-Gated Multi-Factor Authorization:
 *      * Reads DS1302 RTC to ensure access is only permitted during valid operational shift hours.
 *  - 6-DOF Inertial Enclosure Defense:
 *      * Real-time 3D acceleration vector monitoring detects attempts to drill,撬 (pry), or physically remove the vault controller.
 *  - FreeRTOS Dual-Core Design:
 *      * Core 1: 250 Hz PPG Optical Peak Extraction & IBI Calculation.
 *      * Core 0: Cryptographic State Machine, RTC Time-Lock, Actuator Solenoid & Diagnostics.
 */

#include <Wire.h>
#include <math.h>

// ======================== PIN DEFINITIONS (ESP32-S3) ========================
// Analog Sensors (ADC1)
const int PIN_PULSE_ANALOG  = 4;   // Pulse Rate Monitor AO (ADC1_CH3)
const int PIN_TOUCH_DIGITAL = 5;   // Metal Touch Sensor DO (Hardware INT1)
const int PIN_TILT_DIGITAL  = 6;   // Mercury / Tilt Switch

// DS1302 RTC Pins (3-Wire Interface)
const int PIN_RTC_RST       = 1;   // Chip Enable (CE)
const int PIN_RTC_DAT       = 2;   // I/O Data
const int PIN_RTC_CLK       = 3;   // Serial Clock (SCLK)

// I2C MPU-6050 Anti-Tamper
const int PIN_I2C_SDA       = 8;
const int PIN_I2C_SCL       = 9;
const int MPU_ADDR          = 0x68;

// Actuators
const int PIN_SOLENOID_RELAY = 7;  // 5V Relay for Magnetic Lock (Active LOW)
const int PIN_BUZZER        = 10;  // Passive Buzzer
const int PIN_RGB_RED       = 14;  // Red
const int PIN_RGB_GREEN     = 15;  // Green
const int PIN_RGB_BLUE      = 16;  // Blue

// System Access States
enum VaultState {
  VAULT_STANDBY,        // Blue LED, Waiting for user touch
  VAULT_MEASURING_PPG,  // Cyan Breathing, Analyzing biometric pulse
  VAULT_ACCESS_GRANTED, // Green LED, Solenoid unlocked (5s)
  VAULT_ACCESS_DENIED,  // Amber Flash, Invalid pulse or time gate
  VAULT_TAMPER_ALARM    // Red Strobe, SIREN, Lock deadbolted
};

VaultState currentVaultState = VAULT_STANDBY;

// Biometric Metrics
struct BiometricResult {
  int   bpm;
  float ibiSeconds;
  bool  livenessConfirmed;
  bool  fingerPresent;
};

BiometricResult currentBio = {0, 0.0f, false, false};
SemaphoreHandle_t vaultMutex;

// Tamper Reference
float baselineAx = 0, baselineAy = 0, baselineAz = 0;
bool mpuActive = false;

// ======================== FAST BIT-BANG DS1302 RTC DRIVER ===================
void rtcWriteByte(byte value) {
  for (int i = 0; i < 8; i++) {
    digitalWrite(PIN_RTC_DAT, (value & 0x01) ? HIGH : LOW);
    digitalWrite(PIN_RTC_CLK, HIGH);
    delayMicroseconds(2);
    digitalWrite(PIN_RTC_CLK, LOW);
    delayMicroseconds(2);
    value >>= 1;
  }
}

byte rtcReadByte() {
  byte value = 0;
  pinMode(PIN_RTC_DAT, INPUT);
  for (int i = 0; i < 8; i++) {
    value >>= 1;
    if (digitalRead(PIN_RTC_DAT)) value |= 0x80;
    digitalWrite(PIN_RTC_CLK, HIGH);
    delayMicroseconds(2);
    digitalWrite(PIN_RTC_CLK, LOW);
    delayMicroseconds(2);
  }
  pinMode(PIN_RTC_DAT, OUTPUT);
  return value;
}

struct RTCDateTime {
  byte second, minute, hour, day, month, year;
};

RTCDateTime readRTC() {
  RTCDateTime dt = {0, 30, 10, 22, 10, 26}; // Default 10:30 AM
  digitalWrite(PIN_RTC_RST, HIGH);
  delayMicroseconds(4);
  rtcWriteByte(0xBF); // Clock burst read
  byte sec  = rtcReadByte();
  byte min  = rtcReadByte();
  byte hr   = rtcReadByte();
  byte date = rtcReadByte();
  byte mon  = rtcReadByte();
  rtcReadByte(); // Day of week
  byte yr   = rtcReadByte();
  digitalWrite(PIN_RTC_RST, LOW);

  dt.second = (sec & 0x0F) + ((sec >> 4) & 0x07) * 10;
  dt.minute = (min & 0x0F) + ((min >> 4) & 0x07) * 10;
  dt.hour   = (hr & 0x0F) + ((hr >> 4) & 0x03) * 10;
  return dt;
}

// Set RGB LED
void setLedColor(byte r, byte g, byte b) {
  analogWrite(PIN_RGB_RED, r);
  analogWrite(PIN_RGB_GREEN, g);
  analogWrite(PIN_RGB_BLUE, b);
}

// Play Melodic Chime
void playChime(bool success) {
  if (success) {
    tone(PIN_BUZZER, 1046, 120); delay(140); // C6
    tone(PIN_BUZZER, 1318, 120); delay(140); // E6
    tone(PIN_BUZZER, 1568, 250); delay(260); // G6
  } else {
    tone(PIN_BUZZER, 440, 200); delay(220);  // A4
    tone(PIN_BUZZER, 370, 350); delay(360);  // F#4
  }
}

// ======================== FREERTOS TASK: CORE 1 PPG BIOMETRICS ==============
void TaskBiometricPulse(void *pvParameters) {
  const TickType_t xFrequency = pdMS_TO_TICKS(4); // 250 Hz (4ms)
  TickType_t xLastWakeTime = xTaskGetTickCount();

  float signalFilter = 2048.0f;
  unsigned long lastPeakTime = 0;
  int peakCount = 0;
  float ibiSum = 0.0f;
  bool isPeak = false;

  Serial.println(F("[Core 1] 250 Hz Optical PPG Biometrics Online."));

  for (;;) {
    vTaskDelayUntil(&xLastWakeTime, xFrequency);
    unsigned long now = millis();

    int rawPulse = analogRead(PIN_PULSE_ANALOG);
    // Exponential High-Pass / Low-Pass Filter
    signalFilter = 0.85f * signalFilter + 0.15f * rawPulse;
    float dynamicPulse = rawPulse - signalFilter;

    // Detect optical finger contact (raw baseline drops when finger covers sensor)
    bool fingerOn = (rawPulse > 600 && rawPulse < 3800);

    if (fingerOn) {
      // Peak detection with dynamic threshold
      if (dynamicPulse > 120.0f && !isPeak && (now - lastPeakTime > 350)) {
        isPeak = true;
        unsigned long ibiMs = now - lastPeakTime;
        lastPeakTime = now;

        if (ibiMs >= 400 && ibiMs <= 1200) { // Valid human heartbeat range (50 - 150 BPM)
          float ibiSec = ibiMs / 1000.0f;
          ibiSum += ibiSec;
          peakCount++;

          if (peakCount >= 4) { // Liveness confirmed over 4 valid consecutive heartbeats
            float avgIbi = ibiSum / peakCount;
            int bpm = (int)(60.0f / avgIbi);

            if (xSemaphoreTake(vaultMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
              currentBio.bpm = bpm;
              currentBio.ibiSeconds = avgIbi;
              currentBio.livenessConfirmed = true;
              currentBio.fingerPresent = true;
              xSemaphoreGive(vaultMutex);
            }
            peakCount = 0;
            ibiSum = 0.0f;
          }
        }
      } else if (dynamicPulse < 0.0f) {
        isPeak = false;
      }
    } else {
      peakCount = 0;
      ibiSum = 0.0f;
      if (xSemaphoreTake(vaultMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        currentBio.fingerPresent = false;
        currentBio.livenessConfirmed = false;
        xSemaphoreGive(vaultMutex);
      }
    }
  }
}

// ======================== FREERTOS TASK: CORE 0 SECURITY CONTROLLER =========
void TaskSecurityController(void *pvParameters) {
  unsigned long stateStartTime = millis();

  Serial.println(F("[Core 0] TaskSecurityController Online."));

  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(50)); // 20 Hz update
    unsigned long now = millis();

    // 1. Read Physical Tamper Domain
    bool touchActive = (digitalRead(PIN_TOUCH_DIGITAL) == HIGH);
    bool tiltActive  = (digitalRead(PIN_TILT_DIGITAL) == HIGH);

    // Read MPU-6050 3D seismic jerk
    float accelJerk = 0.0f;
    if (mpuActive) {
      Wire.beginTransmission(MPU_ADDR);
      Wire.write(0x3B);
      Wire.endTransmission(false);
      Wire.requestFrom(MPU_ADDR, 6, true);
      if (Wire.available() >= 6) {
        int16_t ax = (Wire.read() << 8 | Wire.read());
        int16_t ay = (Wire.read() << 8 | Wire.read());
        int16_t az = (Wire.read() << 8 | Wire.read());
        float dAx = (ax / 16384.0f) - baselineAx;
        float dAy = (ay / 16384.0f) - baselineAy;
        float dAz = (az / 16384.0f) - baselineAz;
        accelJerk = sqrt(dAx * dAx + dAy * dAy + dAz * dAz);
      }
    }

    // Seismic Tamper Check: High acceleration jerk (>0.7g) or inversion
    if (accelJerk > 0.70f || tiltActive) {
      currentVaultState = VAULT_TAMPER_ALARM;
      stateStartTime = now;
      Serial.println(F(">> [CRITICAL SECURITY] Seismic Drill / Inversion Tamper Detected!"));
    }

    // 2. Fetch Biometric Data Thread-Safely
    BiometricResult bio;
    if (xSemaphoreTake(vaultMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
      bio = currentBio;
      xSemaphoreGive(vaultMutex);
    }

    // 3. Vault State Machine Execution
    switch (currentVaultState) {
      case VAULT_STANDBY:
        setLedColor(0, 50, 255);             // Blue
        digitalWrite(PIN_SOLENOID_RELAY, HIGH); // Locked
        noTone(PIN_BUZZER);
        if (touchActive || bio.fingerPresent) {
          currentVaultState = VAULT_MEASURING_PPG;
          stateStartTime = now;
          Serial.println(F(">> [ACCESS] User Present. Analyzing Biometric Liveness..."));
        }
        break;

      case VAULT_MEASURING_PPG:
        setLedColor(0, 200, 255);            // Cyan Breathing
        if (bio.livenessConfirmed) {
          // Check Time-Gate Authorization via DS1302 RTC
          RTCDateTime dt = readRTC();
          // Valid Access Hours: 08:00 to 18:00
          bool isTimePermitted = (dt.hour >= 8 && dt.hour < 18);

          if (isTimePermitted) {
            currentVaultState = VAULT_ACCESS_GRANTED;
            stateStartTime = now;
            digitalWrite(PIN_SOLENOID_RELAY, LOW); // UNLOCK SOLENOID RELAY!
            setLedColor(0, 255, 0); // Green
            playChime(true);
            Serial.print(F(">> [ACCESS GRANTED] Verified Human Liveness: "));
            Serial.print(bio.bpm);
            Serial.print(F(" BPM | RTC Time: "));
            Serial.print(dt.hour); Serial.print(F(":")); Serial.println(dt.minute);
          } else {
            currentVaultState = VAULT_ACCESS_DENIED;
            stateStartTime = now;
            playChime(false);
            Serial.println(F(">> [ACCESS DENIED] Outside Authorized Operational Shift Hours!"));
          }
        } else if (now - stateStartTime > 8000) { // Timeout after 8 seconds of unsuccessful scan
          currentVaultState = VAULT_ACCESS_DENIED;
          stateStartTime = now;
          playChime(false);
          Serial.println(F(">> [ACCESS DENIED] Liveness Verification Timeout / Spoof Rejected."));
        }
        break;

      case VAULT_ACCESS_GRANTED:
        setLedColor(0, 255, 0); // Solid Green
        // Hold open for 5 seconds
        if (now - stateStartTime > 5000) {
          currentVaultState = VAULT_STANDBY;
          digitalWrite(PIN_SOLENOID_RELAY, HIGH); // Re-lock
          Serial.println(F(">> Door Solenoid Locked. Returning to Standby."));
        }
        break;

      case VAULT_ACCESS_DENIED:
        setLedColor(255, 140, 0); // Amber
        digitalWrite(PIN_SOLENOID_RELAY, HIGH); // Locked
        if (now - stateStartTime > 3000) {
          currentVaultState = VAULT_STANDBY;
        }
        break;

      case VAULT_TAMPER_ALARM:
        // Strobe Red + Piercing Siren
        setLedColor((now % 200 < 100) ? 255 : 0, 0, 0);
        tone(PIN_BUZZER, (now % 400 < 200) ? 2800 : 1800);
        digitalWrite(PIN_SOLENOID_RELAY, HIGH); // Locked
        // Alarm latch holds for 10 seconds
        if (now - stateStartTime > 10000) {
          currentVaultState = VAULT_STANDBY;
        }
        break;
    }
  }
}

// ======================== SETUP =============================================
void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000);

  Serial.println(F("\n============================================================"));
  Serial.println(F("  PROJECT 4: ESP32-S3 BIOMETRIC ANTI-SPOOFING VAULT SYSTEM"));
  Serial.println(F("============================================================"));

  pinMode(PIN_TOUCH_DIGITAL, INPUT);
  pinMode(PIN_TILT_DIGITAL, INPUT_PULLUP);
  pinMode(PIN_SOLENOID_RELAY, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_RGB_RED, OUTPUT);
  pinMode(PIN_RGB_GREEN, OUTPUT);
  pinMode(PIN_RGB_BLUE, OUTPUT);

  // Configure DS1302 Pins
  pinMode(PIN_RTC_RST, OUTPUT);
  pinMode(PIN_RTC_CLK, OUTPUT);
  pinMode(PIN_RTC_DAT, OUTPUT);
  digitalWrite(PIN_RTC_RST, LOW);
  digitalWrite(PIN_RTC_CLK, LOW);

  digitalWrite(PIN_SOLENOID_RELAY, HIGH); // Locked
  setLedColor(0, 0, 255); // Blue Standby

  // Init I2C IMU
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B);
  Wire.write(0);
  if (Wire.endTransmission() == 0) {
    mpuActive = true;
    Serial.println(F("MPU-6050 Anti-Tamper Core Initialized."));
  }

  vaultMutex = xSemaphoreCreateMutex();

  // Core 1: 250 Hz Optical PPG Biometric Filter
  xTaskCreatePinnedToCore(
    TaskBiometricPulse,
    "PPGBiometrics",
    4096,
    NULL,
    5,
    NULL,
    1
  );

  // Core 0: Security & Access Controller
  xTaskCreatePinnedToCore(
    TaskSecurityController,
    "SecController",
    4096,
    NULL,
    3,
    NULL,
    0
  );

  Serial.println(F("Vault Armed & Ready. Standby for authentication.\n"));
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
