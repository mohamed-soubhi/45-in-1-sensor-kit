/*
 * ==============================================================================
 * Project 1: FreeRTOS 6-DOF Inertial AHRS & Gimbal with Mahony Filter
 * Level 2 Multi-Sensor Fusion on ESP32-S3 (Dual-Core Xtensa LX7 @ 240MHz)
 * ==============================================================================
 * Target: ESP32-S3 Dev Module
 * Kit: Hosyond 45-in-1 Sensor Kit
 * 
 * Fused Modules:
 *  - Module #40: GY-521 (MPU-6050 6-DOF Accelerometer & Gyroscope on I2C)
 *  - Module #20: Analog Joystick Module (Dual-axis Pan/Tilt setpoint override)
 *  - Module #19: Rotary Encoder Module (Fine manual stabilization trim)
 *  - Module #16: Shock / Tap Sensor Module (Sudden mechanical impact override)
 * 
 * Actuators & Telemetry:
 *  - Dual-Channel LEDC High-Resolution PWM (Simulating Pan & Tilt Gimbal Actuators)
 *  - Module #01: RGB LED (Color-coded Flight & Fusion Mode)
 *  - Module #11: Active Buzzer (Calibration & AHRS Lock acoustic tone)
 *  - Built-in Wi-Fi AP + Async Web Server (Core 0 Web Dashboard & Live Roll/Pitch/Yaw)
 * 
 * Level 2 Sensor Fusion Mathematical Core:
 *  - Mahony AHRS Algorithm: Fuses 3D gyroscope angular rates with 3D gravity vector
 *    from accelerometer using proportional-integral (PI) feedback to eliminate gyro drift.
 *  - Multi-Core FreeRTOS Partitioning:
 *      * Core 1 (Priority 5): 100 Hz Real-Time Sensor Fusion & Gimbal PID Loop.
 *      * Core 0 (Priority 1): Wi-Fi Stack, Web Server & JSON Telemetry Broadcast.
 *      * Thread-Safe Inter-Core Communication via FreeRTOS Queues and Mutexes.
 */

#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>

// ======================== PIN DEFINITIONS (ESP32-S3) ========================
// I2C Bus for MPU-6050
const int PIN_I2C_SDA      = 8;   // ESP32-S3 default high-speed I2C SDA
const int PIN_I2C_SCL      = 9;   // ESP32-S3 default high-speed I2C SCL
const int MPU_ADDR         = 0x68;

// Analog Joystick (ADC1 channels safe with Wi-Fi)
const int PIN_JOY_X        = 4;   // ADC1_CH3 - Pan command
const int PIN_JOY_Y        = 5;   // ADC1_CH4 - Tilt command
const int PIN_JOY_SW       = 6;   // Digital push switch

// Rotary Encoder
const int PIN_ENC_CLK      = 10;  // Quadrature Clock A
const int PIN_ENC_DT       = 11;  // Quadrature Data B
const int PIN_ENC_SW       = 12;  // Pushbutton

// Shock & Impact Sensor
const int PIN_SHOCK        = 13;  // Hardware interrupt for violent impact detection

// Actuators & Indicators
const int PIN_RGB_RED      = 14;  // LEDC Channel 0
const int PIN_RGB_GREEN    = 15;  // LEDC Channel 1
const int PIN_RGB_BLUE     = 16;  // LEDC Channel 2
const int PIN_GIMBAL_PITCH = 17;  // LEDC Channel 3 (Servo / Motor Pitch PWM)
const int PIN_GIMBAL_ROLL  = 18;  // LEDC Channel 4 (Servo / Motor Roll PWM)
const int PIN_BUZZER       = 21;  // Active Buzzer

// ==================== SENSOR FUSION DATA STRUCTURES =========================
struct AttitudeEstimate {
  float roll;       // Degrees (-180 to +180)
  float pitch;      // Degrees (-90 to +90)
  float yaw;        // Degrees (-180 to +180)
  float gyroX, gyroY, gyroZ; // deg/s
  float accelX, accelY, accelZ; // g
  unsigned long timestamp;
  bool isImpact;
};

// FreeRTOS Handles
QueueHandle_t telemetryQueue;
SemaphoreHandle_t i2cMutex;

