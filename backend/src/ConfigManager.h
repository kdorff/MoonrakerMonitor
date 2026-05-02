/**
 * @file ConfigManager.h
 * @brief Handles persistent storage and management of the Moonraker Monitor configuration.
 * 
 * Uses ESP32 Preferences (NVS) to store settings as a JSON string, allowing for 
 * flexible and easy-to-update configuration.
 */

#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>

/**
 * @struct StateConfig
 * @brief Configuration for a specific printer state's LED effect.
 */
struct StateConfig {
    uint8_t effect;   ///< WS2812FX effect ID
    uint32_t color;   ///< Primary hex color (0xRRGGBB)
    uint32_t color2;  ///< Secondary hex color for bi-color effects
    uint16_t speed;   ///< Effect speed (ms), where lower is faster
};

/**
 * @struct AppConfig
 * @brief Global application settings and state mappings.
 */
struct AppConfig {
    String moonrakerIP;     ///< IP address or hostname of the Moonraker server.
    String moonrakerApiKey; ///< Optional API key for Moonraker (X-Api-Key header).
    uint8_t ledPin;         ///< GPIO pin connected to the LED data line.
    uint16_t ledCount;      ///< Total number of LEDs in the strip/string.
    uint8_t ledBrightness;  ///< Global brightness level (0-255).
    uint16_t ledType;       ///< NeoPixel type bitmask (e.g., NEO_GRB + NEO_KHZ800).
    
    // State-specific LED mappings. These define which effect/colors to show
    // when the printer transitions into a specific state.
    StateConfig error;        ///< Triggered on printer/network error.
    StateConfig complete;     ///< Triggered when a print job finishes successfully.
    StateConfig paused;       ///< Triggered when a print is suspended.
    StateConfig standby;      ///< Triggered when the printer is idle.
    StateConfig cancelled;    ///< Triggered when a print is aborted.
    StateConfig printing;     ///< Triggered during an active print job.
    StateConfig preparation;  ///< Triggered during heating, homing, or probing.
    StateConfig disconnected; ///< Triggered when Moonraker is unreachable but WiFi is connected.
};

/**
 * @class ConfigManager
 * @brief Manages loading and saving AppConfig to Non-Volatile Storage (NVS).
 */
class ConfigManager {
public:
    ConfigManager();

    /**
     * @brief Initializes the preferences store.
     * @return true if successfully initialized.
     */
    bool begin();

    /**
     * @brief Loads configuration from NVS. If no config exists, saves defaults.
     * @return true if config was successfully loaded or created.
     */
    bool loadConfig();

    /**
     * @brief Serializes the current config to JSON and saves it to NVS.
     * @return true if successfully saved.
     */
    bool saveConfig();
    
    /**
     * @brief Returns a reference to the global configuration object.
     */
    AppConfig& getConfig();

private:
    AppConfig _config;
    Preferences _preferences;
    
    /**
     * @brief Populates the _config object with default values.
     */
    void loadDefaultConfig();
};

#endif // CONFIG_MANAGER_H

