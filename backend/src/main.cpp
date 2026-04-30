#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include "WiFiSetup.h"
#include <WS2812FX.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Update.h>
#include <ArduinoOTA.h>
#include "ConfigManager.h"

ConfigManager configManager;
AsyncWebServer server(80);
WS2812FX* ws2812fx = nullptr;

struct PrinterStatus {
    String state = "standby";
    float progress = 0.0;
    float etaSeconds = 0.0;
    bool connected = false;
};

PrinterStatus currentStatus;
portMUX_TYPE statusMux = portMUX_INITIALIZER_UNLOCKED;

TaskHandle_t PollingTask;

void setupWebServer();
void applyLedState(const PrinterStatus& status);
void pollingTaskCode(void * parameter);

void setup() {
    Serial.begin(115200);
    delay(1000);

    if (!configManager.begin()) {
        Serial.println("ConfigManager failed to initialize");
    }

    AppConfig& config = configManager.getConfig();

    ws2812fx = new WS2812FX(config.ledCount, config.ledPin, NEO_GRB + NEO_KHZ800);
    ws2812fx->init();
    ws2812fx->setBrightness(config.ledBrightness);
    ws2812fx->setNumSegments(2); // Explicitly enable support for 2 segments
    ws2812fx->setSegment(0, 0, config.ledCount - 1, FX_MODE_COLOR_WIPE, (uint32_t)0x000000, 1000);
    ws2812fx->start();

    setupWiFi();

    // Start ArduinoOTA after Wi-Fi connects
    ArduinoOTA.setHostname("MoonrakerMonitor");
    ArduinoOTA.onStart([]() {
        Serial.println("ArduinoOTA Start");
    });
    ArduinoOTA.onEnd([]() {
        Serial.println("\nArduinoOTA End");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });
    ArduinoOTA.begin();

    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS Mount Failed");
    }

    setupWebServer();
    
    // Start Polling Task on Core 0 (Loop runs on Core 1)
    xTaskCreatePinnedToCore(
        pollingTaskCode,
        "PollingTask",
        8192,
        NULL,
        1,
        &PollingTask,
        0);
}

void loop() {
    processWiFi();
    ArduinoOTA.handle();
    ws2812fx->service();

    static unsigned long lastUpdate = 0;
    static PrinterStatus lastLedStatus;
    
    if (millis() - lastUpdate > 500) { // Check for updates 2 times a second
        lastUpdate = millis();
        PrinterStatus localStatus;
        portENTER_CRITICAL(&statusMux);
        localStatus = currentStatus;
        portEXIT_CRITICAL(&statusMux);
        
        if (localStatus.state != lastLedStatus.state || 
            abs(localStatus.progress - lastLedStatus.progress) > 0.1 ||
            localStatus.connected != lastLedStatus.connected) {
            
            applyLedState(localStatus);
            lastLedStatus = localStatus;
        }
    }
}

void pollingTaskCode(void * parameter) {
    for(;;) {
        AppConfig& config = configManager.getConfig();
        String ip = config.moonrakerIP;
        
        static int failCount = 0;
        PrinterStatus newStatus;
        portENTER_CRITICAL(&statusMux);
        newStatus = currentStatus;
        portEXIT_CRITICAL(&statusMux);
        
        if (WiFi.status() == WL_CONNECTED && ip.length() > 0) {
            WiFiClient wifiClient;
            HTTPClient http;
            http.setTimeout(5000); // 5 second timeout
            http.useHTTP10(true);  // Force HTTP/1.0 to prevent chunked encoding timeout issues (-11)
            
            String url = "http://" + ip + "/printer/objects/query?print_stats&display_status&virtual_sdcard";
            
            // Allow user to include port in the IP if necessary (e.g. 192.168.1.41:7125)
            if (!ip.startsWith("http://") && !ip.startsWith("https://")) {
                url = "http://" + ip + "/printer/objects/query?print_stats&display_status&virtual_sdcard";
            } else {
                url = ip + "/printer/objects/query?print_stats&display_status&virtual_sdcard";
            }
            
            http.begin(wifiClient, url);
            if (config.moonrakerApiKey.length() > 0) {
                http.addHeader("X-Api-Key", config.moonrakerApiKey);
            }
            int httpCode = http.GET();
            
            if (httpCode == 200) {
                failCount = 0;
                String payload = http.getString();
                JsonDocument doc;
                DeserializationError error = deserializeJson(doc, payload);
                
                if (!error) {
                    newStatus.connected = true;
                    newStatus.state = doc["result"]["status"]["print_stats"]["state"].as<String>();
                    
                    float elapsed = doc["result"]["status"]["print_stats"]["print_duration"].as<float>();
                    float progressFallback = doc["result"]["status"]["display_status"]["progress"].as<float>() * 100.0;
                    
                    float sdProgress = doc["result"]["status"]["virtual_sdcard"]["progress"].as<float>();
                    
                    float calcProgress = 0;
                    float eta = 0;
                    
                    if (sdProgress > 0) {
                        float estimatedTotal = elapsed / sdProgress;
                        eta = estimatedTotal - elapsed;
                        if (estimatedTotal > 0) {
                            calcProgress = (elapsed / estimatedTotal) * 100.0;
                        } else {
                            calcProgress = 0;
                        }
                    } else {
                        calcProgress = progressFallback;
                    }
                    
                    if (isnan(calcProgress)) calcProgress = 0;
                    if (isnan(eta)) eta = 0;
                    
                    if (newStatus.state == "printing" && calcProgress >= 100.0) {
                        calcProgress = 99.9;
                    }
                    if (calcProgress < 0) calcProgress = 0;
                    if (calcProgress > 100) calcProgress = 100;
                    
                    newStatus.progress = calcProgress;
                    newStatus.etaSeconds = eta > 0 ? eta : 0;
                } else {
                    Serial.println("Failed to parse JSON from Moonraker");
                    failCount++;
                }
            } else {
                Serial.printf("HTTP Request failed, code: %d\n", httpCode);
                failCount++;
            }
            http.end();
        } else {
            if (ip.length() == 0) {
                Serial.println("No Moonraker IP configured.");
            } else {
                Serial.println("Wi-Fi disconnected.");
            }
            failCount++;
        }
        
        if (failCount >= 3) {
            newStatus.connected = false;
            newStatus.state = "error";
        }
        
        portENTER_CRITICAL(&statusMux);
        currentStatus = newStatus;
        portEXIT_CRITICAL(&statusMux);
        
        vTaskDelay(2000 / portTICK_PERIOD_MS); // Poll every 2 seconds
    }
}

