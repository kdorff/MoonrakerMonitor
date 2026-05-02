# Project: Moonraker ESP32 LED Status Controller

## Overview
A standalone smart appliance that directly polls a 3D printer's Moonraker API to drive a WS2812B, WS2811, or SK6812 LED strip. It provides real-time visual feedback of the printer's state and job progress using customizable LED effects and a responsive web dashboard.


## Hardware Architecture
* **Microcontroller:** ESP32 (Dual-core).
* **LEDs:** WS2812B / WS2811 / SK6812 (RGB or RGBW). Support for multiple color orders (GRB, RGB, BRG, etc.) configurable via UI. 
* **Architecture:** Uses **Core 0** for background network tasks (Moonraker polling) and **Core 1** for time-critical LED servicing and the Async Web Server. This ensures zero flickering during WiFi activity.

### Hardware-Specific Notes
- **M5Stack ATOM Lite**: Uses GPIO 26 for external LEDs. Internal LED is on GPIO 27 (not recommended for main strip due to heat).

## Software Architecture
* **Backend:** C++ (PlatformIO / Arduino).
    * `WiFiManager`: Captive portal provisioning.
    * `ESPAsyncWebServer`: High-performance async server for API and static UI.
    * `ArduinoJson`: JSON serialization/deserialization.
    * `LittleFS`: Filesystem for storing the compressed React frontend.
    * `WS2812FX`: Hardware-accelerated LED animation engine.
* **Frontend:** React + TypeScript + Vite.
    * Compiled into static assets, GZIP-compressed, and served from LittleFS.
    * **Automated Build**: PlatformIO integration (`build_frontend.py`) automatically runs `npm run build` during filesystem image creation.
    * Features a tabbed UI (Dashboard/Config) with real-time state synchronization.


## State Machine & LED Logic
The system implements an 8-state awareness model:

1. **Standby**: Idle state.
2. **Preparation**: Heating, homing, or probing.
    - **Detection Logic**: If state is `printing` but `filament_used == 0` or `current_layer <= 0`, it is classified as "Preparation".
3. **Printing**: Active print job with movement/extrusion.
4. **Paused**: Print job suspended.
5. **Complete**: Job finished successfully.
6. **Cancelled**: Job aborted.
7. **Error**: Printer error reported by Moonraker.
8. **Disconnected**: Moonraker API is unreachable (WiFi is connected, but the printer is off or network is down).


### Progress & ETA Math
* **Prioritized Tracking**: The system prioritizes `display_status.progress` (M73) because it provides a linear time-based progression injected by the slicer.
* **Fallback**: Falls back to `virtual_sdcard.progress` (file byte position) if M73 is not present in the G-code.
* **ETA Extrapolation**: ETA is calculated on-board: `(Elapsed / Progress) - Elapsed`.
* **Visual Safety**: Progress is capped at `99.9%` while in the `printing` state to ensure the strip doesn't show "Complete" before the physical move is finished.

## Core API Targets (Moonraker)
The backend polls `/printer/objects/query` every 2 seconds.

### Polled Data Structure
The following Moonraker objects are queried:
- `print_stats`: Provides `state`, `print_duration`, `filament_used`, and `info` (layers).
- `display_status`: Provides `progress` (M73).
- `virtual_sdcard`: Provides `progress` (File-based fallback).

### Example JSON Payload (Parsed)
```json
{
  "result": {
    "status": {
      "print_stats": {
        "state": "printing",
        "print_duration": 1234.5,
        "filament_used": 10.5,
        "info": { "total_layer": 100, "current_layer": 10 }
      },
      "display_status": { "progress": 0.1 },
      "virtual_sdcard": { "progress": 0.12 }
    }
  }
}
```
