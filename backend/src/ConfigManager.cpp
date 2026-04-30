#include "ConfigManager.h"
#include <WS2812FX.h>

ConfigManager::ConfigManager() {
    loadDefaultConfig();
}

/**
 * @brief Initialize the AppConfig struct with hardcoded factory defaults.
 */
void ConfigManager::loadDefaultConfig() {
    _config.moonrakerIP = "192.168.1.41";
    _config.moonrakerApiKey = "";
    _config.ledPin = 16;
    _config.ledCount = 5;
    _config.ledBrightness = 255;
    _config.ledType = NEO_GRB + NEO_KHZ800; // Default to most common WS2812B type

    // Default LED effect mappings for each printer state
    _config.printing = { 10, 0x000000, 0x000000, 1000 };   // Multi Dynamic (Off)
    _config.paused = { 2, 0xFFFF00, 0x000000, 1000 };      // Breath Yellow
    _config.standby = { 0, 0x4B0082, 0x000000, 1000 };     // Static Indigo
    _config.complete = { 0, 0x00FF00, 0x000000, 1000 };    // Static Green
    _config.error = { 0, 0xFF0000, 0x000000, 1000 };       // Static Red
    _config.cancelled = { 0, 0xFFA500, 0x000000, 1000 };   // Static Orange
    _config.preparation = { 1, 0x0000FF, 0x000000, 1000 }; // Blink Blue
    _config.disconnected = { 0, 0xFF0000, 0x000000, 1000 }; // Static Red (Same as Error by default)
}

/**
 * @brief Open the "moonraker" NVS namespace.
 */
bool ConfigManager::begin() {
    _preferences.begin("moonraker", false); // false = read/write
    return loadConfig();
}

/**
 * @brief Loads the config from NVS by reading a single "cfg" JSON string.
 * This approach is more flexible than saving each field individually to NVS.
 */
bool ConfigManager::loadConfig() {
    String jsonStr = _preferences.getString("cfg", "");
    if (jsonStr == "") {
        Serial.println("Config not found in NVS, using default and saving.");
        return saveConfig();
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, jsonStr);

    if (error) {
        Serial.println("Failed to parse NVS JSON, using default configuration");
        return false;
    }

    // Load top-level device settings
    _config.moonrakerIP = doc["moonrakerIP"] | _config.moonrakerIP;
    _config.moonrakerApiKey = doc["moonrakerApiKey"] | _config.moonrakerApiKey;
    _config.ledPin = doc["ledPin"] | _config.ledPin;
    _config.ledCount = doc["ledCount"] | _config.ledCount;
    _config.ledBrightness = doc["ledBrightness"] | _config.ledBrightness;
    _config.ledType = doc["ledType"] | _config.ledType;

    /**
     * Helper lambda to safely extract state configuration from a JSON object.
     */
    auto loadState = [](JsonVariantConst json, StateConfig& state) {
        if (!json.isNull()) {
            state.effect = json["effect"] | state.effect;
            state.color = json["color"] | state.color;
            state.color2 = json["color2"] | state.color2;
            state.speed = json["speed"] | state.speed;
        }
    };

    // Populate all state objects
    loadState(doc["error"], _config.error);
    loadState(doc["complete"], _config.complete);
    loadState(doc["paused"], _config.paused);
    loadState(doc["standby"], _config.standby);
    loadState(doc["cancelled"], _config.cancelled);
    loadState(doc["printing"], _config.printing);
    loadState(doc["preparation"], _config.preparation);
    loadState(doc["disconnected"], _config.disconnected);

    return true;
}

/**
 * @brief Serializes the config struct to a JSON string and writes it to NVS.
 */
bool ConfigManager::saveConfig() {
    JsonDocument doc;
    doc["moonrakerIP"] = _config.moonrakerIP;
    doc["moonrakerApiKey"] = _config.moonrakerApiKey;
    doc["ledPin"] = _config.ledPin;
    doc["ledCount"] = _config.ledCount;
    doc["ledBrightness"] = _config.ledBrightness;
    doc["ledType"] = _config.ledType;

    /**
     * Helper lambda to convert a StateConfig struct back into a JSON object.
     */
    auto saveState = [](JsonObject json, const StateConfig& state) {
        json["effect"] = state.effect;
        json["color"] = state.color;
        json["color2"] = state.color2;
        json["speed"] = state.speed;
    };

    // Convert each state struct into a nested JSON object
    saveState(doc["error"].to<JsonObject>(), _config.error);
    saveState(doc["complete"].to<JsonObject>(), _config.complete);
    saveState(doc["paused"].to<JsonObject>(), _config.paused);
    saveState(doc["standby"].to<JsonObject>(), _config.standby);
    saveState(doc["cancelled"].to<JsonObject>(), _config.cancelled);
    saveState(doc["printing"].to<JsonObject>(), _config.printing);
    saveState(doc["preparation"].to<JsonObject>(), _config.preparation);
    saveState(doc["disconnected"].to<JsonObject>(), _config.disconnected);

    String jsonStr;
    if (serializeJson(doc, jsonStr) == 0) {
        Serial.println("Failed to serialize config to string");
        return false;
    }
    
    // Save the entire JSON string to a single NVS key
    _preferences.putString("cfg", jsonStr);
    return true;
}

AppConfig& ConfigManager::getConfig() {
    return _config;
}

