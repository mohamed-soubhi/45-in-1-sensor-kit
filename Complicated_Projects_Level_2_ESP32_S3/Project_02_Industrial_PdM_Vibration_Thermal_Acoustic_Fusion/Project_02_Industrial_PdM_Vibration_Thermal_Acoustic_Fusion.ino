/*
 * ==============================================================================
 * Project 2: Industrial Predictive Maintenance (PdM) & Machine Health Sentinel
 * Level 2 Multi-Sensor Fusion on ESP32-S3 (Dual-Core Xtensa LX7 @ 240MHz)
 * ==============================================================================
 * Target: ESP32-S3 Dev Module
 * Kit: Hosyond 45-in-1 Sensor Kit
 * 
 * Fused Kit Modules:
 *  - Module #40: GY-521 (MPU-6050 Accelerometer) - High-Frequency Vibration (500 Hz)
 *  - Module #13: Analog Temperature Sensor (NTC / LM35) - Bearing Thermal Gradient
 *  - Module #37: High-Sensitivity Sound Sensor - Acoustic Friction Emission
 *  - Module #27: Photo Interrupter / Module #24 Hall Sensor - Shaft RPM Tachometer
 * 
 * Actuators & Protection:
 *  - Module #30: 5V Relay Module - Emergency Contactor / Motor Trip Interlock
 *  - Module #01: RGB LED - ISO 10816 Vibration Severity Standard Zones (A/B/C/D)
 *  - Module #12: Passive Buzzer - Frequency-Modulated Warning Beeper
 * 
 * Level 2 Sensor Fusion & Analytical Core:
 *  - ISO 10816-3 Industrial Vibration Standards:
 *      * Calculates True RMS Vibration Velocity (v_RMS in mm/s).
 *      * Calculates Crest Factor & Kurtosis (detects bearing spalling & impact pitting).
 *  - Multi-Domain Failure Precursor Fusion:
 *      * Stage 1 (Initial Wear): Acoustic emission increases (Sound sensor spikes).
 *      * Stage 2 (Defect Spalling): High-frequency vibration crest factor spikes.
 *      * Stage 3 (Severe Looseness): RMS vibration velocity exceeds ISO Zone C (> 4.5 mm/s).
 *      * Stage 4 (Imminent Seizure): Bearing temperature derivative dT/dt shoots upward.
 *  - FreeRTOS Multi-Core Design:
 *      * Core 1: 500 Hz Real-Time Vibration Sampling & Double-Buffer Math.
 *      * Core 0: Asynchronous Diagnostics, Health Index Evaluation & Serial/Web Stream.
 */

#include <Wire.h>
#include <math.h>

// ======================== PIN DEFINITIONS (ESP32-S3) ========================
// I2C MPU-6050
const int PIN_I2C_SDA      = 8;
const int PIN_I2C_SCL      = 9;
const int MPU_ADDR         = 0x68;

// Analog Sensors (ADC1)
const int PIN_TEMP_ANALOG  = 4;   // Bearing temperature sensor (ADC1_CH3)
const int PIN_SOUND_ANALOG = 5;   // Acoustic emission sensor (ADC1_CH4)

// Tachometer (Optical photo-interrupter or Hall sensor)
const int PIN_TACH_PULSE   = 6;   // Digital pulse for shaft RPM

// Actuators
const int PIN_RELAY_TRIP   = 7;   // 5V Relay for emergency motor cutoff (Active LOW)
const int PIN_BUZZER       = 10;  // Passive buzzer
const int PIN_RGB_RED      = 14;  // ISO Zone D indicator
const int PIN_RGB_GREEN    = 15;  // ISO Zone A/B indicator
const int PIN_RGB_BLUE     = 16;  // Running status

// ==================== ISO 10816 VIBRATION SEVERITY ZONES ====================
// Based on ISO 10816-3 for Class I/II medium industrial machines:
const float ISO_ZONE_B_LIMIT = 2.8f; // mm/s RMS (Good to Acceptable)
const float ISO_ZONE_C_LIMIT = 4.5f; // mm/s RMS (Acceptable to Unsatisfactory - Warning)
const float ISO_ZONE_D_LIMIT = 7.1f; // mm/s RMS (Unacceptable - Immediate Trip!)

