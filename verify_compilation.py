import subprocess
from pathlib import Path

# Sketches known to require external libraries not shipped in the core
SKIP_EXTERNAL_LIB = {
    "6.Digital_Ir_Receiver1",
    "6.Digital_Ir_Receiver2",
    "7.DHT11",
    "15.18B20Temp",
    "43.Ds1302_Rtc_Module",
    "44.SD_Card_Info",
}

targets = []

# Code sketches
for p in Path("Code").iterdir():
    if p.is_dir() and p.name not in SKIP_EXTERNAL_LIB:
        inos = list(p.rglob("*.ino"))
        if inos:
            targets.append((p.name, inos[0].parent))

# Level 1 sketches
for p in Path("Complicated_Projects_Level_1").iterdir():
    if p.is_dir():
        inos = list(p.rglob("*.ino"))
        if inos:
            targets.append((p.name, inos[0].parent))

print(f"Testing compilation for {len(targets)} sketches...")
passed = 0
failed = []

for name, folder in sorted(targets):
    cmd = ["arduino-cli", "compile", "-b", "arduino:avr:uno", str(folder)]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode == 0:
        print(f"  [PASS] {name}")
        passed += 1
    else:
        print(f"  [FAIL] {name}: {res.stderr.strip() or res.stdout.strip()[:200]}")
        failed.append(name)

print(f"\nCompile Verification Summary: {passed} passed, {len(failed)} failed.")
if failed:
    exit(1)
