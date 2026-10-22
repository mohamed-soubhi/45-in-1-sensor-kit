#!/usr/bin/env python3
"""
sync_hub.py - Synchronize embedded Arduino sketches in index.html with the source .ino files.
Prevents index.html from drifting when sketches are modified or fixed.
"""

import os
import re
import html
from pathlib import Path

# Mapping of pre block IDs to their respective source .ino files
SKETCH_MAPPINGS = {
    # Level 2 ESP32-S3 Projects
    "codeContent_l2_1": Path("Complicated_Projects_Level_2_ESP32_S3/Project_01_FreeRTOS_AHRS_Gimbal_Mahony_WiFi/Project_01_FreeRTOS_AHRS_Gimbal_Mahony_WiFi.ino"),
    "codeContent_l2_2": Path("Complicated_Projects_Level_2_ESP32_S3/Project_02_Industrial_PdM_Vibration_Thermal_Acoustic_Fusion/Project_02_Industrial_PdM_Vibration_Thermal_Acoustic_Fusion.ino"),
    "codeContent_l2_3": Path("Complicated_Projects_Level_2_ESP32_S3/Project_03_Wildfire_Early_Warning_Bayesian_Mesh_Node/Project_03_Wildfire_Early_Warning_Bayesian_Mesh_Node.ino"),
    "codeContent_l2_4": Path("Complicated_Projects_Level_2_ESP32_S3/Project_04_Cyber_Physical_Biometric_Vault_AntiSpoof/Project_04_Cyber_Physical_Biometric_Vault_AntiSpoof.ino"),

    # Level 1 Uno Projects
    "codeContent_fusion_1": Path("Complicated_Projects_Level_1/Project_01_Hazard_Guardian_Fire_Thermal_Sound/Project_01_Hazard_Guardian_Fire_Thermal_Sound.ino"),
    "codeContent_fusion_2": Path("Complicated_Projects_Level_1/Project_02_Smart_Plant_Ecosystem_Soil_Water_Light_Temp/Project_02_Smart_Plant_Ecosystem_Soil_Water_Light_Temp.ino"),
    "codeContent_fusion_3": Path("Complicated_Projects_Level_1/Project_03_Autonomous_Proximity_Radar_Ultrasonic_IR_Encoder/Project_03_Autonomous_Proximity_Radar_Ultrasonic_IR_Encoder.ino"),
    "codeContent_fusion_4": Path("Complicated_Projects_Level_1/Project_04_Security_Sentinel_IMU_Shock_Tilt_Touch/Project_04_Security_Sentinel_IMU_Shock_Tilt_Touch.ino"),

    # 42 Base Kit Sketches
    "codeContent_1": Path("Code/1.Rgb_Led/1.Rgb_Led.ino"),
    "codeContent_2": Path("Code/2.Smd_Rgb/2.Smd_Rgb.ino"),
    "codeContent_4": Path("Code/4.Laser_Module/4.Laser_Module.ino"),
    "codeContent_5": Path("Code/5.Digital_Ir_Transmitters/5.Digital_Ir_Transmitters.ino"),
    "codeContent_6": Path("Code/6.Digital_Ir_Receiver2/6.Digital_Ir_Receiver2.ino"),
    "codeContent_7": Path("Code/7.DHT11/DHT11/DHT11.ino"),
    "codeContent_8": Path("Code/8.Double_Color_Led/8.Double_Color_Led.ino"),
    "codeContent_9": Path("Code/9.Mini_Double_Color_Led/9.Mini_Double_Color_Led.ino"),
    "codeContent_10": Path("Code/10.Light_Cup/10.Light_Cup.ino"),
    "codeContent_11": Path("Code/11.Active Buzzer/Active_buzzer_Module/Active_buzzer_Module.ino"),
    "codeContent_12": Path("Code/12.Passtive Buzzer/Passtive_Buzzer/Passtive_Buzzer.ino"),
    "codeContent_13": Path("Code/13.Temp/temp/temp.ino"),
    "codeContent_14": Path("Code/14.Photoresistor_Module/Photoresistor_Module/Photoresistor_Module.ino"),
    "codeContent_15": Path("Code/15.18B20Temp/DS18B20_sensor_module_1/DS18B20_sensor_module_1.ino"),
    "codeContent_16": Path("Code/16.Shock/shock/shock.ino"),
    "codeContent_17": Path("Code/17.Pulse_Rate_Monitor/17.Pulse_Rate_Monitor.ino"),
    "codeContent_18": Path("Code/18.Button/18.Button.ino"),
    "codeContent_19": Path("Code/19.Rotary_Encoder/19.Rotary_Encoder.ino"),
    "codeContent_20": Path("Code/20.Joystick/20.Joystick.ino"),
    "codeContent_21": Path("Code/21.Tilt_Switch/21.Tilt_Switch.ino"),
    "codeContent_22": Path("Code/22.Hall_Magnetic_Sensor/22.Hall_Magnetic_Sensor.ino"),
    "codeContent_23": Path("Code/23.TAP_Module/23.TAP_Module.ino"),
    "codeContent_24": Path("Code/24.Hall_Magnetic_Sensor/24.Hall_Magnetic_Sensor.ino"),
    "codeContent_25": Path("Code/25.Mercury_Tilt_Switch/25.Mercury_Tilt_Switch.ino"),
    "codeContent_26": Path("Code/26.Reed_Switch/26.Reed_Switch.ino"),
    "codeContent_27": Path("Code/27.Photo_Interrupter_Module/27.Photo_Interrupter_Module.ino"),
    "codeContent_28": Path("Code/28.Line_Tracking_Sensor_Module/28.Line_Tracking_Sensor_Module.ino"),
    "codeContent_29": Path("Code/29.Infrared_Obstacle_Avoidance_Sensor_Module/29.Infrared_Obstacle_Avoidance_Sensor_Module.ino"),
    "codeContent_30": Path("Code/30.Relay/30.Relay.ino"),
    "codeContent_32": Path("Code/32.Linear_Magnetic_Hall_Module/32.Linear_Magnetic_Hall_Module.ino"),
    "codeContent_33": Path("Code/33.Metal_Touch_Sensor_Module/33.Metal_Touch_Sensor_Module.ino"),
    "codeContent_34": Path("Code/34.Flame_Sensor_Module/34.Flame_Sensor_Module.ino"),
    "codeContent_35": Path("Code/35.Digital_Temperature_Sensor_Module/35.Digital_Temperature_Sensor_Module.ino"),
    "codeContent_36": Path("Code/36.Reed_Switch_Module/36.Reed_Switch_Module.ino"),
    "codeContent_37": Path("Code/37.High_Sensitivity_Sound_Detection_Module/37.High_Sensitivity_Sound_Detection_Module.ino"),
    "codeContent_38": Path("Code/38.Microphone_Sound_Sensor_Module/38.Microphone_Sound_Sensor_Module.ino"),
    "codeContent_39": Path("Code/39.Water_Level_Sensor/39.Water_Level_Sensor.ino"),
    "codeContent_40": Path("Code/40.GY521_Module/40.GY521_Module.ino"),
    "codeContent_42": Path("Code/42.Soil_Moisture_Sensor/42.Soil_Moisture_Sensor.ino"),
    "codeContent_43": Path("Code/43.Ds1302_Rtc_Module/43.Ds1302_Rtc_Module.ino"),
    "codeContent_44": Path("Code/44.SD_Card_Info/44.SD_Card_Info.ino"),
    "codeContent_45": Path("Code/45.Ultrasonic_Module/45.Ultrasonic_Module.ino"),
}

def sync_hub():
    hub_path = Path("index.html")
    if not hub_path.exists():
        print("Error: index.html not found.")
        return

    content = hub_path.read_text(encoding="utf-8")
    updated_count = 0

    for code_id, ino_path in SKETCH_MAPPINGS.items():
        if not ino_path.exists():
            print(f"Warning: Source file {ino_path} does not exist.")
            continue

        raw_code = ino_path.read_text(encoding="utf-8")
        escaped_code = html.escape(raw_code)

        pattern = rf'(<pre\s+id="{re.escape(code_id)}"><code>)[\s\S]*?(</code></pre>)'
        new_block = rf'\g<1>{escaped_code}\g<2>'

        content, count = re.subn(pattern, new_block, content, count=1)
        if count > 0:
            updated_count += 1
        else:
            print(f"Warning: Block id {code_id} not found in index.html")

    hub_path.write_text(content, encoding="utf-8")
    print(f"Successfully synchronized {updated_count}/{len(SKETCH_MAPPINGS)} sketches into index.html.")

if __name__ == "__main__":
    sync_hub()