// Wi-Fi Access Point Configuration
const char* AP_SSID = "ESP32S3-Gimbal-AHRS";
const char* AP_PASS = "fusion123";
WebServer server(80);

// Global State (Protected by Mutex or Read-Only for Web)
AttitudeEstimate currentAttitude;
volatile float rollTrimOffset = 0.0;
volatile float pitchTrimOffset = 0.0;
volatile bool emergencyBrake = false;

// ======================== MAHONY FILTER CONSTANTS ===========================
// TwoKp: Proportional feedback gain to pull gyro back to accelerometer gravity
// TwoKi: Integral feedback gain to eliminate persistent bias
const float TwoKp = 2.0f * 0.5f;
const float TwoKi = 2.0f * 0.05f;

// Quaternion orientation of earth relative to sensor
float q0 = 1.0f, q1 = 0.0f, q2 = 0.0f, q3 = 0.0f;
float integralFBx = 0.0f, integralFBy = 0.0f, integralFBz = 0.0f;

// Fast inverse square root
float invSqrt(float x) {
  float halfx = 0.5f * x;
  float y = x;
  long i = *(long*)&y;
  i = 0x5f3759df - (i >> 1);
  y = *(float*)&i;
  y = y * (1.5f - (halfx * y * y));
  return y;
}

