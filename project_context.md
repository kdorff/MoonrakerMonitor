# Project: Moonraker ESP32 LED Status Controller

## Overview
A standalone smart appliance that directly polls a 3D printer's Moonraker API to drive a WS2812B LED strip. This replaces a previous Home Assistant + WLED setup. The system provides real-time visual feedback of the printer's state and job progress using customizable LED effects.

## Hardware Architecture
* **Microcontroller:** ESP32, possibly ESP32-S3 (e.g., M5Stack ATOMS3 Lite).
* **LEDs:** WS2812B 5V strip (any number of LEDs but let's target around 40 LEDs for initial development). Driven directly via the ESP32 (using a level shifter if necessary - check compatibility of ESP32 pins with WS2812B data line voltage requirements).
* **Power:** 5V shared power supply shared with LED strip.

## Software Architecture
* **Backend:** C++ via PlatformIO.
    * `WiFiManager`: For captive portal Wi-Fi provisioning.
    * `ESPAsyncWebServer`: To serve the static frontend and handle REST API calls.
    * `ArduinoJson`: To parse Moonraker API payloads.
    * `LittleFS`: To store the compressed frontend files.
* **Frontend:** React + TypeScript (Vite).
    * Compiled, GZIP-compressed, and uploaded to LittleFS.
    * Provides a web UI for configuring the Moonraker IP, network settings, and mapping LED effects/colors to specific printer states.
* **LED Library:** `WS2812FX` (or similar). Chosen specifically because it includes a large library of pre-built effects (blink, breathe, wipe, scan, theater chase), replacing the need for WLED presets.

## The State Machine & LED Logic
The system polls the Moonraker API (specifically `print_stats.state`) and reacts to 6 distinct states. Track more states if they exist such as states for heating and cooling? What is offered by moonraker for the Snapmaker U1?

### Final / Idle States
If the state is `standby`, `complete`, `cancelled`, or `error`:
* **LED Count:** The effect is applied to the **entire length** of the LED strip (`max_number_of_lights`).
* **Effect:** The user-configured effect for that specific state is played.

### Active / Progress States
If the state is `printing` or `paused`:
* **Progress Calculation:** * The system calculates progress using time-based math for a smooth, linear visual: `Elapsed Time / (Elapsed Time + Slicer Estimated Time Left)`.
    * **Crucial Constraint:** The calculated progress must be capped at `99.9%` to prevent the strip from showing "Complete" if the physical machine takes slightly longer than the slicer's estimate.
* **LED Count:** The calculated percentage determines how many LEDs are illuminated: `max(1, int(round((progress_percentage / 100.0) * max_number_of_lights)))`. At least 1 LED must always be on.
* **Effect:** The user-configured effect for `printing` or `paused` is applied **only to the active number of LEDs**. The remaining LEDs on the strip should be off/black.

## Core API Targets (Moonraker)
The C++ backend will need to poll the Moonraker `/printer/objects/query` endpoint to retrieve:
1.  `print_stats.state`: (The current 6 states).
2.  `print_stats.print_duration`: (Elapsed time in seconds).
3.  `display_status.progress`: (File-based progress, used strictly as a fallback).
4.  *(Requires determining the exact Moonraker API path for Slicer Estimated Time Left, usually found in `virtual_sdcard` or `toolhead` objects depending on the exact Klipper configuration).*
