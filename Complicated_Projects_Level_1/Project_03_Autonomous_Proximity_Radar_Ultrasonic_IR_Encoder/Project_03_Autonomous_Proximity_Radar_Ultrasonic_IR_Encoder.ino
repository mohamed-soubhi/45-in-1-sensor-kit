/*
 * Project 3: Autonomous Proximity Radar & Dual-Domain Anti-Collision System
 * -------------------------------------------------------------------------
 * Target Board: Arduino Uno (ATmega328P)
 * Kit: Hosyond 45-in-1 Sensor Kit
 * 
 * Fused Sensors:
 *  - Module #45: HC-SR04 Ultrasonic Sensor (Trig D12, Echo D11) - Acoustic ToF Ranging
 *  - Module #29: Infrared Obstacle Avoidance Sensor (Digital D3) - Fast Optical Reflection
 *  - Module #28: Line Tracking Sensor (Digital D2) - Ground Cliff / Drop-Off Sensor
 * 
 * Actuators & Indicators:
 *  - Module #01: RGB LED (Pins D6 Red, D5 Green, D9 Blue) - Distance Gradient
 *  - Module #12: Passive Buzzer (Digital D8) - Automotive Variable Pitch Beep
 *  - Module #30: 5V Relay Module (Digital D7) - Emergency Braking / Motor Cutoff
 * 
 * Sensor Fusion Principle:
 *  1. Ultrasonic sensors provide metric distance (2cm to 400cm) via acoustic echo time,
 *     but suffer from surface reflection specular angles and sound-absorbing materials.
 *  2. Optical IR sensors respond instantaneously (<1ms) in the close field (1cm to 10cm),
 *     filling ultrasonic near-field blind spots.
 *  3. Ground Line-Tracker acts as a downward cliff sensor to prevent table drop-offs.
 *  4. Fusing both modalities eliminates blind spots and triggers staged safety responses:
 *     - CLEAR (> 35cm): Green LED, Silent, Relay energized (drive enabled)
 *     - APPROACH (20 - 35cm): Cyan/Yellow LED, Slow intermittent beeps (2 Hz)
 *     - WARNING (10 - 20cm): Orange LED, Fast intermittent beeps (8 Hz)
 *     - CRITICAL (< 10cm OR IR Trigger OR Cliff Detected): Red LED, Continuous Tone, Relay TRIP (Emergency Stop)
 */

// ======================== PIN ASSIGNMENTS ========================
const int PIN_US_TRIG       = 12;  // Ultrasonic Trigger
const int PIN_US_ECHO       = 11;  // Ultrasonic Echo
const int PIN_IR_OBSTACLE   = 3;   // Optical IR Obstacle (Active LOW)
const int PIN_CLIFF_TRACKER = 2;   // Ground Cliff Tracker (Active LOW over floor)

// Actuators
const int PIN_RGB_RED       = 6;   // PWM
const int PIN_RGB_GREEN     = 5;   // PWM
const int PIN_RGB_BLUE      = 9;   // PWM
const int PIN_BUZZER        = 8;   // Passive Buzzer (tone)
const int PIN_BRAKE_RELAY   = 7;   // 5V Relay for Motor / Brake (Active LOW)

// ==================== ZONES & THRESHOLDS =========================
const float DIST_CLEAR_CM    = 35.0;
const float DIST_WARNING_CM  = 20.0;
const float DIST_CRITICAL_CM = 10.0;

enum RadarZone {
  ZONE_CLEAR,       // Path completely unobstructed
  ZONE_APPROACH,    // Long-distance object detected (35 - 20cm)
  ZONE_WARNING,     // Medium-distance obstacle closing (20 - 10cm)
  ZONE_EMERGENCY    // Critical collision imminent / cliff breach!
};

RadarZone currentZone = ZONE_CLEAR;

// Non-blocking Timing
unsigned long lastRangingTime = 0;
const unsigned long RANGING_INTERVAL = 60; // 60ms between ultrasonic pings (avoids echoes)

unsigned long lastBeepToggleTime = 0;
bool beepState = false;
int beepIntervalMs = 500;

unsigned long lastTelemetryTime = 0;

// Filtered Distance (Exponential Moving Average)
float filteredDistance = 100.0;
const float ALPHA = 0.4; // Filter weight (0.0 to 1.0)