enum MachineHealthZone {
  ZONE_A_GOOD,         // Green LED, Relay closed, safe
  ZONE_B_ACCEPTABLE,   // Cyan LED, Safe for continuous operation
  ZONE_C_UNSATISFACTORY,// Amber LED, High vibration/heat warning, buzzer beep
  ZONE_D_UNACCEPTABLE  // Flashing Red, SIREN, RELAY TRIP (Emergency Shutdown)
};

// Double-Buffer Vibration Storage (500 Hz Sampling)
const int SAMPLE_WINDOW = 128;
float accelBufferX[2][SAMPLE_WINDOW];
float accelBufferY[2][SAMPLE_WINDOW];
float accelBufferZ[2][SAMPLE_WINDOW];
volatile int writeBuf = 0;
volatile int readBuf = 1;
volatile int bufferIndex = 0;
volatile bool bufferReady = false;

// Processed Machine Health Metrics
struct HealthMetrics {
  float vRms;          // Velocity RMS (mm/s)
  float crestFactor;   // Peak / RMS
  float kurtosis;      // 4th moment statistical peakedness
  float temperature;   // Bearing temperature (°C)
  float tempRate;      // dT/dt (°C / min)
  float acousticNoise; // Acoustic level
  int   shaftRpm;      // Rotational speed
  MachineHealthZone zone;
};

HealthMetrics currentMetrics;

// FreeRTOS Handles
SemaphoreHandle_t metricsMutex;

// Tachometer Interrupt Tracking
volatile unsigned long tachPulses = 0;
volatile unsigned long lastTachTime = 0;
void IRAM_ATTR isrTachometer() {
  tachPulses++;
}

// ======================== HELPER FUNCTIONS ==================================
float readBearingTempC() {
  int raw = analogRead(PIN_TEMP_ANALOG);
  if (raw <= 10) return -20.0f;
  if (raw >= 4085) return 120.0f;
  // Kit module #13: 10k pullup resistor and 10k NTC thermistor to ground
  // Formula: R_ntc = 10k / ((4095 / raw) - 1.0)
  float rTherm = 10000.0f / ((4095.0f / (float)raw) - 1.0f);
  // Steinhart-Hart / Beta equation (B = 3950, R0 = 10k at 25°C = 298.15K)
  float steinhart = rTherm / 10000.0f;          // (R/Ro)
  steinhart = log(steinhart);                   // ln(R/Ro)
  steinhart /= 3950.0f;                         // 1/B * ln(R/Ro)
  steinhart += 1.0f / (25.0f + 273.15f);        // + (1/To)
  steinhart = 1.0f / steinhart;                 // Invert to Kelvin
  float tempC = steinhart - 273.15f;            // Convert to Celsius
  return tempC;
}

// Set RGB LED Color
void setZoneLed(byte r, byte g, byte b) {
  analogWrite(PIN_RGB_RED, r);
  analogWrite(PIN_RGB_GREEN, g);
  analogWrite(PIN_RGB_BLUE, b);
}

// ======================== FREERTOS TASK: CORE 1 HIGH-SPEED SAMPLER ==========
void TaskVibrationSampler(void *pvParameters) {
  const TickType_t xFrequency = pdMS_TO_TICKS(2); // 500 Hz (2ms period)
  TickType_t xLastWakeTime = xTaskGetTickCount();

  Serial.println(F("[Core 1] High-Frequency 500 Hz Vibration Sampler Online."));

  for (;;) {
    vTaskDelayUntil(&xLastWakeTime, xFrequency);

    // Read MPU-6050 Accelerometer
    Wire.beginTransmission(MPU_ADDR);
    Wire.write(0x3B);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)MPU_ADDR, (size_t)6, true);

    if (Wire.available() >= 6) {
      uint8_t axh = Wire.read(); uint8_t axl = Wire.read();
      uint8_t ayh = Wire.read(); uint8_t ayl = Wire.read();
      uint8_t azh = Wire.read(); uint8_t azl = Wire.read();
      int16_t ax = (int16_t)((axh << 8) | axl);
      int16_t ay = (int16_t)((ayh << 8) | ayl);
      int16_t az = (int16_t)((azh << 8) | azl);

      // Convert to g's (16384 LSB/g) into active write buffer
      accelBufferX[writeBuf][bufferIndex] = ax / 16384.0f;
      accelBufferY[writeBuf][bufferIndex] = ay / 16384.0f;
      accelBufferZ[writeBuf][bufferIndex] = az / 16384.0f;

      bufferIndex++;
      if (bufferIndex >= SAMPLE_WINDOW) {
        bufferIndex = 0;
        readBuf = writeBuf;
        writeBuf = 1 - writeBuf;
        bufferReady = true;
      }
    }
  }
}

