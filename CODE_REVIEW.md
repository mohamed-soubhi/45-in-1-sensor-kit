# Code Review: Hosyond 45-in-1 Sensor Kit Repository

| | |
|---|---|
| **Date** | 2026-10-22 |
| **Revision reviewed** | `0ab6ffc` (branch `master`) |
| **Scope** | Whole repository: 45 kit sketches (`Code/`), Level 1 Uno projects, Level 2 ESP32-S3 projects, HTML hub and per-project pages, Sphinx docs, and repository hygiene |
| **Method** | Line-by-line reading of every `.ino`; cross-checking sensor polarity and wiring against the kit's own reference sketches and `.rst` docs; `arduino-cli compile -b arduino:avr:uno` on every Uno sketch; a scripted relative-link check over all `.html`/`.md` files |

---

## 1. Executive summary

The kit sketches mostly work, but they're vendor-quality code with a few real bugs. The bigger problems are in the **Level 1 and Level 2 "multi-sensor fusion" projects**, which build cleanly on Uno but have logic errors that would stop them from working on real hardware:

* **Some Level 2 sketches can't work as written.** The vault project (L2-P4) goes into a permanent tamper alarm as soon as an MPU-6050 is connected. The gimbal project (L2-P1) never drives its RGB LED or servos. The PdM project (L2-P2) reads room temperature as about 100–165 °C and trips its relay right away.
* **Four Level 1 and Level 2 projects read sensors with the opposite polarity from the kit's own reference sketches.** These are the soil moisture, photoresistor, NTC thermistor and tilt switch. The result is that the plant pump runs when the soil is wet, the anti-scorch logic triggers in the dark, the thermal-rise channel never fires, and the security sentinel stays in "tamper warning" while upright.
* **Two kit sketches don't compile as shipped** (#37 and #38), because each folder holds a stray `*_copy_*.ino` duplicate. The GY-521 sketch (#40) shows temperature in place of gyro X, and each gyro axis after it is shifted by one.
* **Repository hygiene:** about 199 MB of generated Sphinx output is committed, including pickles and every image a second time. There are 18 broken links in the per-project pages, no `.gitignore`, and no CI compile check. A CI compile check would have caught several of the issues above.

### Compile results (`arduino:avr:uno`, core 1.8.8)

| Result | Sketches |
|---|---|
| ✅ Pass (39) | All other `Code/*` sketches and all 4 Level 1 projects (6–9 KB flash, 18–28 %) |
| ❌ **Fail: duplicate definitions** | `Code/37.*` and `Code/38.*` (`redefinition of 'void setup()'`) |
| ⚠️ Not verified: library not installed | #6 (IRremote), #7 (SimpleDHT), #15 (OneWire), #43 (RtcDS1302), #44 (SD) |
| ⚠️ Not verified: ESP32 core not installed | All 4 Level 2 ESP32-S3 projects (findings for these come from reading the code) |

### Findings by severity

| Severity | Count | Meaning |
|---|---|---|
| 🔴 Critical | 6 | Doesn't compile, or can't do its main job on real hardware |
| 🟠 High | 6 | Main feature behaves wrongly under normal conditions |
| 🟡 Medium | 12 | Real defect with limited impact, races, or features the docs describe but the code doesn't implement |
| 🔵 Low | 14 | Robustness, clarity, misleading comments |
| ⚙️ Repo / docs | 12 | Hygiene, links, build, licensing |

---

## 2. Critical findings

### C1 — L2-P4 Vault: tamper baseline is never calibrated, so the alarm never clears
`Complicated_Projects_Level_2_ESP32_S3/Project_04_.../Project_04_...AntiSpoof.ino:84, 249-257`

`baselineAx/Ay/Az` start at `0` and are never set. `accelJerk` is therefore `|a|`, which is about **1.0 g at rest** (gravity), well above the `0.70 g` threshold. Once the MPU-6050 responds, `currentVaultState` is forced to `VAULT_TAMPER_ALARM` every 50 ms. The siren runs constantly and access can never be granted.
**Fix:** average N samples in `setup()` after the wake write (as L1-P4 does), or compare `| |a| − 1 g |` and leave out the baseline.

### C2 — L2-P1 Gimbal: `ledcWrite()` is given channel numbers, but the v3 API expects pins
`Project_01_FreeRTOS_AHRS_Gimbal_Mahony_WiFi.ino:296-306` vs `349-353`