// Mahony AHRS 6-DOF update (Accelerometers & Gyroscopes)
void MahonyAHRSupdateIMU(float gx, float gy, float gz, float ax, float ay, float az, float dt) {
  float recipNorm;
  float halfvx, halfvy, halfvz;
  float halfex, halfey, halfez;
  float qa, qb, qc;

  // Compute feedback only if accelerometer measurement valid (avoids NaN in normalisation)
  if (!((ax == 0.0f) && (ay == 0.0f) && (az == 0.0f))) {
    // Normalise accelerometer measurement
    recipNorm = invSqrt(ax * ax + ay * ay + az * az);
    ax *= recipNorm;
    ay *= recipNorm;
    az *= recipNorm;

    // Estimated direction of gravity and vector perpendicular to magnetic flux
    halfvx = q1 * q3 - q0 * q2;
    halfvy = q0 * q1 + q2 * q3;
    halfvz = q0 * q0 - 0.5f + q3 * q3;

    // Error is sum of cross product between estimated and measured direction of gravity
    halfex = (ay * halfvz - az * halfvy);
    halfey = (az * halfvx - ax * halfvz);
    halfez = (ax * halfvy - ay * halfvx);

    // Compute and apply integral feedback if enabled
    if (TwoKi > 0.0f) {
      integralFBx += TwoKi * halfex * dt;
      integralFBy += TwoKi * halfey * dt;
      integralFBz += TwoKi * halfez * dt;
      gx += integralFBx;
      gy += integralFBy;
      gz += integralFBz;
    } else {
      integralFBx = 0.0f;
      integralFBy = 0.0f;
      integralFBz = 0.0f;
    }

    // Apply proportional feedback
    gx += TwoKp * halfex;
    gy += TwoKp * halfey;
    gz += TwoKp * halfez;
  }

  // Integrate rate of change of quaternion
  gx *= (0.5f * dt);
  gy *= (0.5f * dt);
  gz *= (0.5f * dt);
  qa = q0;
  qb = q1;
  qc = q2;
  q0 += (-qb * gx - qc * gy - q3 * gz);
  q1 += (qa * gx + qc * gz - q3 * gy);
  q2 += (qa * gy - qb * gz + q3 * gx);
  q3 += (qa * gz + qb * gy - qc * gx);

  // Normalise quaternion
  recipNorm = invSqrt(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
  q0 *= recipNorm;
  q1 *= recipNorm;
  q2 *= recipNorm;
  q3 *= recipNorm;
}

// Convert Quaternions to Euler Angles (Roll, Pitch, Yaw in degrees)
void computeEulerAngles(float &roll, float &pitch, float &yaw) {
  roll  = atan2(2.0f * (q0 * q1 + q2 * q3), 1.0f - 2.0f * (q1 * q1 + q2 * q2)) * 180.0f / PI;
  pitch = asin(constrain(2.0f * (q0 * q2 - q3 * q1), -1.0f, 1.0f)) * 180.0f / PI;
  yaw   = atan2(2.0f * (q0 * q3 + q1 * q2), 1.0f - 2.0f * (q2 * q2 + q3 * q3)) * 180.0f / PI;
}

// ======================== HARDWARE INTERRUPT ================================
volatile unsigned long lastShockTime = 0;
void IRAM_ATTR isrShockImpact() {
  unsigned long now = millis();
  if (now - lastShockTime > 100) {
    emergencyBrake = true;
    lastShockTime = now;
  }
}

// ======================== WEB SERVER ENDPOINTS ==============================
void handleRoot() {
  String page = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  page += "<title>ESP32-S3 AHRS Telemetry</title>";
  page += "<style>body{background:#0f172a;color:#f8fafc;font-family:sans-serif;padding:20px;text-align:center;}";
  page += ".card{background:#1e293b;border:1px solid #334155;border-radius:12px;padding:20px;max-width:500px;margin:20px auto;box-shadow:0 10px 25px rgba(0,0,0,0.5);}";
  page += ".val{font-size:2.2rem;font-weight:bold;color:#38bdf8;margin:10px 0;}";
  page += ".badge{background:#f59e0b;color:#0f172a;font-weight:bold;padding:4px 10px;border-radius:6px;}";
  page += "</style></head><body>";
  page += "<h2>🚀 ESP32-S3 FreeRTOS 6-DOF AHRS Hub</h2>";
  page += "<span class='badge'>Mahony Fusion Filter @ 100 Hz</span>";
  page += "<div class='card'><div>ROLL (X-Axis)</div><div class='val' id='roll'>0.0&deg;</div>";
  page += "<div>PITCH (Y-Axis)</div><div class='val' id='pitch'>0.0&deg;</div>";
  page += "<div>YAW (Z-Axis Rate)</div><div class='val' id='yaw'>0.0&deg;</div></div>";
  page += "<script>setInterval(()=>{fetch('/data').then(r=>r.json()).then(d=>{";
  page += "document.getElementById('roll').innerHTML=d.roll.toFixed(1)+'&deg;';";
  page += "document.getElementById('pitch').innerHTML=d.pitch.toFixed(1)+'&deg;';";
  page += "document.getElementById('yaw').innerHTML=d.yaw.toFixed(1)+'&deg;';";
  page += "});},100);</script></body></html>";
  server.send(200, "text/html", page);
}

void handleData() {
  AttitudeEstimate att;
  if (xQueuePeek(telemetryQueue, &att, pdMS_TO_TICKS(10)) != pdTRUE) {
    att = currentAttitude;
  }
  String json = "{";
  json += "\"roll\":" + String(att.roll, 2) + ",";
  json += "\"pitch\":" + String(att.pitch, 2) + ",";
  json += "\"yaw\":" + String(att.yaw, 2) + ",";
  json += "\"impact\":" + String(att.isImpact ? "true" : "false");
  json += "}";
  server.send(200, "application/json", json);
}

// ======================== FREERTOS TASK: CORE 1 SENSOR FUSION ===============
void TaskSensorFusion(void *pvParameters) {
  const TickType_t xFrequency = pdMS_TO_TICKS(10); // 100 Hz (10 ms)
  TickType_t xLastWakeTime = xTaskGetTickCount();
  unsigned long lastTime = micros();

  Serial.println(F("[Core 1] TaskSensorFusion started at 100 Hz."));

  for (;;) {
    vTaskDelayUntil(&xLastWakeTime, xFrequency);

    unsigned long now = micros();
    float dt = (now - lastTime) / 1000000.0f;
    lastTime = now;
    if (dt <= 0.0f || dt > 0.1f) dt = 0.01f;

    // 1. Read MPU-6050 Accelerometer & Gyroscope via I2C with Mutex
    int16_t axRaw = 0, ayRaw = 0, azRaw = 16384, gxRaw = 0, gyRaw = 0, gzRaw = 0;
    if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
      Wire.beginTransmission(MPU_ADDR);
      Wire.write(0x3B);
      Wire.endTransmission(false);
      Wire.requestFrom((uint8_t)MPU_ADDR, (size_t)14, true);

      if (Wire.available() >= 14) {
        uint8_t axh = Wire.read(); uint8_t axl = Wire.read();
        uint8_t ayh = Wire.read(); uint8_t ayl = Wire.read();
        uint8_t azh = Wire.read(); uint8_t azl = Wire.read();
        Wire.read(); Wire.read(); // Skip temperature
        uint8_t gxh = Wire.read(); uint8_t gxl = Wire.read();
        uint8_t gyh = Wire.read(); uint8_t gyl = Wire.read();
        uint8_t gzh = Wire.read(); uint8_t gzl = Wire.read();
        axRaw = (int16_t)((axh << 8) | axl);
        ayRaw = (int16_t)((ayh << 8) | ayl);
        azRaw = (int16_t)((azh << 8) | azl);
        gxRaw = (int16_t)((gxh << 8) | gxl);
        gyRaw = (int16_t)((gyh << 8) | gyl);
        gzRaw = (int16_t)((gzh << 8) | gzl);
      }
      xSemaphoreGive(i2cMutex);
    }

    // Convert raw to physical units:
    // Accel +/-2g: 16384 LSB/g | Gyro +/-250 deg/s: 131.0 LSB/(deg/s)
    float ax = axRaw / 16384.0f;
    float ay = ayRaw / 16384.0f;
    float az = azRaw / 16384.0f;

    float gx = (gxRaw / 131.0f) * (PI / 180.0f); // Convert to radians/sec for Mahony
    float gy = (gyRaw / 131.0f) * (PI / 180.0f);
    float gz = (gzRaw / 131.0f) * (PI / 180.0f);

    // 2. Execute Level-2 Mahony AHRS Algorithm
    MahonyAHRSupdateIMU(gx, gy, gz, ax, ay, az, dt);

    // 3. Extract Euler Angles
    float roll, pitch, yaw;
    computeEulerAngles(roll, pitch, yaw);

    // Read Joystick Manual Setpoint
    int joyX = analogRead(PIN_JOY_X);
    int joyY = analogRead(PIN_JOY_Y);
    float panOffset = map(joyX, 0, 4095, -30, 30);
    float tiltOffset = map(joyY, 0, 4095, -30, 30);

    // Rotary Encoder manual roll trim
    static int lastEncClk = HIGH;
    int encClk = digitalRead(PIN_ENC_CLK);
    if (encClk != lastEncClk && encClk == LOW) {
      if (digitalRead(PIN_ENC_DT) != encClk) {
        rollTrimOffset += 0.5f;
      } else {
        rollTrimOffset -= 0.5f;
      }
    }
    lastEncClk = encClk;

    // Joystick or Encoder switch resets manual trim
    if (digitalRead(PIN_JOY_SW) == LOW || digitalRead(PIN_ENC_SW) == LOW) {
      rollTrimOffset = 0.0f;
      pitchTrimOffset = 0.0f;
    }

    // Update Global Attitude
    currentAttitude.roll = roll + rollTrimOffset;
    currentAttitude.pitch = pitch + pitchTrimOffset + tiltOffset;
    currentAttitude.yaw = yaw + panOffset;
    currentAttitude.accelX = ax;
    currentAttitude.accelY = ay;
    currentAttitude.accelZ = az;
    currentAttitude.isImpact = emergencyBrake;
    currentAttitude.timestamp = millis();

    // 4. Actuator Gimbal PWM Output
    if (!emergencyBrake) {
      // Map Roll/Pitch (-45 to +45 deg) to Servo PWM Pulse (1000us to 2000us)
      int rollUs = constrain(map((int)currentAttitude.roll, -45, 45, 1000, 2000), 1000, 2000);
      int pitchUs = constrain(map((int)currentAttitude.pitch, -45, 45, 1000, 2000), 1000, 2000);
      // Convert pulse us to 14-bit duty (50 Hz period = 20000 us, max duty = 16383)
      uint32_t rollDuty = (uint32_t)rollUs * 16383UL / 20000UL;
      uint32_t pitchDuty = (uint32_t)pitchUs * 16383UL / 20000UL;
      ledcWrite(PIN_GIMBAL_ROLL, rollDuty);
      ledcWrite(PIN_GIMBAL_PITCH, pitchDuty);
      // Status RGB: Smooth Cyan (Stabilizing active)
      ledcWrite(PIN_RGB_RED, 0);
      ledcWrite(PIN_RGB_GREEN, 180);
      ledcWrite(PIN_RGB_BLUE, 255);
      digitalWrite(PIN_BUZZER, LOW);
    } else {
      // Impact emergency override: lock actuators, buzz & pulse Red
      ledcWrite(PIN_RGB_RED, 255);
      ledcWrite(PIN_RGB_GREEN, 0);
      ledcWrite(PIN_RGB_BLUE, 0);
      digitalWrite(PIN_BUZZER, (millis() / 200) % 2 ? HIGH : LOW);
      if (millis() - lastShockTime > 3000) {
        emergencyBrake = false; // Auto-resume after 3s
        digitalWrite(PIN_BUZZER, LOW);
      }
    }

    // Push to Queue for Core 0
    xQueueOverwrite(telemetryQueue, &currentAttitude);
  }
}

