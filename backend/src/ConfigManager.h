#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>

struct StateConfig {
    uint8_t effect;
    uint32_t color;
    uint32_t color2;
    uint16_t speed;
};

struct AppConfig {
    String moonrakerIP;
    String moonrakerApiKey;
    uint8_t ledPin;
    uint16_t ledCount;
    uint8_t ledBrightness;
    uint16_t ledType;
    
    
    StateConfig error;
    StateConfig complete;
    StateConfig paused;
    StateConfig standby;
    StateConfig cancelled;
    StateConfig printing;
    StateConfig preparation;
    StateConfig disconnected;
};

class ConfigManager {
public:
    ConfigManager();
    bool begin();
    bool loadConfig();
    bool saveConfig();
    
    AppConfig& getConfig();

private:
    AppConfig _config;
    Preferences _preferences;
    
    void loadDefaultConfig();
};

#endif // CONFIG_MANAGER_H