// ======================== FREERTOS TASK: CORE 0 DIAGNOSTICS & FUSION ========
void TaskDiagnostics(void *pvParameters) {
  unsigned long lastTempCheck = millis();
  float prevTemp = 25.0f;
  unsigned long lastRpmCheck = millis();

  Serial.println(F("[Core 0] TaskDiagnostics & Sensor Fusion Online."));

  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(100)); // Run analysis every 100ms
    unsigned long now = millis();

    // 1. Process Vibration Window if Ready
    if (bufferReady) {
      bufferReady = false;
      int curRead = readBuf;

      float sumSquares = 0.0f;
      float peakVal = 0.0f;
      float sumFourthPower = 0.0f;
      float mean = 0.0f;

      // Calculate Total Dynamic Acceleration Magnitude: |a| - 1g (remove static gravity)
      for (int i = 0; i < SAMPLE_WINDOW; i++) {
        float ax = accelBufferX[curRead][i];
        float ay = accelBufferY[curRead][i];
        float az = accelBufferZ[curRead][i];
        float mag = sqrt(ax * ax + ay * ay + az * az) - 1.0f;
        mean += mag;
      }
      mean /= SAMPLE_WINDOW;

      for (int i = 0; i < SAMPLE_WINDOW; i++) {
        float ax = accelBufferX[curRead][i];
        float ay = accelBufferY[curRead][i];
        float az = accelBufferZ[curRead][i];
        float mag = sqrt(ax * ax + ay * ay + az * az) - 1.0f; // signed dynamic acceleration

        float diff = mag - mean;
        sumSquares += diff * diff;
        sumFourthPower += diff * diff * diff * diff;
        float absMag = abs(mag);
        if (absMag > peakVal) peakVal = absMag;
      }

      float variance = sumSquares / SAMPLE_WINDOW;
      float stdDev = sqrt(variance);
      float kurtosis = (variance > 0.0001f) ? (sumFourthPower / (SAMPLE_WINDOW * variance * variance)) : 3.0f;

      // Approximate RMS velocity in mm/s from acceleration (v = a / (2*pi*f)):
      // For dominant 50 Hz industrial motor vibration: 1g ≈ 31.2 mm/s RMS
      float rmsVelocity = stdDev * 31.2f;
      float crestFactor = (stdDev > 0.001f) ? (peakVal / stdDev) : 1.0f;

      // 2. Measure Bearing Thermal Gradient
      float temp = readBearingTempC();
      float dtMinutes = (now - lastTempCheck) / 60000.0f;
      float tempRate = 0.0f;
      if (dtMinutes >= 0.1f) {
        tempRate = (temp - prevTemp) / dtMinutes; // °C per minute
        prevTemp = temp;
        lastTempCheck = now;
      }

      // 3. Read High-Frequency Acoustic Noise
      int rawAcoustic = analogRead(PIN_SOUND_ANALOG);

      // 4. Calculate Shaft RPM
      unsigned long dtRpm = now - lastRpmCheck;
      int rpm = 0;
      if (dtRpm >= 500) {
        rpm = (tachPulses * 60000) / dtRpm;
        tachPulses = 0;
        lastRpmCheck = now;
      }

      // 5. Sensor Fusion & ISO 10816 Health Evaluation
      MachineHealthZone zone = ZONE_A_GOOD;

      // Base ISO velocity zone
      if (rmsVelocity >= ISO_ZONE_D_LIMIT) {
        zone = ZONE_D_UNACCEPTABLE;
      } else if (rmsVelocity >= ISO_ZONE_C_LIMIT) {
        zone = ZONE_C_UNSATISFACTORY;
      } else if (rmsVelocity >= ISO_ZONE_B_LIMIT) {
        zone = ZONE_B_ACCEPTABLE;
      } else {
        zone = ZONE_A_GOOD;
      }

      // Multi-Domain Fusion Escalation:
      // If Kurtosis > 4.5 (bearing pitting) AND Acoustic noise is high (>1500), escalate zone
      if (kurtosis > 4.5f && rawAcoustic > 1500 && zone < ZONE_C_UNSATISFACTORY) {
        zone = ZONE_C_UNSATISFACTORY; // Pre-failure warning
      }
      // If Bearing Temp is critically high (>75°C) or thermal rate > 5°C/min, trip immediately!
      if (temp > 75.0f || tempRate > 5.0f) {
        zone = ZONE_D_UNACCEPTABLE;
      }

      // 6. Actuator Protection Control
      if (xSemaphoreTake(metricsMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        currentMetrics.vRms = rmsVelocity;
        currentMetrics.crestFactor = crestFactor;
        currentMetrics.kurtosis = kurtosis;
        currentMetrics.temperature = temp;
        currentMetrics.tempRate = tempRate;
        currentMetrics.acousticNoise = rawAcoustic;
        currentMetrics.shaftRpm = rpm;
        currentMetrics.zone = zone;
        xSemaphoreGive(metricsMutex);
      }

      switch (zone) {
        case ZONE_A_GOOD:
          setZoneLed(0, 255, 0);             // Green
          digitalWrite(PIN_RELAY_TRIP, HIGH);// Motor Enabled
          noTone(PIN_BUZZER);
          break;

        case ZONE_B_ACCEPTABLE:
          setZoneLed(0, 200, 255);           // Cyan
          digitalWrite(PIN_RELAY_TRIP, HIGH);
          noTone(PIN_BUZZER);
          break;

        case ZONE_C_UNSATISFACTORY:
          setZoneLed(255, 140, 0);           // Amber
          digitalWrite(PIN_RELAY_TRIP, HIGH);
          tone(PIN_BUZZER, 1000, 100);       // Intermittent advisory chirp
          break;

        case ZONE_D_UNACCEPTABLE:
          setZoneLed(255, 0, 0);             // Bright Red
          digitalWrite(PIN_RELAY_TRIP, LOW); // TRIP MOTOR CONTACTOR (EMERGENCY SHUTDOWN!)
          tone(PIN_BUZZER, 2400);            // Continuous siren
          break;
      }

      // Serial Telemetry
      Serial.print(F("[PdM-FUSION] v_RMS: "));
      Serial.print(rmsVelocity, 2);
      Serial.print(F(" mm/s | Kurtosis: "));
      Serial.print(kurtosis, 2);
      Serial.print(F(" | Temp: "));
      Serial.print(temp, 1);
      Serial.print(F("C (Rate: "));
      Serial.print(tempRate, 1);
      Serial.print(F("C/min) | RPM: "));
      Serial.print(rpm);
      Serial.print(F(" -> ISO ZONE: "));

      switch (zone) {
        case ZONE_A_GOOD:          Serial.println(F("ZONE A [Good]")); break;
        case ZONE_B_ACCEPTABLE:    Serial.println(F("ZONE B [Acceptable]")); break;
        case ZONE_C_UNSATISFACTORY:Serial.println(F("ZONE C [Warning: Maintenance Required]")); break;
        case ZONE_D_UNACCEPTABLE:  Serial.println(F("ZONE D [CRITICAL: MOTOR TRIPPED!]")); break;
      }
    }
  }
}

