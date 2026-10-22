# Hosyond 45-in-1 Sensor Kit - Projects, Code & Multi-Sensor Fusion

Complete clone, extracted project codebase, and advanced multi-sensor fusion suites for the [Hosyond 45 In 1 Sensor Kit](https://45-in-1-sensor-kit.readthedocs.io/en/latest/).

## Interactive Web Dashboards & Documentation

- **Master Interactive Hub:** [`index.html`](./index.html) - Browse all 45 modules, preview sketches, connection diagrams, and sensor fusion projects.
- **Level 1 Uno Sensor Fusion Hub:** [`Complicated_Projects_Level_1/index.html`](./Complicated_Projects_Level_1/index.html)
- **Level 2 ESP32-S3 FreeRTOS Fusion Hub:** [`Complicated_Projects_Level_2_ESP32_S3/index.html`](./Complicated_Projects_Level_2_ESP32_S3/index.html)
- **Offline Sphinx ReadTheDocs:** [`docs/build/html/index.html`](./docs/build/html/index.html)

---

## Directory Layout

```
45-in-1-sensor-kit/
├── index.html                                        # Master Interactive Web Hub
├── CODE_REVIEW.md                                    # Comprehensive code audit & remediation log
├── Code/                                             # 45 base sensor module sketches & per-module HTML
│   ├── 1.Rgb_Led/
│   ├── ...
│   └── 45.Ultrasonic_Module/
├── Complicated_Projects_Level_1/                     # Arduino Uno Sensor Fusion Projects
│   ├── Project_01_Hazard_Guardian_Fire_Thermal_Sound/
│   ├── Project_02_Smart_Plant_Ecosystem_Soil_Water_Light_Temp/
│   ├── Project_03_Autonomous_Reverse_Safety_Radar_Brake/
│   └── Project_04_Security_Sentinel_IMU_Shock_Tilt_Touch/
├── Complicated_Projects_Level_2_ESP32_S3/            # ESP32-S3 Dual-Core FreeRTOS Fusion Projects
│   ├── Project_01_FreeRTOS_AHRS_Gimbal_Mahony_WiFi/
│   ├── Project_02_Industrial_PdM_Vibration_Thermal_Acoustic_Fusion/
│   ├── Project_03_Wildfire_Early_Warning_Bayesian_Mesh_Node/
│   └── Project_04_Cyber_Physical_Biometric_Vault_AntiSpoof/
└── docs/                                             # Sphinx source files & build configuration
    ├── source/
    │   ├── img/connect/                              # Wiring schematics & breadboard diagrams
    │   └── img/phenomenon/                           # Sensor hardware reference photos
    └── requirements.txt                              # Sphinx Python documentation dependencies
```

---

## Multi-Sensor Fusion Project Suites

### ⚡ Level 1: Arduino Uno Fusion Projects (ATmega328P @ 16 MHz)
Deterministic, non-blocking state machine sensor fusion within 2 KB SRAM:
1. **Hazard Guardian:** Fuses optical flame (#34), NTC thermistor gradient (#13), and acoustic envelope (#37) to calculate a multi-spectral composite hazard index with hysteresis.
2. **Smart Plant Ecosystem:** Fuses soil electrical conductivity (#42), ambient lux (#14), and air temperature/humidity (#7) to compute dynamic evapotranspiration water demand.
3. **Autonomous Reverse Radar:** Fuses ultrasonic TOF distance (#45) with dual obstacle IR beam sensors (#29) to trigger proportional emergency braking.
4. **Security Sentinel:** Fuses 6-DOF IMU jerk (#40), mechanical vibration (#16), omnidirectional tilt (#21), and capacitive touch (#33) into a multi-tiered security state machine.

### 🚀 Level 2: ESP32-S3 FreeRTOS Fusion Projects (Xtensa LX7 @ 240 MHz)
High-performance dual-core real-time estimation and telemetry:
1. **6-DOF Inertial AHRS & Gimbal:** 100 Hz Mahony quaternion attitude filter, 14-bit servo PWM, and live Core 0 Web telemetry.
2. **Industrial Predictive Maintenance (PdM):** Core 1 500 Hz vibration sampling, double-buffer RMS velocity, statistical kurtosis, and ISO 10816 bearing fault detection.
3. **Wildfire Early Warning Sentinel:** Dual-domain Bayesian probability tree fusing fuel dryness, ambient vapor pressure deficit, and optical combustion indicators.
4. **Biometric Anti-Spoofing Vault:** 250 Hz optical PPG pulse liveness filter, DS1302 time-gating, and 3D seismic tamper detection.

---

## Hardware & Electrical Guidelines (3.3V vs. 5V)

- **Arduino Uno:** Operates on 5V logic. All modules connect directly to 5V and Uno digital/analog pins.
- **ESP32-S3:** Operates on 3.3V logic (ADC maximum input is 3.3V).
  - **Analog Sensors (#13, #14, #17, #20, #42):** Power from the ESP32-S3 **3.3V rail** (not 5V) to protect the ADC inputs.
  - **Relay Module (#30):** The kit 5V active-LOW relay requires a logic level shifter or N-channel MOSFET driver when operated with 3.3V GPIO to ensure the relay optocoupler fully releases when the pin is HIGH.

---

## Base Kit Project Catalog (45 Modules)

| # | Project / Sensor | Category | Code Location | Wiring Diagram | Doc |
|---|---|---|---|---|---|
| 1 | **RGB LED** | Lighting / LED | [`Code/1.Rgb_Led`](./Code/1.Rgb_Led) | [`Diagram`](./docs/source/img/connect/1_bb.png) | [`RST`](./docs/source/1.rgb_led.rst) |
| 2 | **SMD RGB** | Sensor | [`Code/2.Smd_Rgb`](./Code/2.Smd_Rgb) | [`Diagram`](./docs/source/img/connect/2_bb.png) | [`RST`](./docs/source/2.smd_rgb.rst) |
| 3 | **7 Color Flash** | Lighting / LED | [`Code/3.7_Color_Flash`](./Code/3.7_Color_Flash) | [`Diagram`](./docs/source/img/connect/3_bb.png) | [`RST`](./docs/source/3.7_color_flash.rst) |
| 4 | **Laser Module** | Optical & Distance | [`Code/4.Laser_Module`](./Code/4.Laser_Module) | [`Diagram`](./docs/source/img/connect/4_bb.png) | [`RST`](./docs/source/4.laser_module.rst) |
| 5 | **Digital IR Transmitters** | Optical & Distance | [`Code/5.Digital_Ir_Transmitters`](./Code/5.Digital_Ir_Transmitters) | [`Diagram`](./docs/source/img/connect/5_bb.png) | [`RST`](./docs/source/5.ir_transmitters.rst) |
| 6 | **Digital IR Receiver** | Optical & Distance | [`Code/6.Digital_Ir_Receiver1`](./Code/6.Digital_Ir_Receiver1) | [`Diagram`](./docs/source/img/connect/6_bb.png) | [`RST`](./docs/source/6.ir_receiver.rst) |
| 7 | **DHT11 Module** | Environmental | [`Code/7.DHT11`](./Code/7.DHT11) | [`Diagram`](./docs/source/img/connect/7_bb.png) | [`RST`](./docs/source/7.dht11_module.rst) |
| 8 | **Double Color Led** | Lighting / LED | [`Code/8.Double_Color_Led`](./Code/8.Double_Color_Led) | [`Diagram`](./docs/source/img/connect/8_bb.png) | [`RST`](./docs/source/8.double_color_led.rst) |
| 9 | **Mini Double Color Led** | Lighting / LED | [`Code/9.Mini_Double_Color_Led`](./Code/9.Mini_Double_Color_Led) | [`Diagram`](./docs/source/img/connect/9_bb.png) | [`RST`](./docs/source/9.mini_double_color_led.rst) |
| 10 | **Magical Light Cup** | Sensor | [`Code/10.Light_Cup`](./Code/10.Light_Cup) | [`Diagram`](./docs/source/img/connect/10_bb.png) | [`RST`](./docs/source/10.light_cup.rst) |
| 11 | **Active Buzzer** | Audio / Sound | [`Code/11.Active Buzzer`](./Code/11.Active%20Buzzer) | [`Diagram`](./docs/source/img/connect/11_bb.png) | [`RST`](./docs/source/11.active_buzzer.rst) |
| 12 | **Passtive Buzzer** | Audio / Sound | [`Code/12.Passtive Buzzer`](./Code/12.Passtive%20Buzzer) | [`Diagram`](./docs/source/img/connect/12_bb.png) | [`RST`](./docs/source/12.passtive_buzzer.rst) |
| 13 | **Analog Temperature** | Environmental | [`Code/13.Temp`](./Code/13.Temp) | [`Diagram`](./docs/source/img/connect/13_bb.png) | [`RST`](./docs/source/13.analog_temperature.rst) |
| 14 | **Photoresistor Module** | Optical & Distance | [`Code/14.Photoresistor_Module`](./Code/14.Photoresistor_Module) | [`Diagram`](./docs/source/img/connect/14_bb.png) | [`RST`](./docs/source/14.photoresistor_module.rst) |
| 15 | **DS18B20_sensor** | Sensor | [`Code/15.18B20Temp`](./Code/15.18B20Temp) | [`Diagram`](./docs/source/img/connect/15_bb.png) | [`RST`](./docs/source/15.ds18b20_sensor.rst) |
| 16 | **Shock Sensor** | Motion / Vibration | [`Code/16.Shock`](./Code/16.Shock) | [`Diagram`](./docs/source/img/connect/16_bb.png) | [`RST`](./docs/source/16.shock_sensor.rst) |
| 17 | **Pulse Rate Monitor** | Sensor | [`Code/17.Pulse_Rate_Monitor`](./Code/17.Pulse_Rate_Monitor) | [`Diagram`](./docs/source/img/connect/17_bb.png) | [`RST`](./docs/source/17.pulse_rate_monitor.rst) |
| 18 | **Button** | Controls & Switches | [`Code/18.Button`](./Code/18.Button) | [`Diagram`](./docs/source/img/connect/18_bb.png) | [`RST`](./docs/source/18.button.rst) |
| 19 | **Rotary Encoder** | Controls & Switches | [`Code/19.Rotary_Encoder`](./Code/19.Rotary_Encoder) | [`Diagram`](./docs/source/img/connect/19_bb.png) | [`RST`](./docs/source/19.rotary_encoder.rst) |
| 20 | **Joystick** | Controls & Switches | [`Code/20.Joystick`](./Code/20.Joystick) | [`Diagram`](./docs/source/img/connect/20_bb.png) | [`RST`](./docs/source/20.Joystick.rst) |
| 21 | **Tilt switch** | Motion / Vibration | [`Code/21.Tilt_Switch`](./Code/21.Tilt_Switch) | [`Diagram`](./docs/source/img/connect/21_bb.png) | [`RST`](./docs/source/21.tilt_switch.rst) |
| 22 | **KY-035 Hall Sensor** | Magnetic | [`Code/22.Hall_Magnetic_Sensor`](./Code/22.Hall_Magnetic_Sensor) | [`Diagram`](./docs/source/img/connect/22_bb.png) | [`RST`](./docs/source/22.ky-035_hall_sensor.rst) |
| 23 | **TAP Module** | Motion / Vibration | [`Code/23.TAP_Module`](./Code/23.TAP_Module) | [`Diagram`](./docs/source/img/connect/23_bb.png) | [`RST`](./docs/source/23.tap_module.rst) |
| 24 | **KY-003 Hall Sensor** | Magnetic | [`Code/24.Hall_Magnetic_Sensor`](./Code/24.Hall_Magnetic_Sensor) | [`Diagram`](./docs/source/img/connect/24_bb.png) | [`RST`](./docs/source/24.ky-003_hall_sensor.rst) |
| 25 | **Mercury Tilt Switch** | Motion / Vibration | [`Code/25.Mercury_Tilt_Switch`](./Code/25.Mercury_Tilt_Switch) | [`Diagram`](./docs/source/img/connect/25_bb.png) | [`RST`](./docs/source/25.mercury_tilt_switch.rst) |
| 26 | **Reed Switch** | Magnetic | [`Code/26.Reed_Switch`](./Code/26.Reed_Switch) | [`Diagram`](./docs/source/img/connect/26_bb.png) | [`RST`](./docs/source/26.reed_switch.rst) |
| 27 | **Photo Interrupter Module** | Optical & Distance | [`Code/27.Photo_Interrupter_Module`](./Code/27.Photo_Interrupter_Module) | [`Diagram`](./docs/source/img/connect/27_bb.png) | [`RST`](./docs/source/27.photo_interrupter_module.rst) |
| 28 | **Line Tracking Sensor Module** | Optical & Distance | [`Code/28.Line_Tracking_Sensor_Module`](./Code/28.Line_Tracking_Sensor_Module) | [`Diagram`](./docs/source/img/connect/28_bb.png) | [`RST`](./docs/source/28.line_tracking_sensor_module.rst) |
| 29 | **Infrared Obstacle Avoidance Sensor Module** | Optical & Distance | [`Code/29.Infrared_Obstacle_Avoidance_Sensor_Module`](./Code/29.Infrared_Obstacle_Avoidance_Sensor_Module) | [`Diagram`](./docs/source/img/connect/29_bb.png) | [`RST`](./docs/source/29.infrared_obstacle_avoidance_sensor_module.rst) |
| 30 | **Relay** | Controls & Switches | [`Code/30.Relay`](./Code/30.Relay) | [`Diagram`](./docs/source/img/connect/30_bb.png) | [`RST`](./docs/source/30.relay.rst) |
| 31 | **Breadboard Power Supply Module** | Power Supply | [`Code/31.Breadboard_Power_Supply_Module`](./Code/31.Breadboard_Power_Supply_Module) | N/A | [`RST`](./docs/source/31.breadboard_power_supply_module.rst) |
| 32 | **Linear Magnetic Hall Module** | Magnetic | [`Code/32.Linear_Magnetic_Hall_Module`](./Code/32.Linear_Magnetic_Hall_Module) | [`Diagram`](./docs/source/img/connect/32_bb.png) | [`RST`](./docs/source/32.linear_magnetic_hall_module.rst) |
| 33 | **Metal Touch Sensor Module** | Controls & Switches | [`Code/33.Metal_Touch_Sensor_Module`](./Code/33.Metal_Touch_Sensor_Module) | [`Diagram`](./docs/source/img/connect/33_bb.png) | [`RST`](./docs/source/33.metal_touch_sensor_module.rst) |
| 34 | **Flame Sensor Module** | Optical & Distance | [`Code/34.Flame_Sensor_Module`](./Code/34.Flame_Sensor_Module) | [`Diagram`](./docs/source/img/connect/34_bb.png) | [`RST`](./docs/source/34.flame_sensor_module.rst) |
| 35 | **Digital Temperature Sensor Module** | Environmental | [`Code/35.Digital_Temperature_Sensor_Module`](./Code/35.Digital_Temperature_Sensor_Module) | [`Diagram`](./docs/source/img/connect/35_bb.png) | [`RST`](./docs/source/35.digital_temperature_sensor_module.rst) |
| 36 | **Reed Switch Module** | Magnetic | [`Code/36.Reed_Switch_Module`](./Code/36.Reed_Switch_Module) | [`Diagram`](./docs/source/img/connect/36_bb.png) | [`RST`](./docs/source/36.reed_switch_module.rst) |
| 37 | **High Sensitivity Sound Detection Module** | Audio / Sound | [`Code/37.High_Sensitivity_Sound_Detection_Module`](./Code/37.High_Sensitivity_Sound_Detection_Module) | [`Diagram`](./docs/source/img/connect/37_bb.png) | [`RST`](./docs/source/37.high_sensitivity_sound_detection_module.rst) |
| 38 | **Microphone Sound Sensor Module** | Audio / Sound | [`Code/38.Microphone_Sound_Sensor_Module`](./Code/38.Microphone_Sound_Sensor_Module) | [`Diagram`](./docs/source/img/connect/38_bb.png) | [`RST`](./docs/source/38.microphone_sound_sensor_module.rst) |
| 39 | **Water Level Sensor** | Environmental | [`Code/39.Water_Level_Sensor`](./Code/39.Water_Level_Sensor) | [`Diagram`](./docs/source/img/connect/39_bb.png) | [`RST`](./docs/source/39.water_level_sensor.rst) |
| 40 | **GY521 Module** | Modules & Storage | [`Code/40.GY521_Module`](./Code/40.GY521_Module) | [`Diagram`](./docs/source/img/connect/40_bb.png) | [`RST`](./docs/source/40.gy521_module.rst) |
| 41 | **MP1854EN** | Power Supply | [`Code/41.MP1854EN`](./Code/41.MP1854EN) | N/A | [`RST`](./docs/source/41.mp1854en.rst) |
| 42 | **Soil Moisture Sensor** | Environmental | [`Code/42.Soil_Moisture_Sensor`](./Code/42.Soil_Moisture_Sensor) | [`Diagram`](./docs/source/img/connect/42_bb.png) | [`RST`](./docs/source/42.soil_moisture_sensor.rst) |
| 43 | **Ds1302 Rtc Module** | Modules & Storage | [`Code/43.Ds1302_Rtc_Module`](./Code/43.Ds1302_Rtc_Module) | [`Diagram`](./docs/source/img/connect/43_bb.png) | [`RST`](./docs/source/43.ds1302_rtc_module.rst) |
| 44 | **SD Card Info** | Modules & Storage | [`Code/44.SD_Card_Info`](./Code/44.SD_Card_Info) | [`Diagram`](./docs/source/img/connect/44_bb.png) | [`RST`](./docs/source/44.sd_card_info.rst) |
| 45 | **Ultrasonic Module** | Optical & Distance | [`Code/45.Ultrasonic_Module`](./Code/45.Ultrasonic_Module) | [`Diagram`](./docs/source/img/connect/45_bb.png) | [`RST`](./docs/source/45.ultrasonic_module.rst) |

---

## Toolchain & Compilation Requirements

### 1. Arduino AVR Uno Core (Level 1 & Base Kit)
- **Core:** `arduino:avr` (version ≥ 1.8.6)
- **Compiler Command:** `arduino-cli compile -b arduino:avr:uno <sketch_folder>`
- **Optional Libraries:**
  - OneWire & DallasTemperature (Module #15)
  - SimpleDHT (Module #7)
  - IRremote (Module #6)
  - RtcDS1302 (Module #43)
  - SD (Module #44)

### 2. ESP32-S3 Core (Level 2 Fusion Projects)
- **Core:** `esp32:esp32` (version ≥ 3.0.0 for LEDC 3.x API support)
- **Target Board:** ESP32-S3 Dev Module (`esp32:esp32:esp32s3`)
- **Built-in FreeRTOS:** Dual-core preemptive scheduling with high-resolution 14-bit timer PWM.
