/**
 * @file main.cpp
 * @brief Entry point for the Moonraker Monitor firmware.
 *
 * This system uses a dual-core architecture:
 * - Core 0: Background networking (polling Moonraker API).
 * - Core 1: Time-critical tasks (LED servicing, Web Server, ArduinoOTA).
 *
 * This separation ensures that network latency or heavy JSON parsing doesn't
 * cause the LED animations to flicker or stutter.
 */

#include "ConfigManager.h"
#include "WiFiSetup.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <AsyncJson.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <Update.h>
#include <WS2812FX.h>
#include <WiFi.h>

// Global instances
ConfigManager configManager;
AsyncWebServer server(80);
WS2812FX *ws2812fx = nullptr;

/**
 * @struct PrinterStatus
 * @brief Holds the current state of the printer as retrieved from Moonraker.
 */
struct PrinterStatus {
  String state =
      "standby"; ///< Current printer state (printing, standby, complete, etc.)
  float progress = 0.0;   ///< Calculated print progress (0-100)
  float etaSeconds = 0.0; ///< Estimated time remaining in seconds
  bool connected = false; ///< Whether the last Moonraker poll was successful
};

PrinterStatus currentStatus;
portMUX_TYPE statusMux =
    portMUX_INITIALIZER_UNLOCKED; ///< Mutex for sharing status between cores

TaskHandle_t PollingTask;

// Forward declarations
void setupWebServer();
void applyLedState(const PrinterStatus &status);
void pollingTaskCode(void *parameter);

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Initialize configuration
  if (!configManager.begin()) {
    Serial.println("ConfigManager failed to initialize");
  }

  AppConfig &config = configManager.getConfig();

  // Initialize LED strip using configured parameters
  ws2812fx = new WS2812FX(config.ledCount, config.ledPin, config.ledType);
  ws2812fx->init();
  ws2812fx->setBrightness(config.ledBrightness);

  // We use two segments:
  // Segment 0: Active LEDs representing print progress
  // Segment 1: Inactive LEDs (usually black/off)
  ws2812fx->setNumSegments(2);
  ws2812fx->setSegment(0, 0, config.ledCount - 1, FX_MODE_COLOR_WIPE,
                       (uint32_t)0x000000, 1000);
  ws2812fx->start();

  // Initialize Wi-Fi (WiFiManager)
  setupWiFi();

  // Configure ArduinoOTA for wireless flashing from PlatformIO
  ArduinoOTA.setHostname("MoonrakerMonitor");
  ArduinoOTA.onStart([]() { Serial.println("ArduinoOTA Start"); });
  ArduinoOTA.onEnd([]() { Serial.println("\nArduinoOTA End"); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError(
      [](ota_error_t error) { Serial.printf("Error[%u]: ", error); });
  ArduinoOTA.begin();

  // Initialize Filesystem for the web dashboard
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS Mount Failed");
  }

  // Initialize Web Server
  setupWebServer();

  /**
   * @brief Pin the Polling Task to Core 0.
   * The main loop() runs on Core 1 by default. By putting the network polling
   * on Core 0, we prevent network-related delays from affecting the LED timings
   * which are serviced in loop().
   */
  xTaskCreatePinnedToCore(pollingTaskCode, "PollingTask", 8192, NULL, 1,
                          &PollingTask, 0);
}

void loop() {
  processWiFi();       ///< Background Wi-Fi maintenance
  ArduinoOTA.handle(); ///< Process incoming OTA updates
  ws2812fx->service(); ///< Service the LED animation engine

  static unsigned long lastUpdate = 0;
  static PrinterStatus lastLedStatus;

  // Periodically sync the LED state with the current printer status
  if (millis() - lastUpdate > 500) {
    lastUpdate = millis();
    PrinterStatus localStatus;

    // Use critical section to safely copy data modified by Core 0
    portENTER_CRITICAL(&statusMux);
    localStatus = currentStatus;
    portEXIT_CRITICAL(&statusMux);

    // Only trigger an update if the state has meaningfully changed
    if (localStatus.state != lastLedStatus.state ||
        abs(localStatus.progress - lastLedStatus.progress) > 0.1 ||
        localStatus.connected != lastLedStatus.connected) {

      applyLedState(localStatus);
      lastLedStatus = localStatus;
    }
  }
}

/**
 * @brief Core 0 Task responsible for polling the Moonraker API.
 */