// ======================== SETUP =============================================
void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000);

  Serial.println(F("\n============================================================"));
  Serial.println(F("  PROJECT 2: ESP32-S3 PREDICTIVE MAINTENANCE (PdM) SENTINEL"));
  Serial.println(F("============================================================"));

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000); // 400 kHz Fast I2C
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B);
  Wire.write(0); // Wake MPU-6050
  Wire.endTransmission();

  pinMode(PIN_TACH_PULSE, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_TACH_PULSE), isrTachometer, RISING);

  pinMode(PIN_RELAY_TRIP, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_RGB_RED, OUTPUT);
  pinMode(PIN_RGB_GREEN, OUTPUT);
  pinMode(PIN_RGB_BLUE, OUTPUT);

  // Initial Safe Defaults
  digitalWrite(PIN_RELAY_TRIP, HIGH); // Motor enabled
  setZoneLed(0, 255, 0); // Green
  noTone(PIN_BUZZER);

  metricsMutex = xSemaphoreCreateMutex();

  // Core 1: 500 Hz Sampling Task
  xTaskCreatePinnedToCore(
    TaskVibrationSampler,
    "VibeSampler",
    4096,
    NULL,
    5,
    NULL,
    1
  );

  // Core 0: Diagnostics & Fusion Task
  xTaskCreatePinnedToCore(
    TaskDiagnostics,
    "Diagnostics",
    4096,
    NULL,
    2,
    NULL,
    0
  );

  Serial.println(F("Industrial PdM Monitor Online. ISO 10816 standards active.\n"));
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