The setup uses the Arduino-ESP32 **3.x** API (`ledcAttach(pin, freq, bits)`). In 3.x, `ledcWrite()` takes a **pin**, not a channel. `ledcWrite(0..4, …)` writes to GPIO 0–4, none of which were attached; GPIO 4 is the joystick ADC input. **The RGB LED and both gimbal outputs are never driven.**
**Fix:** `ledcWrite(PIN_RGB_RED, …)`, `ledcWrite(PIN_GIMBAL_ROLL, …)` and so on.

### C3 — L2-P1 Gimbal: servo PWM setup is invalid for the ESP32-S3, and the pulse width maths is wrong
`Project_01_...ino:294-297, 352-353`

1. `ledcAttach(..., 50, 16)`: the ESP32-S3 LEDC timer supports at most **14-bit** resolution, so attaching at 16 bits fails.
2. Even at a valid resolution, writing `1000…2000` as a raw duty value doesn't produce 1–2 ms pulses. At 50 Hz the period is 20 ms, so the duty value must be `us × (2^bits − 1) / 20000`. With 16-bit values, `1000…2000` would give 0.3–0.6 ms pulses, which is outside the servo range.

**Fix:** use 14 bits and `duty = us * 16383 / 20000`, or use the `ESP32Servo` library.

### C4 — L2-P2 PdM: the LM35 formula is applied to the kit's NTC thermistor module, so the motor trips at power-on
`Project_02_Industrial_PdM_...ino:103-108, 245`

Kit module #13 is an **NTC thermistor divider** (see `docs/source/13.analog_temperature.rst`), not an LM35. `voltage × 100` gives about 100–165 °C at room temperature (mid-rail divider at 3.3 V). That is above the `> 75 °C` rule, so `ZONE_D_UNACCEPTABLE` and the relay trip fire immediately and stay on.
**Fix:** use the same Steinhart–Hart conversion as the kit sketch, with the correct divider orientation and a 3.3 V / 12-bit ADC scale.

### C5 — Kit #37 and #38 don't compile: duplicate `*_copy_*.ino` files
`Code/37.High_Sensitivity_Sound_Detection_Module/*_copy_20240524112919.ino`, `Code/38.Microphone_Sound_Sensor_Module/*_copy_20240524112949.ino`

The Arduino IDE and arduino-cli compile **every** `.ino` in a sketch folder. The copies are byte-identical to the main sketch, so the build fails with `redefinition of 'void setup()'` (confirmed with arduino-cli). Separately, the main file names (`37_…`) don't match the folder names (`37.…`), so the IDE prompts to move the files.
**Fix:** delete both `_copy_` files and rename each folder or `.ino` so the names match.

### C6 — Kit #40 GY-521: gyro values are shifted because the temperature registers aren't skipped
`Code/40.GY521_Module/40.GY521_Module.ino:31-36`

The sketch requests 14 bytes from `0x3B` but reads only 12 and never skips `TEMP_OUT_H/L` (`0x41–0x42`). So `gyro_x` actually contains **temperature**, `gyro_y` contains gyro X, and `gyro_z` contains gyro Y. The comments say the opposite. The same bug is in `docs/source/40.gy521_module.rst`.
**Fix:** read `temperature = Wire.read()<<8 | Wire.read();` between the accelerometer and gyro reads. L2-P1 line 250 does this correctly.

---

## 3. High findings

### H1 — Soil moisture polarity is inverted compared with kit #42 (L1-P2, L2-P3)
* `Level_1/Project_02_...ino:28, 38-39, 183-184`: assumes `0 = dry, 1023 = wet` and waters when `raw < 350`.
* `Level_2/Project_03_...ino:197-198`: `map(raw, 1000, 3500, 0, 100)` is treated as % *moisture*.

The kit's reference sketch (`Code/42…`, `docs/source/42.soil_moisture_sensor.rst`) treats **high ADC as dry** (`>= 800`, "Time to water your plant"). As written, **the pump runs on wet soil and never on dry soil**, and the wildfire fuel-dryness factor is reversed.

### H2 — Photoresistor polarity is inverted compared with kit #14 (L1-P2)
`Level_1/Project_02_...ino:30, 41, 185`: assumes `1023 = intense sun`. Kit sketch #14 inverts the raw value (`value = 1023 − analogRead`) because the module reads **high in the dark**. The "anti-scorch" interlock therefore suspends watering at night and allows it under direct sun.

### H3 — L1-P1 Hazard Guardian: NTC divider orientation is the opposite of kit #13, so the thermal channel never fires
`Level_1/Project_01_...ino:95-116, 119-133`

The project computes `R = 10k·(1023/raw − 1)` (thermistor on the high side). Kit sketch #13 uses `R = 10k / (1023/raw − 1)` (thermistor on the low side). With the kit module, heating makes the computed temperature *fall*. `calculateTemperatureRate()` clamps negative rates to 0, so **the 35 %-weighted thermal-rise channel never contributes**. The fallback branch (`raw × 500/1024`, "LM35") returns 0–500 °C for a thermistor and should be removed.