// ======================== HELPER FUNCTIONS =======================
// Trigger Ultrasonic Distance Measurement
float measureUltrasonicDistanceCm() {
  digitalWrite(PIN_US_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_US_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_US_TRIG, LOW);

  // Measure echo pulse duration (timeout 25ms = max ~4.3 meters)
  unsigned long duration = pulseIn(PIN_US_ECHO, HIGH, 25000);
  if (duration == 0) return 400.0; // Out of range or no echo

  // Speed of sound: 343 m/s = 0.0343 cm/us -> distance = (duration * 0.0343) / 2
  float distance = (duration * 0.0343) / 2.0;
  return distance;
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
  Serial.println(F("  PROJECT 3: PROXIMITY RADAR & MULTI-SENSOR ANTI-COLLISION"));
  Serial.println(F("======================================================="));

  pinMode(PIN_US_TRIG, OUTPUT);
  pinMode(PIN_US_ECHO, INPUT);
  pinMode(PIN_IR_OBSTACLE, INPUT);
  pinMode(PIN_CLIFF_TRACKER, INPUT);

  pinMode(PIN_RGB_RED, OUTPUT);
  pinMode(PIN_RGB_GREEN, OUTPUT);
  pinMode(PIN_RGB_BLUE, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_BRAKE_RELAY, OUTPUT);

  // Initial State: Safe drive enabled
  digitalWrite(PIN_BRAKE_RELAY, HIGH); // Relay ready (Active LOW)
  setRgbColor(0, 255, 0);              // Green
  noTone(PIN_BUZZER);

  Serial.println(F("Ultrasonic & Optical Radar online. Scanning perimeter...\n"));
}

// ======================== MAIN LOOP ==============================
void loop() {
  unsigned long currentMillis = millis();

  // 1. Periodic Ranging & Sensor Fusion
  if (currentMillis - lastRangingTime >= RANGING_INTERVAL) {
    lastRangingTime = currentMillis;

    // Read Ultrasonic Sensor with Exponential Filter
    float rawDist = measureUltrasonicDistanceCm();
    filteredDistance = (ALPHA * rawDist) + ((1.0 - ALPHA) * filteredDistance);

    // Read Optical Near-Field IR Obstacle (Active LOW)
    bool irObstacleDetected = (digitalRead(PIN_IR_OBSTACLE) == LOW);

    // Read Ground Cliff Tracker (Active HIGH indicates table edge/drop-off)
    bool cliffDetected = (digitalRead(PIN_CLIFF_TRACKER) == HIGH);

    // 2. Sensor Fusion Zone Determination
    if (cliffDetected || irObstacleDetected || filteredDistance < DIST_CRITICAL_CM) {
      currentZone = ZONE_EMERGENCY;
    } else if (filteredDistance < DIST_WARNING_CM) {
      currentZone = ZONE_WARNING;
    } else if (filteredDistance < DIST_CLEAR_CM) {
      currentZone = ZONE_APPROACH;
    } else {
      currentZone = ZONE_CLEAR;
    }

    // 3. Zone Actuation & LED Visualization
    switch (currentZone) {
      case ZONE_CLEAR:
        setRgbColor(0, 255, 0);              // Bright Green
        digitalWrite(PIN_BRAKE_RELAY, HIGH); // Traction motor enabled
        noTone(PIN_BUZZER);
        beepIntervalMs = 0;                  // Silent
        break;

      case ZONE_APPROACH:
        setRgbColor(255, 255, 0);            // Yellow
        digitalWrite(PIN_BRAKE_RELAY, HIGH); // Drive enabled but warning
        beepIntervalMs = 400;                // Slow cadence (2.5 Hz)
        break;

      case ZONE_WARNING:
        setRgbColor(255, 120, 0);            // Orange
        digitalWrite(PIN_BRAKE_RELAY, HIGH); // Drive enabled
        beepIntervalMs = 120;                // Rapid cadence (8 Hz)
        break;

      case ZONE_EMERGENCY:
        setRgbColor(255, 0, 0);              // Solid Red
        digitalWrite(PIN_BRAKE_RELAY, LOW);  // TRIP BRAKE RELAY (Emergency Stop!)
        beepIntervalMs = -1;                 // Continuous solid tone
        tone(PIN_BUZZER, 2200);              // High piercing pitch
        break;
    }

    // 4. Diagnostics Telemetry (Every 250ms)
    if (currentMillis - lastTelemetryTime >= 250) {
      lastTelemetryTime = currentMillis;
      Serial.print(F("[RADAR] Dist: "));
      Serial.print(filteredDistance, 1);
      Serial.print(F(" cm | IR Near: "));
      Serial.print(irObstacleDetected ? F("TRIPPED") : F("CLEAR"));
      Serial.print(F(" | Cliff: "));
      Serial.print(cliffDetected ? F("CLIFF!!") : F("OK"));
      Serial.print(F(" | ZONE: "));

      switch (currentZone) {
        case ZONE_CLEAR:      Serial.println(F("CLEAR [Drive Enabled]")); break;
        case ZONE_APPROACH:   Serial.println(F("APPROACH [Slow Down]")); break;
        case ZONE_WARNING:    Serial.println(F("WARNING [Caution]")); break;
        case ZONE_EMERGENCY:  Serial.println(F("EMERGENCY BRAKE ENGAGED!")); break;
      }
    }
  }

  // 5. Variable Pitch Acoustic Beeper (Non-blocking)
  if (beepIntervalMs > 0) {
    if (currentMillis - lastBeepToggleTime >= (unsigned long)beepIntervalMs) {
      lastBeepToggleTime = currentMillis;
      beepState = !beepState;
      if (beepState) {
        tone(PIN_BUZZER, 1800);
      } else {
        noTone(PIN_BUZZER);
      }
    }
  } else if (beepIntervalMs == 0) {
    noTone(PIN_BUZZER);
  }
}