void applyLedState(const PrinterStatus& status) {
    AppConfig& config = configManager.getConfig();
    uint16_t totalLeds = config.ledCount;
    StateConfig stateConf;
    
    if (!status.connected) {
        stateConf = config.error;
    } else if (status.state == "printing") {
        if (status.progress == 0.0) {
            stateConf = config.preparation;
        } else {
            stateConf = config.printing;
        }
    } else if (status.state == "paused") {
        stateConf = config.paused;
    } else if (status.state == "complete") {
        stateConf = config.complete;
    } else if (status.state == "error") {
        stateConf = config.error;
    } else if (status.state == "cancelled") {
        stateConf = config.cancelled;
    } else { // standby and others
        stateConf = config.standby;
    }

    uint16_t activeLeds = totalLeds;
    if ((status.state == "printing" && status.progress > 0.0) || status.state == "paused") {
        activeLeds = max((uint16_t)1, (uint16_t)round((status.progress / 100.0) * totalLeds));
    }

    // Set first segment for active progress
    uint32_t colors[] = { (uint32_t)stateConf.color, (uint32_t)stateConf.color2, 0 };
    ws2812fx->setSegment(0, 0, activeLeds - 1, stateConf.effect, colors, stateConf.speed, false);
    
    // Set second segment to off/black if not full length
    if (activeLeds < totalLeds) {
        ws2812fx->setSegment(1, activeLeds, totalLeds - 1, FX_MODE_STATIC, (uint32_t)0x000000, 1000);
        ws2812fx->addActiveSegment(1);
    } else {
        ws2812fx->removeActiveSegment(1);
    }
}