### H4 — L1-P4 Security Sentinel: tilt polarity is inverted compared with kit #21, so it is "tilted" while upright
`Level_1/Project_04_...ino:178, 210`

Kit #21 reads **LOW when tilted**. The project treats `HIGH` as tilted. Upright, the device reads `isTilted = true` on every poll and stays in `SEC_TAMPER_WARNING`. Combined with the `imuJerk > 0.35` OR, any tilt of 20° or more also counts as "severe".

### H5 — L2-P4 Vault: liveness stays latched, so a held finger re-grants access repeatedly
`Level_2/Project_04_...ino:194-207, 285, 315-323`

`currentBio.livenessConfirmed` is only cleared when the finger leaves the sensor. After `ACCESS_GRANTED` returns to `STANDBY`, a finger still on the sensor goes straight back to `MEASURING_PPG`, finds `livenessConfirmed == true`, and **unlocks again** every ~5 s without a new measurement. Related problems:
* `BiometricResult bio;` (`:264`) is uninitialised if the mutex take times out, which is undefined behaviour in a security decision.
* The "high-pass" EMA (`α = 0.15` at 250 Hz, corner ≈ 6 Hz) removes most of the 1–2 Hz cardiac component, so peaks above `120` come mainly from noise or motion.
* "4 consecutive beats" isn't enforced, because invalid IBIs don't reset `peakCount`.
* Any periodic optical source at 50–150 BPM passes the "anti-spoofing" check, so the README/HTML claims about rejecting silicone and 3D molds aren't supported.

### H6 — Level 2: 3.3 V GPIO drives 5 V active-LOW relay modules, and the 5 V analog modules need care
All four Level 2 projects drive kit relay #30 (5 V, active-LOW, opto/transistor input) straight from ESP32-S3 GPIO. With the module on 5 V, a 3.3 V "HIGH" often **doesn't release** the relay, so the "safe" state can be energised. Analog modules powered from 5 V can also put more than 3.3 V on ADC pins. The wiring tables say "3.3 V logic" but don't cover relay VCC/JD-VCC or level shifting.
**Fix:** document powering the sensors from 3.3 V, and use a transistor or level shifter for the relay (or a 3.3 V-compatible relay board).

---

## 4. Medium findings

| # | Location | Issue |
|---|---|---|
| M1 | L1-P4 `:224-238, 271-284` | `SEC_TAMPER_WARNING` is re-evaluated on the next 50 ms poll and drops back to `ARMED_NORMAL`. The 150 ms chirp and the 3 s auto-return are effectively dead code, so the amber warning just flickers for 50 ms. With a sustained touch, `alarmStartTime` is reset every poll and the buzzer stays on. |
| M2 | L1-P1 `:221-224, 237-288` | No hysteresis or latch on the state machine: the EMERGENCY relay can chatter around the 0.80 threshold. `soundTriggerCount` (4-byte, ISR-shared) is read and cleared without `noInterrupts()`, which is a torn read/write race on AVR. Each sound event affects only one loop pass. |
| M3 | L2-P2 `:70-76, 165-192` | Described as a "double buffer", but it is a single buffer the sampler keeps overwriting while the diagnostics task reads it (race across cores). The mean is computed on the signed `mag` but the variance and kurtosis on `abs(mag)`, which biases both statistics. |
| M4 | L2-P1 `:212-219, 239-256, 311` | `currentAttitude` is written on core 1 and read by the web handler on core 0 with no lock. `telemetryQueue` is written but never read, and `i2cMutex` has only one user, so the "thread-safe IPC" claim doesn't hold. `axRaw…gzRaw` are uninitialised if the mutex or I2C read fails, which feeds garbage into the filter. |
| M5 | L1-P2 `:184, 189-193` | `waterDemandFactor` (the "evapotranspiration index") and `isSoilSaturated` (the "hysteresis") are computed but never used. Watering is a fixed 5 s burst. |
| M6 | L2-P1/P2/P3 | Features described in headers and HTML pages that the code doesn't implement: encoder trim, joystick switch and buzzer (P1, pins declared but never configured); web stream and crest-factor fusion (P2); FFMC/EMC model and "50 Hz" sampling (P3, which runs at 10 Hz); "cryptographic state machine" (P4). |
| M7 | L2-P4 `:118-136` | DS1302 driver never clears the Clock-Halt bit or sets the time. A new module reads `00:00`, so access is always denied. The default `dt` values are overwritten even when the read fails. |
| M8 | L2-P3 `:84-118, 185` | DHT11 bit-bang runs without a critical section on a preemptive RTOS, so you get sporadic checksum failures. The return value is ignored, so stale values are shown as live. |
| M9 | `Code/19` `:14-34` | `Serial.println` inside the ISR; the 200 ms "debounce" drops steps during normal rotation; `count` (a `long`) is reset in `loop()` without disabling interrupts; `attachInterrupt` runs before `pinMode` and uses a hard-coded interrupt number instead of `digitalPinToInterrupt`. |
| M10 | `Code/15` `:45-62` | No wait (≥750 ms) after `CONVERT T`, so the sketch reads the previous value or the 85 °C power-on default. DS18**S**20 (`0x10`) scaling is wrong (it should be `/2`, not `/16`). `present` is unused. |
| M11 | `Code/45` `:16-17` | `int distance = pulseIn(...)` overflows a 16-bit `int` on timeout (the default timeout is 1 s, returning up to 1,000,000). There is no explicit timeout, and "no echo" prints `0`. |
| M12 | `Code/6.*` | Uses the IRremote **2.x** API (`decode(&results)`, `results.value`), which current IRremote 4.x has removed, so it won't compile against the current library. `LED_PIN` is never set as `OUTPUT`. The folder `6.Digital_Ir_Receiver1` actually contains the **transmitter**. |

