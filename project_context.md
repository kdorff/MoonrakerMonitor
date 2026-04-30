# Project: Moonraker ESP32 LED Status Controller

## Overview
A standalone smart appliance that directly polls a 3D printer's Moonraker API to drive a WS2812B, WS2811, or SK6812 LED strip. It provides real-time visual feedback of the printer's state and job progress using customizable LED effects and a responsive web dashboard.


## Hardware Architecture
* **Microcontroller:** ESP32 (Dual-core).
* **LEDs:** WS2812B / WS2811 / SK6812 (RGB or RGBW). Support for multiple color orders (GRB, RGB, BRG, etc.) configurable via UI. Driven via GPIO (Default: 16).

* **Power:** 5V Power Supply (Shared between ESP32 and LEDs, but with separate wiring to avoid drawing high current through the ESP32 pins). Common Ground is mandatory.
* **Architecture:** Uses **Core 0** for background network tasks (Moonraker polling) and **Core 1** for time-critical LED servicing and the Async Web Server. This ensures zero flickering during WiFi activity.

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
2. **Preparation**: Heating, homing, or probing (State is `printing` but progress is `0.0`).
3. **Printing**: Active print job.
4. **Paused**: Print job suspended.
5. **Complete**: Job finished successfully.
6. **Cancelled**: Job aborted.
7. **Error**: Printer error reported by Moonraker.
8. **Disconnected**: Moonraker API is unreachable (WiFi is connected, but the printer is off or network is down).


### Progress & ETA Math
* **Prioritized Tracking**: The system prioritizes `display_status.progress` (M73) because it provides a linear time-based progression.
* **Fallback**: Falls back to `virtual_sdcard.progress` (file byte position) if M73 is not present in the G-code.
* **ETA Extrapolation**: ETA is calculated on-board: `(Elapsed / Progress) - Elapsed`.
* **Visual Safety**: Progress is capped at `99.9%` while in the `printing` state to ensure the strip doesn't show "Complete" before the physical move is finished.

## Core API Targets (Moonraker)
The backend polls `/printer/objects/query` every 2 seconds for:
* `print_stats`: state, print_duration.
* `display_status`: progress (M73).
* `virtual_sdcard`: progress (fallback).