void pollingTaskCode(void *parameter) {
  for (;;) {
    AppConfig &config = configManager.getConfig();
    String ip = config.moonrakerIP;

    static int failCount = 0;
    PrinterStatus newStatus;

    // Copy current state to preserve progress/eta if poll fails
    portENTER_CRITICAL(&statusMux);
    newStatus = currentStatus;
    portEXIT_CRITICAL(&statusMux);

    if (WiFi.status() == WL_CONNECTED && ip.length() > 0) {
      WiFiClient wifiClient;
      HTTPClient http;
      http.setTimeout(5000);

      // NOTE: Using HTTP/1.0 avoids certain chunked encoding issues with
      // Moonraker that can lead to timeout errors in some network environments.
      http.useHTTP10(true);

      String url;
      String query =
          "/printer/objects/query?print_stats&display_status&virtual_sdcard";
      if (!ip.startsWith("http://") && !ip.startsWith("https://")) {
        url = "http://" + ip + query;
      } else {
        url = ip + query;
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
          newStatus.state =
              doc["result"]["status"]["print_stats"]["state"].as<String>();

          float elapsed =
              doc["result"]["status"]["print_stats"]["print_duration"]
                  .as<float>();
          float m73Progress =
              doc["result"]["status"]["display_status"]["progress"].as<float>();
          float sdProgress =
              doc["result"]["status"]["virtual_sdcard"]["progress"].as<float>();

          float calcProgress = 0;
          float eta = 0;

          /**
           * PROGRESS MATH LOGIC:
           * 1. Prioritize M73 Slicer progress (display_status). It tracks
           * time-based progression injected by the slicer, making it much more
           * linear and accurate.
           * 2. Fallback to SD progress (virtual_sdcard). It tracks file byte
           * position. This is less accurate (G-code bytes != print time) but
           * works for any file.
           */
          if (m73Progress > 0 && m73Progress <= 1.0) {
            calcProgress = m73Progress * 100.0;
            float estimatedTotal = elapsed / m73Progress;
            eta = estimatedTotal - elapsed;
          } else if (sdProgress > 0 && sdProgress <= 1.0) {
            calcProgress = sdProgress * 100.0;
            float estimatedTotal = elapsed / sdProgress;
            eta = estimatedTotal - elapsed;
          }

          if (isnan(calcProgress))
            calcProgress = 0;
          if (isnan(eta))
            eta = 0;

          // Prevent showing 100% on the LED strip while the state is still
          // "printing"
          if (newStatus.state == "printing" && calcProgress >= 100.0) {
            calcProgress = 99.9;
          }

          newStatus.progress = calcProgress;
          newStatus.etaSeconds = eta > 0 ? eta : 0;
        } else {
          failCount++;
        }
      } else {
        failCount++;
      }
      http.end();
    } else {
      failCount++;
    }

    // Consider disconnected after 3 failed polls to prevent flickering on
    // temporary jitter
    if (failCount >= 3) {
      newStatus.connected = false;
      newStatus.state = "disconnected";
    }

    // Safely update the global status
    portENTER_CRITICAL(&statusMux);
    currentStatus = newStatus;
    portEXIT_CRITICAL(&statusMux);

    vTaskDelay(2000 / portTICK_PERIOD_MS); // Poll every 2 seconds
  }
}

/**
 * @brief Logic for mapping printer status to specific LED segments and effects.
 */
void applyLedState(const PrinterStatus &status) {
  AppConfig &config = configManager.getConfig();
  uint16_t totalLeds = config.ledCount;
  StateConfig stateConf;

  // Choose the appropriate state mapping
  if (!status.connected) {
    stateConf = config.disconnected;
  } else if (status.state == "printing") {
    // If printing but progress is 0.0, we are likely heating or homing
    stateConf = (status.progress == 0.0) ? config.preparation : config.printing;
  } else if (status.state == "paused") {
    stateConf = config.paused;
  } else if (status.state == "complete") {
    stateConf = config.complete;
  } else if (status.state == "error") {
    stateConf = config.error;
  } else if (status.state == "cancelled") {
    stateConf = config.cancelled;
  } else {
    stateConf = config.standby;
  }

  // Determine how many LEDs should be active based on progress
  uint16_t activeLeds = totalLeds;
  if ((status.state == "printing" && status.progress > 0.0) ||
      status.state == "paused") {
    activeLeds = max((uint16_t)1,
                     (uint16_t)round((status.progress / 100.0) * totalLeds));
  }

  // Segment 0: Progress segment
  uint32_t colors[] = {(uint32_t)stateConf.color, (uint32_t)stateConf.color2,
                       0};
  ws2812fx->setSegment(0, 0, activeLeds - 1, stateConf.effect, colors,
                       stateConf.speed, false);

  // Segment 1: "Shadow" segment for remaining LEDs (Background)
  if (activeLeds < totalLeds) {
    ws2812fx->setSegment(1, activeLeds, totalLeds - 1, FX_MODE_STATIC,
                         (uint32_t)0x000000, 1000);
    ws2812fx->addActiveSegment(1);
  } else {
    ws2812fx->removeActiveSegment(1);
  }
}

/**
 * @brief Initialize all HTTP API endpoints for the dashboard.
 */
