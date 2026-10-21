# Complicated Projects - Level 1 (Multi-Sensor Fusion for Arduino Uno)

Welcome to the **Complicated Projects Level 1** suite! These projects demonstrate advanced embedded systems programming, multi-sensor fusion, and non-blocking state machines on the **Arduino Uno** (ATmega328P) using sensors and actuators from the **Hosyond 45-in-1 Sensor Kit**.

---

## What is Multi-Sensor Fusion?
In real-world engineering, single sensors are unreliable:
- Optical sensors are blinded by ambient light or shadows.
- Thermal sensors suffer from thermal inertia and delay.
- Acoustic sensors can't distinguish between speech, wind, and alarms.
- Mechanical switches bounce and give false positives.

**Sensor Fusion** combines complementary physical domains (optical, acoustic, inertial, thermal, electrical) with algorithmic weighting, derivative tracking ($\frac{dT}{dt}$), exponential smoothing (EMA), and multi-layer state machines. The result is a robust, production-grade embedded control loop with high confidence and near-zero false triggers.

---

## Level 1 Projects Catalog

| # | Project Name | Fused Sensors | Actuators & Outputs | Key Fusion Principle |
|---|---|---|---|---|
| **01** | [**Environmental Hazard Guardian**](./Project_01_Hazard_Guardian_Fire_Thermal_Sound/) | Flame Sensor (#34)<br>Analog Temp (#13)<br>Sound Sensor (#37) | RGB LED (#01)<br>Active Buzzer (#11)<br>5V Relay (#30) | Fuses optical IR flame flicker with rate-of-heating derivative ($\frac{dT}{dt}$) and acoustic explosion spikes to detect fires in <50ms without false alarms. |
| **02** | [**Smart Agritech Plant Ecosystem**](./Project_02_Smart_Plant_Ecosystem_Soil_Water_Light_Temp/) | Soil Moisture (#42)<br>Water Level (#39)<br>Photoresistor LDR (#14)<br>DHT11 Climate (#07) | 5V Relay Pump (#30)<br>Double Color LED (#08)<br>Passive Buzzer (#12) | Evapotranspiration estimation: prevents midday thermal sun scorch, enforces soil drainage cooldown, and locks out the water pump if the reservoir is dry. |
| **03** | [**Autonomous Proximity Radar**](./Project_03_Autonomous_Proximity_Radar_Ultrasonic_IR_Encoder/) | HC-SR04 Ultrasonic (#45)<br>IR Obstacle Avoidance (#29)<br>Ground Line Tracker (#28) | RGB Gradient (#01)<br>Passive Beeper (#12)<br>Brake Relay (#30) | Fuses metric acoustic time-of-flight ranging with microsecond optical near-field reflection and downward cliff drop-off detection to eliminate all blind spots. |
| **04** | [**Multi-Layer Security Sentinel**](./Project_04_Security_Sentinel_IMU_Shock_Tilt_Touch/) | GY-521 6-DOF IMU (#40)<br>Shock Sensor (#16)<br>Metal Touch (#33)<br>Tilt Switch (#21) | RGB Police Strobe (#01)<br>Active Buzzer Siren (#11)<br>Deadbolt Relay (#30) | 4-layer physical perimeter defense: distinguishes human touch from low-G structural tilt and violent blunt impact using dual hardware interrupts and 3D jerk vectors. |

---

## Architectural Highlights
All Level 1 projects adhere to best practices in embedded firmware development:
1. **100% Non-Blocking Execution:** Uses `millis()` and `micros()` timers—**no `delay()` calls** in active runtime loops.
2. **Hardware Interrupt Integration:** Time-critical signals (sound spikes, mechanical shock, chassis touch) use Arduino Uno hardware interrupts (`INT0` on Pin 2, `INT1` on Pin 3).
3. **No External Library Headaches:** Sketches use native register and direct bit-banged protocols (e.g. native I2C register access for MPU-6050, native 1-wire timing for DHT11), so they compile immediately in Arduino IDE with **zero library installation errors**.
4. **Diagnostic Serial Telemetry:** Every sketch outputs structured live telemetry over USB Serial at **115200 baud** for easy debugging via the Arduino Serial Monitor or Serial Plotter.
5. **Fail-Safe Actuator Interlocks:** Emergency relays and sirens default to safe, inactive states upon boot and power loss.

---

## Quick Start
1. Connect your **Arduino Uno** via USB.
2. Open any sketch folder (e.g. `Project_01_Hazard_Guardian_Fire_Thermal_Sound/Project_01_Hazard_Guardian_Fire_Thermal_Sound.ino`).
3. Select `Tools > Board > Arduino Uno` and your COM port under `Tools > Port`.
4. Open the project's `README.md` and connect the sensor pins according to the **Wiring Table**.
5. Click **Upload** and open `Tools > Serial Monitor` at **115200 baud**.
