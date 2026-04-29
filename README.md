# Moonraker Monitor

Moonraker Monitor is a standalone ESP32-based hardware status indicator for Klipper 3D printers running Moonraker. It connects directly to your printer's Moonraker API over Wi-Fi and uses WS2812B (NeoPixel) LED strips to provide rich, real-time, highly customizable visual feedback on your printer's current state and print progress.

It features a beautiful, responsive React Single-Page Application (SPA) dashboard hosted directly on the ESP32, allowing you to configure the LED animations and colors from any web browser without needing to recompile the firmware.

![Dashboard Preview](https://via.placeholder.com/800x400.png?text=Moonraker+Monitor+Dashboard) *(Dashboard preview)*

## Features

- **Real-Time Polling**: Directly polls Moonraker for print progress, estimated time remaining, and system state (`printing`, `paused`, `standby`, `complete`, `error`, `cancelled`).
- **Progress Bar Mode**: During a print, the LED strip acts as a physical progress bar, lighting up sequentially based on print completion percentage.
- **Advanced Animations**: Powered by the `WS2812FX` library, offering 30+ hardware-accelerated LED animations (Breath, Chase, Twinkle, Fire Flicker, Dual Scan, etc.).
- **Dual-Color Support**: Every state can be mapped to a primary and secondary color to create advanced, beautiful bi-color chase animations.
- **On-Board React UI**: A sleek React & TailwindCSS dashboard is hosted entirely on the ESP32's internal flash storage. 
- **Persistent NVS Storage**: All configuration changes made in the UI are securely saved to the ESP32's Non-Volatile Storage (NVS) and persist across power cycles and firmware updates.
- **WiFiManager Integration**: No hardcoded network credentials. The ESP32 spins up its own Access Point on first boot so you can easily connect it to your home Wi-Fi network.

## Hardware Requirements

1. **ESP32 Microcontroller** (e.g., ESP32 D1 Mini, NodeMCU-32S)
2. **WS2812B LED Strip** (NeoPixels)
3. **5V Power Supply** (Make sure it provides enough amperage for your LED count. A rough rule of thumb is ~60mA per LED at maximum white brightness).
4. *Optional:* Logic Level Shifter (3.3V to 5V) if your LED strip glitches when connected directly to the ESP32's 3.3V data pin.

### Wiring
- **ESP32 5V / VIN** -> LED Strip 5V
- **ESP32 GND** -> LED Strip GND
- **ESP32 GPIO 16** -> LED Strip Data In (DIN) *(Pin is configurable in the Web UI)*

## Software Installation (PlatformIO)

This project is built using [PlatformIO](https://platformio.org/). The repository contains both the C++ firmware (`/backend`) and the React frontend (`/frontend`). 

> **Note**: The React frontend is already pre-compiled into static HTML/JS/CSS assets and stored in `/backend/data`. You do **not** need Node.js or `npm` installed just to flash the board!

1. Clone this repository to your local machine.
2. Open the `/backend` folder in VSCode with the PlatformIO extension installed.
3. Connect your ESP32 via USB.
4. **Step 1: Flash the UI**
   - In PlatformIO, go to the **Project Tasks** menu.
   - Expand your environment (e.g., `esp32dev`) -> **Platform**.
   - Click **Build Filesystem Image** then **Upload Filesystem Image**. This pushes the React UI to the ESP32's LittleFS partition.
5. **Step 2: Flash the Firmware**
   - Click the standard **Upload** button (right arrow in the bottom toolbar) to compile and flash the C++ firmware.

## Initial Setup & Configuration

1. **Connect to Wi-Fi**: 
   - Once powered on for the first time, the ESP32 will host a Wi-Fi Access Point named **MoonrakerMonitorAP**.
   - Connect to this network on your phone or computer.
   - A captive portal will appear (or navigate to `192.168.4.1`). Enter your home Wi-Fi credentials.
2. **Access the Dashboard**:
   - Once connected to your home Wi-Fi, find the ESP32's IP address on your router.
   - Open your web browser and navigate to `http://<ESP32_IP_ADDRESS>`.
3. **Configure the Device**:
   - In the **Config** tab, enter your printer's **Moonraker IP**.
   - *(Optional)* Enter your **Moonraker API Key** if your instance requires authorization.
   - Define your **LED Pin** (Default: 16) and **LED Count**.
   - Click **Save Configuration**. The LED strip will immediately update!

## Customizing the Dashboard (Development)

If you want to modify the React dashboard interface:

1. Navigate to the `/frontend` directory.
2. Run `npm install` to install dependencies.
3. Run `npm run dev` to start the Vite development server.
4. When you are done making changes, run `npm run build`. This automatically compiles and copies the optimized UI assets directly into `/backend/data/`.
5. Flash the updated UI to the ESP32 using the **Upload Filesystem Image** task in PlatformIO.

## Built With

*   [PlatformIO](https://platformio.org/)
*   [ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer)
*   [WS2812FX](https://github.com/kitesurfer1404/WS2812FX)
*   [ArduinoJson](https://arduinojson.org/)
*   [React](https://reactjs.org/) & [Vite](https://vitejs.dev/)
*   [Tailwind CSS](https://tailwindcss.com/)