void setupWebServer() {
  // Current printer status for the Dashboard tab
  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
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

  // Fetch current configuration for the Config tab
  server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *request) {
    AppConfig &config = configManager.getConfig();
    JsonDocument doc;
    doc["moonrakerIP"] = config.moonrakerIP;
    doc["moonrakerApiKey"] = config.moonrakerApiKey;
    doc["ledPin"] = config.ledPin;
    doc["ledCount"] = config.ledCount;
    doc["ledBrightness"] = config.ledBrightness;
    doc["ledType"] = config.ledType;

    auto saveState = [](JsonObject json, const StateConfig &state) {
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
    saveState(doc["disconnected"].to<JsonObject>(), config.disconnected);

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });

  // Save new configuration from the web UI
  server.addHandler(new AsyncCallbackJsonWebHandler(
      "/api/config", [](AsyncWebServerRequest *request, JsonVariant &json) {
        AppConfig &config = configManager.getConfig();
        JsonObject jsonObj = json.as<JsonObject>();

        if (jsonObj.containsKey("moonrakerIP"))
          config.moonrakerIP = jsonObj["moonrakerIP"].as<String>();
        if (jsonObj.containsKey("moonrakerApiKey"))
          config.moonrakerApiKey = jsonObj["moonrakerApiKey"].as<String>();
        if (jsonObj.containsKey("ledPin"))
          config.ledPin = jsonObj["ledPin"].as<uint8_t>();
        if (jsonObj.containsKey("ledCount"))
          config.ledCount = jsonObj["ledCount"].as<uint16_t>();
        if (jsonObj.containsKey("ledType"))
          config.ledType = jsonObj["ledType"].as<uint16_t>();
        if (jsonObj.containsKey("ledBrightness")) {
          config.ledBrightness = jsonObj["ledBrightness"].as<uint8_t>();
          ws2812fx->setBrightness(config.ledBrightness);
        }

        auto loadState = [](JsonObject json, StateConfig &state) {
          if (json.containsKey("effect"))
            state.effect = json["effect"].as<uint8_t>();
          if (json.containsKey("color"))
            state.color = json["color"].as<uint32_t>();
          if (json.containsKey("color2"))
            state.color2 = json["color2"].as<uint32_t>();
          if (json.containsKey("speed"))
            state.speed = json["speed"].as<uint16_t>();
        };

        if (jsonObj.containsKey("error"))
          loadState(jsonObj["error"].as<JsonObject>(), config.error);
        if (jsonObj.containsKey("complete"))
          loadState(jsonObj["complete"].as<JsonObject>(), config.complete);
        if (jsonObj.containsKey("paused"))
          loadState(jsonObj["paused"].as<JsonObject>(), config.paused);
        if (jsonObj.containsKey("standby"))
          loadState(jsonObj["standby"].as<JsonObject>(), config.standby);
        if (jsonObj.containsKey("cancelled"))
          loadState(jsonObj["cancelled"].as<JsonObject>(), config.cancelled);
        if (jsonObj.containsKey("printing"))
          loadState(jsonObj["printing"].as<JsonObject>(), config.printing);
        if (jsonObj.containsKey("preparation"))
          loadState(jsonObj["preparation"].as<JsonObject>(),
                    config.preparation);
        if (jsonObj.containsKey("disconnected"))
          loadState(jsonObj["disconnected"].as<JsonObject>(),
                    config.disconnected);

        configManager.saveConfig();

        // Apply changes immediately to the LEDs
        PrinterStatus currentLocalStatus;
        portENTER_CRITICAL(&statusMux);
        currentLocalStatus = currentStatus;
        portEXIT_CRITICAL(&statusMux);
        applyLedState(currentLocalStatus);

        request->send(200, "application/json", "{\"status\":\"ok\"}");
      }));

  // Trigger device reboot
  server.on("/api/restart", HTTP_POST, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json", "{\"status\":\"restarting\"}");
    delay(500);
    ESP.restart();
  });

  // Simple built-in update UI for Web OTA
  server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request) {
    const char *html =
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

  // Handle the OTA upload stream
  server.on(
      "/update", HTTP_POST,
      [](AsyncWebServerRequest *request) {
        bool shouldReboot = !Update.hasError();
        AsyncWebServerResponse *response = request->beginResponse(
            200, "text/plain",
            shouldReboot ? "Update Success! Rebooting..." : "Update Failed");
        response->addHeader("Connection", "close");
        request->send(response);
        if (shouldReboot) {
          delay(500);
          ESP.restart();
        }
      },
      [](AsyncWebServerRequest *request, String filename, size_t index,
         uint8_t *data, size_t len, bool final) {
        if (!index) {
          int command = U_FLASH;
          if (request->hasParam("type", true) &&
              request->getParam("type", true)->value() == "fs") {
            command = U_SPIFFS;
          } else if (filename.indexOf("littlefs") != -1) {
            command = U_SPIFFS;
          }
          Update.begin(UPDATE_SIZE_UNKNOWN, command);
        }
        if (!Update.hasError())
          Update.write(data, len);
        if (final)
          Update.end(true);
      });

  // Serve the React frontend from LittleFS
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  // Enable CORS for development
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers",
                                       "content-type");

  server.begin();
}
