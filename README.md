# Cyberpunk Smart Badge Keychain Fun Find

ESP32 dual core smart badge project broadcasting Apple Find My Macless Haystack BLE beacon while simulating ultra realistic fluid dynamics on a DSTK22666 0.91 inch SSD1306 OLED display using an ADXL345 accelerometer.

## Hardware Components

1. Microcontroller: ESP32 WROOM 32D DevKit v1 with 240 MHz dual core processor and Bluetooth Low Energy.
2. Display: DSTK22666 SSD1306 monochrome OLED screen with 128 by 32 pixel resolution.
3. Accelerometer: GY 291 ADXL345 digital 3 axis sensor over I2C.

## Pin Connections

ESP32 GPIO 21 SDA connects to OLED SDA and ADXL345 SDA
ESP32 GPIO 22 SCL connects to OLED SCL and ADXL345 SCL
ESP32 3V3 connects to OLED VCC and ADXL345 VCC and ADXL345 CS
ESP32 GND connects to OLED GND and ADXL345 GND and ADXL345 SDO

## Features

1. Dual Core FreeRTOS Architecture
Core 0 runs the Apple Find My Macless Haystack background task broadcasting raw 31 byte BLE advertisement payloads.
Core 1 handles sensor acquisition and simulates 240 fluid particles while rendering at up to 110 frames per second.

2. Overclocked Display Pipeline
The SSD1306 internal clock divider and oscillator register D5 is overclocked to value F0.
This increases display row scanning frequency above 150 Hz and eliminates all screen tearing and scan artifacts.
The I2C bus speeds up to 800 kHz during frame transmission and delivers the full 512 byte GDDRAM buffer in a single burst.

3. Position Based Dynamics Fluid Engine
Incompressible fluid model using 240 particles with volume preservation constraints.
Surface tension and cohesive attraction create realistic meniscus and droplet behavior.
Axis aligned bounding box spatial hashing cuts collision math by 75 percent.
Direct page memory mapping draws 2 by 2 macro droplets directly into video RAM in under 100 microseconds.

4. Adaptive ProMotion Power Saving
Dynamic frame timing runs at 110 frames per second during movement and sloshing.
Refresh rate drops to 33 frames per second when fluid settles and rests.
This reduces processor and bus activity by 70 percent while keeping active motion perfectly smooth.
Screen enters automatic power down after 60 seconds of inactivity.
Hard shake verification instantly wakes the device and triggers kinetic fluid splash.

## Build and Flashing

Method A: PlatformIO
1. Build and upload using the provided upload.bat script or PlatformIO extension.
2. Run monitor.bat to observe live telemetry at 115200 baud.

Method B: Arduino IDE
1. Open CyberpunkBadge/CyberpunkBadge.ino in Arduino IDE.
2. Select ESP32 Dev Module as the board.
3. Set CPU frequency to 240 MHz and upload speed to 460800 baud.
4. Flash firmware to your connected device.

## Apple Find My Key Generation

Run key_uretici.py using Python to generate your own private and public key pair.
Copy the generated 28 byte public key into PUBLIC_KEY_28 array in the source code.
