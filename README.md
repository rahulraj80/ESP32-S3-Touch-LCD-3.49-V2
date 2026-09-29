# Waveshare ESP32-S3-Touch-LCD-3.49 (V2)

This repository contains the enhanced ESP-IDF factory firmware for the **Waveshare ESP32-S3-Touch-LCD-3.49 (Rev 1.1)** with an interactive **Options & Diagnostics Panel** and an **ADB-like USB Serial Remote Control Interface**.

---

## 🚀 Key Features & Enhancements

- **ADB-like USB Remote Interface (`remote_ui_control.cpp`):**
  - Inject simulated touch taps and swipes over serial (`tap <x> <y>`, `swipe <x1> <y1> <x2> <y2>`)
  - Change carousel pages (`page 0|1|2`, `next`, `prev`)
  - Backlight control and power-cut (`bl <0-255>`, `dark`, `light`) **IMP** : <u>**_Setting BL to 25% via Page 2, or setting it to below 92 (0-255 scale) will keep the screen too dark to be visible. Remember the positions of the other BL values, and tap on dark screen or change BL over USB with `remote_ui_control.cpp` to correct._**</u>
  - Test pattern injection (`color <red|green|blue|white|black|none>`)
  - Real-time JSON telemetry status (`status`)
- **Options & Diagnostics Panel:**
  - Solid color pattern overlay buttons for panel testing
  - Hardware toggles (Backlight rail kill, Color inversion)
  - Quick brightness presets (10%, 25%, 50%, 75%, 100%)
  - Telemetry readout on screen
- **Fixed Pinout & Hardware Compatibility:**
  - Backlight PWM on GPIO 42 (LEDC)
  - TCA9554 EXIO1 for AP3032 boost regulator enable
  - TCA9554 EXIO6 for SYS_EN power hold latch

---

## 🛠️ Build & Setup Instructions

For the complete, step-by-step setup and compilation guide for **Windows** and **Ubuntu / Linux**, see **[BUILD_GUIDE.md](BUILD_GUIDE.md)**.

### Quick Build (Ubuntu / Linux with ESP-IDF)
```bash
. ~/esp/esp-idf/export.sh
cd ESP-IDF/11_FactoryProgram
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

### Quick Build (Windows PowerShell with ESP-IDF)
```powershell
cd ESP-IDF\11_FactoryProgram
idf.py build
idf.py -p COM11 flash monitor
```

---

## 📄 License
This repository is licensed under the Apache License 2.0 / MIT.