void setupWebServer() {
    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request){
        PrinterStatus localStatus;
        portENTER_CRITICAL(&statusMux);
        localStatus = currentStatus;
        portEXIT_CRITICAL(&statusMux);
        
        JsonDocument doc;
        doc["state"] = localStatus.state;
        doc["progress"] = localStatus.progress;
        doc["etaSeconds"] = localStatus.etaSeconds;
        doc["connected"] = localStatus.connected;
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });
    
    server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *request){
        AppConfig& config = configManager.getConfig();
        JsonDocument doc;
        doc["moonrakerIP"] = config.moonrakerIP;
        doc["moonrakerApiKey"] = config.moonrakerApiKey;
        doc["ledPin"] = config.ledPin;
        doc["ledCount"] = config.ledCount;
        doc["ledBrightness"] = config.ledBrightness;
        
        auto saveState = [](JsonObject json, const StateConfig& state) {
            json["effect"] = state.effect;
            json["color"] = state.color;
            json["color2"] = state.color2;
            json["speed"] = state.speed;
        };

        saveState(doc["error"].to<JsonObject>(), config.error);
        saveState(doc["complete"].to<JsonObject>(), config.complete);
        saveState(doc["paused"].to<JsonObject>(), config.paused);
        saveState(doc["standby"].to<JsonObject>(), config.standby);
        saveState(doc["cancelled"].to<JsonObject>(), config.cancelled);
        saveState(doc["printing"].to<JsonObject>(), config.printing);
        saveState(doc["preparation"].to<JsonObject>(), config.preparation);
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });
    
    // Handle JSON body for POST /api/config
    server.addHandler(new AsyncCallbackJsonWebHandler("/api/config", [](AsyncWebServerRequest *request, JsonVariant &json) {
        AppConfig& config = configManager.getConfig();
        JsonObject jsonObj = json.as<JsonObject>();
        
        if (jsonObj.containsKey("moonrakerIP")) config.moonrakerIP = jsonObj["moonrakerIP"].as<String>();
        if (jsonObj.containsKey("moonrakerApiKey")) config.moonrakerApiKey = jsonObj["moonrakerApiKey"].as<String>();
        if (jsonObj.containsKey("ledPin")) config.ledPin = jsonObj["ledPin"].as<uint8_t>();
        if (jsonObj.containsKey("ledCount")) config.ledCount = jsonObj["ledCount"].as<uint16_t>();
        if (jsonObj.containsKey("ledBrightness")) {
            config.ledBrightness = jsonObj["ledBrightness"].as<uint8_t>();
            ws2812fx->setBrightness(config.ledBrightness); // Apply dynamically!
        }
        
        auto loadState = [](JsonObject json, StateConfig& state) {
            if (json.containsKey("effect")) state.effect = json["effect"].as<uint8_t>();
            if (json.containsKey("color")) state.color = json["color"].as<uint32_t>();
            if (json.containsKey("color2")) state.color2 = json["color2"].as<uint32_t>();
            if (json.containsKey("speed")) state.speed = json["speed"].as<uint16_t>();
        };
        
        if (jsonObj.containsKey("error")) loadState(jsonObj["error"].as<JsonObject>(), config.error);
        if (jsonObj.containsKey("complete")) loadState(jsonObj["complete"].as<JsonObject>(), config.complete);
        if (jsonObj.containsKey("paused")) loadState(jsonObj["paused"].as<JsonObject>(), config.paused);
        if (jsonObj.containsKey("standby")) loadState(jsonObj["standby"].as<JsonObject>(), config.standby);
        if (jsonObj.containsKey("cancelled")) loadState(jsonObj["cancelled"].as<JsonObject>(), config.cancelled);
        if (jsonObj.containsKey("printing")) loadState(jsonObj["printing"].as<JsonObject>(), config.printing);
        if (jsonObj.containsKey("preparation")) loadState(jsonObj["preparation"].as<JsonObject>(), config.preparation);
        
        configManager.saveConfig();
        
        // Immediately apply the new config to the current state
        PrinterStatus currentLocalStatus;
        portENTER_CRITICAL(&statusMux);
        currentLocalStatus = currentStatus;
        portEXIT_CRITICAL(&statusMux);
        applyLedState(currentLocalStatus);
        
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    }));

    // Handle restart
    server.on("/api/restart", HTTP_POST, [](AsyncWebServerRequest *request){
        request->send(200, "application/json", "{\"status\":\"restarting\"}");
        delay(500);
        ESP.restart();
    });

    // Web OTA endpoints
    server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request){
        const char* html = 
            "<!DOCTYPE html><html><body>"
            "<h2>Moonraker Monitor OTA Update</h2>"
            "<form method='POST' action='/update' enctype='multipart/form-data'>"
            "Type: <select name='type'>"
            "<option value='flash'>Firmware (.bin)</option>"
            "<option value='fs'>Filesystem (littlefs.bin)</option>"
            "</select><br><br>"
            "<input type='file' name='update'><br><br>"
            "<input type='submit' value='Update'>"
            "</form></body></html>";
        request->send(200, "text/html", html);
    });

    server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request){
        bool shouldReboot = !Update.hasError();
        AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", shouldReboot ? "Update Success! Rebooting..." : "Update Failed");
        response->addHeader("Connection", "close");
        request->send(response);
        if (shouldReboot) {
            delay(500);
            ESP.restart();
        }
    }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
        if (!index) {
            Serial.printf("Update Start: %s\n", filename.c_str());
            int command = U_FLASH;
            if (request->hasParam("type", true)) {
                if (request->getParam("type", true)->value() == "fs") {
                    command = U_SPIFFS;
                    Serial.println("Update Type: Filesystem (U_SPIFFS)");
                } else {
                    Serial.println("Update Type: Firmware (U_FLASH)");
                }
            } else if (filename.indexOf("littlefs") != -1) {
                command = U_SPIFFS;
                Serial.println("Auto-detected Filesystem update from filename");
            }
            if (!Update.begin(UPDATE_SIZE_UNKNOWN, command)) {
                Update.printError(Serial);
            }
        }
        if (!Update.hasError()) {
            if (Update.write(data, len) != len) {
                Update.printError(Serial);
            }
        }
        if (final) {
            if (Update.end(true)) {
                Serial.printf("Update Success: %uB\n", index + len);
            } else {
                Update.printError(Serial);
            }
        }
    });

    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

    // Pre-flight CORS for API
    server.onNotFound([](AsyncWebServerRequest *request) {
        if (request->method() == HTTP_OPTIONS) {
            request->send(200);
        } else {
            request->send(404, "text/plain", "Not found");
        }
    });

    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "content-type");

    server.begin();
}