---

## 5. Low findings

* **`Wire.read()<<8 | Wire.read()`** (#40, L1-P4, L2-P1/P2/P4): the two operands of `|` are unsequenced, so C++ doesn't guarantee which read happens first. GCC happens to evaluate left-to-right. Use two statements to be safe.
* **RGB pin mapping** (L1-P1, L1-P4): uses R/G/B = 9/10/11, while the kit diagram and sketch #1 use R=11, B=10, G=9. If you wire it per the kit, the colours come out wrong.
* **`Code/1` and `Code/2`**: `analogWrite(9, 128 − val)` goes negative and wraps, so the green channel jumps. The sketches also ignore their own pin variables. #1/#2 and #8/#9 are byte-identical (expected from the vendor, but worth noting).
* **`Code/12` passive buzzer** `:17-19`: the second tone loop toggles HIGH→LOW with no delay between, giving roughly 4 µs pulses that are nearly silent. There's also a redundant `while(1)` inside `loop()`.
* **`Code/13`** `:12`: divides by zero when `Vo == 0`.
* **`Code/17` pulse monitor**: `ledPin`, `change` and `oldChange` are unused, and no BPM is computed despite the name.
* **`Code/20` joystick**: a stray `pinMode(3, OUTPUT)`; the switch is never read.
* **`Code/30` relay**: the comment says "Pin3" but the code uses pin 8. Whether HIGH means on or off depends on the module's active level.
* **`Code/44` SD**: a stray, unused `SD.h` (SparkFun GPLv3 header that references a missing `utility/SdFat.h`) sits in the sketch folder. The sketch doesn't check that `SD.open` succeeded and always prints "Writing to file successful". Typo: "exits".
* **`Code/7` DHT11**: depends on the bundled 2016 `SimpleDHT.zip` (the `read(pin, …)` API). Pin the library version or update the call.
* **L1-P3**: the comments give 2 Hz and 8 Hz beeps, but the toggle intervals actually produce about 1.25 Hz and 4 Hz. The header says the relay is "energized (drive enabled)" but the code de-energises it. For a brake, consider fail-safe polarity.
* **L2-P3** `:221`: a flame reading above 60 % alone triggers EXTREME and the sprinkler, bypassing the fusion logic. Direct sunlight can trip it.
* **L2-P4** `:146-155`: `playChime()` uses `delay()` in the security task, pausing tamper monitoring for about 0.5 s.
* **L2 projects**: Wi-Fi AP passwords are hard-coded (`fusion123`, `wildfire123`). That's fine for a demo, but the docs should say so. Several page labels are inaccurate, e.g. "YAW (Z-Axis Rate)" displays an angle, and yaw from a 6-DOF IMU drifts.

---

## 6. Repository, documentation & build

| # | Finding | Recommendation |
|---|---|---|
| R1 | **`docs/build/` (≈199 MB) is committed**, including `.doctrees/*.doctree`, `environment.pickle`, and a second copy of every image under `_images/`. | Add a `.gitignore` for `docs/build/` and rebuild locally or on RTD. If offline HTML must ship, publish it as a release asset or `gh-pages` instead. |
| R2 | Source photos are 4–5 MB JPEGs (`docs/source/img/phenomenon`, ≈191 MB total). | Re-encode to about 1600 px / quality 80 (≈200–300 KB each), or move them to Git LFS. |
| R3 | No `.gitignore`. `docs/source/.vscode/settings.json` is committed. | Add one (build output, `.vscode/`, `*.bak`, etc.). |
| R4 | **18 broken relative links** in `Code/*/index.html`: wrong doc slugs (`11.active buzzer.html`, `13.temp.html`, `15.18b20temp.html`, `16.shock.html`, `22/24.hall_magnetic_sensor.html`, `5.digital_ir_transmitters.html`, `6.digital_ir_receiver1/2.html`, `7.dht11.html`) and `.ino` links that skip the sub-folder (#11, #12, #13, #14, #15, #16, #7). | Generate the links from the real file list (see R9) and add a link check to CI. |
| R5 | The root `README.md` doesn't mention `Complicated_Projects_Level_1/2` or the root `index.html` hub. `[Code/11.Active Buzzer](./Code/11.Active Buzzer)` breaks because of the unencoded space. | Add a "Projects" section and encode spaces as `%20`, or rename the folders. |
| R6 | Level 1 has a `README.md` at folder and project level; Level 2 has HTML only. | Keep the formats consistent. |
| R7 | Duplicate assets: `29(1).jpg` is identical to `29.jpg`, plus the `_copy_` sketches (C5). | Delete them. |
| R8 | `docs/requirements.txt` is **UTF-16 LE**, and `docs/.readthedocs.yaml` is not at the repo root, where Read the Docs looks for it. `sphinx_conestack_theme` and `readthedocs-sphinx-search` are listed but not used by `conf.py`. | Save the file as UTF-8, move the RTD config to the root, and trim unused dependencies. |
| R9 | `index.html` (310 KB, ~7,000 lines) embeds every sketch inline. It will silently drift from the `.ino` files: fixing C6 in the `.ino` won't update the hub. | Generate the hub from the sources with a small script, and commit both the script and its output. |
| R10 | **Licensing:** the root `LICENSE` is GPL-2.0, `Code/44/SD.h` is GPL-3.0, and the docs and photos are vendor content. The README describes the repo as a "clone". | Confirm redistribution rights for the vendor docs and images, and record the licence of each part. |
| R11 | **No CI.** An `arduino-cli compile` matrix (`arduino:avr:uno` for `Code/` and Level 1, `esp32:esp32:esp32s3` for Level 2) would have caught C2, C3 and C5, and the library-API breakage in M12. | Add a GitHub Actions workflow with `arduino/compile-sketches` and pinned library versions. |
| R12 | Toolchain requirements aren't documented. Level 2 needs **arduino-esp32 ≥ 3.0** (`ledcAttach`), and the kit sketches need OneWire, IRremote (2.x API), SimpleDHT, RtcDS1302 (Makuna) and SD. | Add a "Requirements" table with exact versions, or a `sketch.yaml` / `arduino-cli` profile. |

---

## 7. What's done well

* Level 1 projects are written non-blocking (`millis()` scheduling) with clear state machines, `F()` strings to save SRAM, and consistent pin tables. They fit comfortably on an Uno (18–28 % of flash).
* L2-P1's Mahony filter is a faithful port of the reference implementation, including correct Euler extraction and clamping of `asin`.
* L1-P3's ultrasonic code uses a bounded `pulseIn` timeout and an EMA filter, which is the right pattern and better than kit #45.
* Pages are self-contained and work offline. The per-project HTML includes wiring tables and theory sections that are useful for teaching.

---

## 8. Recommended action plan

1. **Make it build:** delete the `_copy_` sketches and fix the folder/sketch name mismatches (C5). Add CI compile checks (R11).
2. **Fix correctness in the Level 2 sketches:** C1, C2, C3, C4 and H5, and add the relay/ADC voltage guidance (H6).
3. **Align sensor polarity with the kit:** H1–H4. Consider a small shared `kit_sensors.h` with one tested read function per module (e.g. `readSoilDrynessPct()`, `readNtcC()`, `isTilted()`) so every project reads each module the same way.
4. **Fix the kit sketches:** C6 (GY-521), M9 (encoder), M10 (DS18B20), M11 (ultrasonic), M12 (IRremote). Fix the same code in the `.rst` docs and the HTML hub.
5. **Make the docs match the code:** remove or implement the features listed in M5 and M6, and tone down the security claims in L2-P4.
6. **Repo hygiene:** add a `.gitignore`, stop tracking `docs/build/`, compress the images, fix the 18 broken links, and update the README (R1–R9).
7. **Licensing check:** R10.