// ======================== FREERTOS TASK: CORE 0 NETWORK & TELEMETRY =========
void TaskNetwork(void *pvParameters) {
  Serial.println(F("[Core 0] TaskNetwork started. Handling Wi-Fi and Web server."));

  for (;;) {
    server.handleClient();
    vTaskDelay(pdMS_TO_TICKS(5)); // Yield 5ms to allow background TCP/IP stack
  }
}

// ======================== SETUP =============================================
void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000);

  Serial.println(F("\n============================================================"));
  Serial.println(F("  PROJECT 1: ESP32-S3 FREERTOS 6-DOF MAHONY AHRS GIMBAL"));
  Serial.println(F("============================================================"));

  // 1. Initialize High-Speed I2C on Pins 8/9
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000); // 400 kHz Fast-Mode
  i2cMutex = xSemaphoreCreateMutex();

  // Wake up MPU-6050
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B);
  Wire.write(0);
  Wire.endTransmission();

  // 2. Hardware Interrupt for Shock Sensor & Input Peripherals
  pinMode(PIN_SHOCK, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_SHOCK), isrShockImpact, FALLING);
  pinMode(PIN_ENC_CLK, INPUT_PULLUP);
  pinMode(PIN_ENC_DT, INPUT_PULLUP);
  pinMode(PIN_ENC_SW, INPUT_PULLUP);
  pinMode(PIN_JOY_SW, INPUT_PULLUP);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, HIGH);
  delay(60);
  digitalWrite(PIN_BUZZER, LOW);

  // 3. Configure High-Resolution LEDC PWM Channels
  ledcAttach(PIN_RGB_RED, 5000, 8);   // 5 kHz, 8-bit resolution
  ledcAttach(PIN_RGB_GREEN, 5000, 8);
  ledcAttach(PIN_RGB_BLUE, 5000, 8);
  ledcAttach(PIN_GIMBAL_ROLL, 50, 14);  // 50 Hz Servo standard, 14-bit
  ledcAttach(PIN_GIMBAL_PITCH, 50, 14);

  // 4. Initialize Wi-Fi Access Point & Web Server
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print(F("Wi-Fi AP Online: ")); Serial.println(AP_SSID);
  Serial.print(F("Connect & Open: http://")); Serial.println(WiFi.softAPIP());

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.begin();

  // 5. Create FreeRTOS Queue
  telemetryQueue = xQueueCreate(1, sizeof(AttitudeEstimate));

  // 6. Spawn Core 1 Real-Time Task (100 Hz Fusion)
  xTaskCreatePinnedToCore(
    TaskSensorFusion,
    "SensorFusionTask",
    4096,
    NULL,
    5, // High priority
    NULL,
    1  // Dedicated to Core 1
  );

  // 7. Spawn Core 0 Network Task (Wi-Fi Web Server)
  xTaskCreatePinnedToCore(
    TaskNetwork,
    "NetworkTask",
    4096,
    NULL,
    1, // Low priority
    NULL,
    0  // Dedicated to Core 0
  );

  Serial.println(F("Dual-Core FreeRTOS scheduling active. System Online!\n"));
}

void loop() {
  // Main Arduino loop remains idle to preserve scheduler efficiency
  vTaskDelay(pdMS_TO_TICKS(1000));
}
