# Waveshare ESP32-S3-Touch-LCD-3.49 (V2): Build & Setup Guide

This guide provides comprehensive, step-by-step instructions to set up the ESP-IDF build environment, compile the factory firmware with the ADB-like USB remote control interface, and flash the device on **Windows** and **Ubuntu / Linux**.

---

## 1. Hardware Specifications

- **Board:** Waveshare ESP32-S3-Touch-LCD-3.49 (Rev 1.1)
- **MCU:** ESP32-S3 (Dual-core Xtensa® 32-bit LX7 @ 240 MHz)
- **Memory:** 16 MB Flash (Quad SPI, 80 MHz), 8 MB Octal PSRAM (OPI @ 80 MHz)
- **Display:** 3.49″ IPS Capacitive Touch LCD (172 × 640, QSPI ST77916 / AXS15231B)
- **Audio:** ES8311 Codec (I2S TX) + ES7210 Mic (I2S RX) + NS4150 Power Amp (TCA9554 EXIO7)
- **IO Expander:** TCA9554PWR (I2C address \`0x20\` / \`0x38\`)
  - \`EXIO0\`: Touch Interrupt (\`TOUCH_INT\`)
  - \`EXIO1\`: Backlight Boost Enable (\`BL_EN\` - AP3032 rail)
  - \`EXIO5\`: LCD Hardware Reset (\`LCD_RST\`)
  - \`EXIO6\`: Power Hold Latch (\`SYS_EN\`)
  - \`EXIO7\`: Power Amp Enable (\`PA_EN\`)
- **Backlight PWM:** GPIO 42 (LEDC Timer 0, 5 kHz)

---

## 2. Windows Setup & Compilation Guide (ESP-IDF v5)

### Step 2.1: Install ESP-IDF on Windows
1. Download the official **ESP-IDF Windows All-in-One Installer**:
   - [ESP-IDF Tools Installer (v5.1.x / v5.3.x)](https://dl.espressif.com/dl/esp-idf/)
2. Run the installer and select **ESP-IDF v5.1 or v5.3** with CMake, Ninja, and Python 3.11 included.
3. Once installation completes, open the **ESP-IDF 5.x PowerShell** shortcut created on your desktop.

### Step 2.2: Clone the Repository on Windows
In ESP-IDF PowerShell:
\`\`\`powershell
git clone https://github.com/rahulraj80/ESP32-S3-Touch-LCD-3.49-V2.git
cd ESP32-S3-Touch-LCD-3.49-V2
\`\`\`

### Step 2.3: Build 11_FactoryProgram (with USB ADB Remote Interface)
\`\`\`powershell
cd ESP-IDF\11_FactoryProgram
idf.py set-target esp32s3
idf.py build
\`\`\`

Generated binaries:
- \`build\bootloader\bootloader.bin\`
- \`build\partition_table\partition-table.bin\`
- \`build\12_LVGL_Test.bin\` (Application binary)

### Step 2.4: Identify COM Port & Flash on Windows
Connect the board via USB-C:
\`\`\`powershell
# Flash Firmware & Bootloader automatically
idf.py -p COM11 flash

# Or flash and open serial monitor
idf.py -p COM11 flash monitor
\`\`\`

---

## 3. Ubuntu / Linux Setup & Compilation Guide

### Step 3.1: Install System Prerequisites
\`\`\`bash
sudo apt update && sudo apt install -y \
  git wget flex bison gperf python3 python3-pip python3-venv \
  cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0
\`\`\`

### Step 3.2: Install ESP-IDF v5
\`\`\`bash
mkdir -p ~/esp
cd ~/esp
git clone -b v5.3 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32s3
\`\`\`

### Step 3.3: Clone the Repository
\`\`\`bash
cd ~
git clone https://github.com/rahulraj80/ESP32-S3-Touch-LCD-3.49-V2.git
cd ESP32-S3-Touch-LCD-3.49-V2
\`\`\`

### Step 3.4: Build 11_FactoryProgram
\`\`\`bash
# 1. Source ESP-IDF environment
source ~/esp/esp-idf/export.sh

# 2. Build using automated script or idf.py
./build_factory.sh
\`\`\`
*(Or manually: \`cd ESP-IDF/11_FactoryProgram && idf.py build\`)*

### Step 3.5: Flash on Ubuntu / Linux
\`\`\`bash
# Set USB permissions
sudo usermod -a -G dialout $USER

# Flash to device
cd ESP-IDF/11_FactoryProgram
idf.py -p /dev/ttyACM0 flash monitor
\`\`\`

---

## 4. Standalone Flashing via \`esptool.py\` (Cross-Platform)

If flashing from a computer without ESP-IDF installed:
\`\`\`bash
pip install esptool
\`\`\`

\`\`\`bash
# Windows
python -m esptool --chip esp32s3 -p COM11 -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m \
  0x0000   ESP-IDF/11_FactoryProgram/build/bootloader/bootloader.bin \
  0x8000   ESP-IDF/11_FactoryProgram/build/partition_table/partition-table.bin \
  0x10000  ESP-IDF/11_FactoryProgram/build/12_LVGL_Test.bin

# Linux / macOS
python3 -m esptool --chip esp32s3 -p /dev/ttyACM0 -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m \
  0x0000   ESP-IDF/11_FactoryProgram/build/bootloader/bootloader.bin \
  0x8000   ESP-IDF/11_FactoryProgram/build/partition_table/partition-table.bin \
  0x10000  ESP-IDF/11_FactoryProgram/build/12_LVGL_Test.bin
\`\`\`

---

## 5. USB Remote Control Interface ("ADB for ESP32")

When \`11_FactoryProgram\` is running, the native USB CDC serial port exposes an interactive command-line interface. You can type commands directly in any serial terminal or automate them from Python.

### Available Serial Commands

| Command | Description | Example |
| :--- | :--- | :--- |
| \`help\` | Displays all available commands and help menu | \`help\` |
| \`page <0\|1\|2>\` | Switch carousel page (0=Sensors, 1=Slider, 2=Options) | \`page 2\` |
| \`next\` / \`prev\` | Switch to the next or previous carousel screen | \`next\` |
| \`tap <x> <y>\` | Injects a simulated touch tap at (x,y) screen coordinates | \`tap 86 320\` |
| \`swipe <x1> <y1> <x2> <y2> [ms]\` | Simulates a touch swipe gesture across screen coordinates | \`swipe 80 500 80 100 200\` |
| \`bl <0-255>\` | Sets the LCD backlight brightness (0=Off, 255=Max) | \`bl 128\` |
| \`color <red\|green\|blue\|white\|black\|none>\` | Overlays a solid test pattern color | \`color red\` |
| \`dark\` | Kills backlight boost power rail (AP3032 \`EXIO1\` + PWM 0) | \`dark\` |
| \`light\` | Restores backlight boost power rail and default brightness | \`light\` |
| \`invert <0\|1>\` | Toggles display color inversion register on ST77916/AXS15231B | \`invert 1\` |
| \`status\` | Returns real-time JSON status telemetry | \`status\` |

---

## 6. Python Remote Control Script Example

You can control the UI and simulate interactions over USB from Python:

\`\`\`python
import serial
import time

ser = serial.Serial("COM11", 115200, timeout=1)
time.sleep(1)

# Navigate to Options Page
ser.write(b"page 2\n")
time.sleep(0.5)

# Trigger solid red test pattern
ser.write(b"color red\n")
time.sleep(1)

# Dismiss pattern
ser.write(b"color none\n")
time.sleep(0.5)

# Get Telemetry Status
ser.write(b"status\n")
print(ser.read_all().decode("utf-8", errors="replace"))

ser.close()
\`\`\`
