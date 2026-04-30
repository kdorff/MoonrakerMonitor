# Moonraker Monitor

Moonraker Monitor is a standalone ESP32-based hardware status indicator for Klipper 3D printers running Moonraker. It connects directly to your printer's Moonraker API over Wi-Fi and uses WS2812B, WS2811, or SK6812 (NeoPixel) LED strips to provide rich, real-time, highly customizable visual feedback on your printer's current state and print progress.


It features a premium, responsive React Single-Page Application (SPA) dashboard hosted directly on the ESP32, allowing you to configure the LED animations and colors from any web browser without needing to recompile the firmware.

![Dashboard Preview](https://via.placeholder.com/800x400.png?text=Moonraker+Monitor+Dashboard) *(Dashboard preview)*

## Features

- **Intelligent Progress Math**: Uses a sophisticated "math engine" that prioritizes M73 (slicer-linear) metadata for extremely accurate progress tracking, with a robust fallback to file-byte progression for legacy compatibility.
- **Dynamic ETA Reporting**: Calculates and displays the estimated time remaining. The web dashboard also provides a localized "Finishes at" timestamp based on your browser's time.
- **Dual-Core Processing**: Engineered for performance, the ESP32 uses Task Pinning to run Moonraker polling on **Core 0** and LED servicing/Web server on **Core 1**, ensuring zero LED flickering even during heavy network activity.
- **8-State Awareness**: Dedicated LED mapping for `Standby`, `Preparation` (heating/homing), `Printing`, `Paused`, `Complete`, `Cancelled`, `Error`, and `Disconnected` (cannot reach Moonraker).
- **Configurable LED Type**: Support for various color orders (GRB, RGB, BRG) and 4-channel strips (RGBW/GRBW) directly from the web interface.

- **Advanced Animation Engine**: Powered by `WS2812FX` with support for 50+ hardware-accelerated animations. Every state supports **bi-color mapping** (Primary/Secondary) for beautiful chase and pulse effects.
- **Premium React Dashboard**: A sleek, modern UI built with React, TypeScript, and Tailwind CSS. Features shimmer-effect progress bars and real-time state synchronization.
- **Dual OTA Support**: Update your device wirelessly via **ArduinoOTA** (from the IDE) or using the **Web Dashboard** (by uploading a `.bin` file).
- **WiFiManager Integration**: No hardcoded credentials. Connect to the device's Access Point to easily configure your home Wi-Fi.

## Expected Printer Compatibility

Moonraker Monitor uses the standard Klipper/Moonraker API (`/printer/objects/query`), making it hardware-agnostic. It works with:

*   **Native Klipper Printers**: Snapmaker U1, Sovol SV07/SV08, Elegoo Neptune 4, Qidi Tech X-Max 3, FLSUN V400, etc.
*   **DIY Builds**: Voron, RatRig, VZBot running Mainsail or Fluidd.
*   **Klipperized Upgrades**: Printers with Creality Sonic Pad, BTT Pad 7, or standard Raspberry Pi.

## Hardware Requirements

1. **ESP32 Microcontroller** (e.g., ESP32 D1 Mini, NodeMCU-32S, or M5Stack ATOMS3)
2. **LED Strip** (WS2812B, WS2811, or SK6812 RGBW)

3. **5V Power Supply** (Dedicated power recommended for >30 LEDs)
4. *Optional:* Logic Level Shifter (3.3V to 5V) for signal stability.

### Wiring

While the ESP32 and LED strip can share the same 5V power supply, you should avoid drawing high current **through** the ESP32's pins (VCC/VIN). 

*   **For Testing**: A very short string (< 10 LEDs) can be powered directly from the ESP32's 5V pin for temporary testing.
*   **For Production**: You should run separate power wires from your 5V supply directly to the LED strip. Drawing too much current through the ESP32 can overheat the board or damage its internal traces.

Please refer to the **[Adafruit NeoPixel Überguide: Basic Connections](https://learn.adafruit.com/adafruit-neopixel-uberguide/basic-connections)** for best practices.

**Key Requirements:**
1. **Common Ground**: Always ensure the Ground (GND) of your power supply, LED strip, and ESP32 are all connected together.
2. **Data Resistor**: A 300-500 Ohm resistor on the data line (GPIO 16) is highly recommended to protect the first LED.
3. **Power Injection**: For long strips, power should be injected at both ends to prevent voltage drop and color shifting.

## Software Installation (PlatformIO)

This project consists of a C++ firmware (`/backend`) and a React frontend (`/frontend`). 

> **Note**: Building the filesystem image now automatically runs the frontend build for you! You will need **Node.js** installed on your computer to compile the UI assets.


1. Clone this repository.
2. Open `/backend` in VSCode with **PlatformIO**.
3. **Step 1: Flash the UI**
   - Go to **Project Tasks** -> **Platform**.
   - Run **Build Filesystem Image** then **Upload Filesystem Image**.
4. **Step 2: Flash the Firmware**
   - Click the **Upload** button to flash the C++ code.

## Initial Setup

1. **Connect to Wi-Fi**: Connect to the **MoonrakerMonitorAP** hotspot and enter your Wi-Fi credentials in the portal.
2. **Access Dashboard**: Navigate to `http://<ESP32_IP_ADDRESS>` in your browser.
3. **Configure**: 
   - Enter your **Moonraker IP** in the Config tab.
   - Set your **LED Count** and **LED Pin**.
   - Map your preferred effects and colors for each state.
   - Click **Save Configuration**.

## Over-The-Air (OTA) Updates

Once Moonraker Monitor is installed and connected to your Wi-Fi, you no longer need a USB cable to update the firmware or the UI!

### Option A: Web Dashboard (Recommended)
The simplest way to update is via the built-in web update portal:
1. Navigate to `http://<ESP32_IP_ADDRESS>/update`.
2. Select the update type: **Firmware** (for `firmware.bin`) or **Filesystem** (for `littlefs.bin`).
3. Upload the file and wait for the device to reboot.

### Option B: PlatformIO (For Developers)
The project natively supports `ArduinoOTA`. To flash updates wirelessly from VSCode:
1. Open `backend/platformio.ini` and set `upload_port` to your ESP32's IP.
2. Set `upload_protocol = espota`.
3. Use the standard **Upload** or **Upload Filesystem Image** tasks.

## Development

If you wish to modify the UI:
1. Navigate to `/frontend`.
2. `npm install` && `npm run dev`.
3. The project is configured to automatically run `npm run build` whenever you trigger a **Build Filesystem Image** task in PlatformIO.
4. Re-upload the Filesystem Image via PlatformIO to see your changes on the device.


## Built With

*   [PlatformIO](https://platformio.org/) & [ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer)
*   [WS2812FX](https://github.com/kitesurfer1404/WS2812FX) (Animation Engine)
*   [ArduinoJson](https://arduinojson.org/) (Moonraker Parsing)
*   [React](https://reactjs.org/), [Vite](https://vitejs.dev/), & [Tailwind CSS](https://tailwindcss.com/)
*   [Lucide React](https://lucide.dev/) (Iconography)
