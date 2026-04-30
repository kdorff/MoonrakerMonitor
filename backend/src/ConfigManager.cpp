#include "ConfigManager.h"

ConfigManager::ConfigManager() {
    loadDefaultConfig();
}

void ConfigManager::loadDefaultConfig() {
    _config.moonrakerIP = "192.168.1.41";
    _config.moonrakerApiKey = "";
    _config.ledPin = 16;
    _config.ledCount = 5;
    _config.ledBrightness = 255;

    // Defaults based on user request
    _config.printing = { 10, 0x000000, 0x000000, 1000 };   // Multi Dynamic, Black, Black
    _config.paused = { 2, 0xFFFF00, 0x000000, 1000 };      // Breath, Yellow, Black
    _config.standby = { 0, 0x4B0082, 0x000000, 1000 };     // Static, Indigo, Black
    _config.complete = { 0, 0x00FF00, 0x000000, 1000 };    // Static, Green, Black
    _config.error = { 0, 0xFF0000, 0x000000, 1000 };       // Static, Red, Black
    _config.cancelled = { 0, 0xFFA500, 0x000000, 1000 };   // Static, Orange, Black
    _config.preparation = { 1, 0x0000FF, 0x000000, 1000 }; // Blink, Blue, Black
}

bool ConfigManager::begin() {
    _preferences.begin("moonraker", false);
    return loadConfig();
}

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

    _config.moonrakerIP = doc["moonrakerIP"] | _config.moonrakerIP;
    _config.moonrakerApiKey = doc["moonrakerApiKey"] | _config.moonrakerApiKey;
    _config.ledPin = doc["ledPin"] | _config.ledPin;
    _config.ledCount = doc["ledCount"] | _config.ledCount;
    _config.ledBrightness = doc["ledBrightness"] | _config.ledBrightness;

    auto loadState = [](JsonVariantConst json, StateConfig& state) {
        if (!json.isNull()) {
            state.effect = json["effect"] | state.effect;
            state.color = json["color"] | state.color;
            state.color2 = json["color2"] | state.color2;
            state.speed = json["speed"] | state.speed;
        }
    };

    loadState(doc["error"], _config.error);
    loadState(doc["complete"], _config.complete);
    loadState(doc["paused"], _config.paused);
    loadState(doc["standby"], _config.standby);
    loadState(doc["cancelled"], _config.cancelled);
    loadState(doc["printing"], _config.printing);
    loadState(doc["preparation"], _config.preparation);

    return true;
}

bool ConfigManager::saveConfig() {
    JsonDocument doc;
    doc["moonrakerIP"] = _config.moonrakerIP;
    doc["moonrakerApiKey"] = _config.moonrakerApiKey;
    doc["ledPin"] = _config.ledPin;
    doc["ledCount"] = _config.ledCount;
    doc["ledBrightness"] = _config.ledBrightness;

    auto saveState = [](JsonObject json, const StateConfig& state) {
        json["effect"] = state.effect;
        json["color"] = state.color;
        json["color2"] = state.color2;
        json["speed"] = state.speed;
    };

    saveState(doc["error"].to<JsonObject>(), _config.error);
    saveState(doc["complete"].to<JsonObject>(), _config.complete);
    saveState(doc["paused"].to<JsonObject>(), _config.paused);
    saveState(doc["standby"].to<JsonObject>(), _config.standby);
    saveState(doc["cancelled"].to<JsonObject>(), _config.cancelled);
    saveState(doc["printing"].to<JsonObject>(), _config.printing);
    saveState(doc["preparation"].to<JsonObject>(), _config.preparation);

    String jsonStr;
    if (serializeJson(doc, jsonStr) == 0) {
        Serial.println("Failed to serialize config to string");
        return false;
    }
    
    _preferences.putString("cfg", jsonStr);
    return true;
}

AppConfig& ConfigManager::getConfig() {
    return _config;
}
