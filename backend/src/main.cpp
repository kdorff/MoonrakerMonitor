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

/**
 * @brief Port of WLED's mode_rainbow (Colorloop).
 * Cycles the entire strip through the rainbow in unison.
 * 
 * @return uint16_t Next service interval in ms (20ms = 50fps).
 */
uint16_t mode_wled_colorloop(void) {
  WS2812FX::Segment *seg = ws2812fx->getSegment();
  // Time-based offset for smooth animation. 256 is the max wheel index.
  uint8_t counter = (millis() * 256 / (seg->speed + 1)) & 0xFF;
  ws2812fx->fill(ws2812fx->color_wheel(counter), seg->start,
                 seg->stop - seg->start + 1);
  return 20; 
}

/**
 * @brief Port of WLED's mode_rainbow_cycle (Rainbow).
 * Cycles a rainbow along the length of the strip.
 * 
 * @return uint16_t Next service interval in ms.
 */
uint16_t mode_wled_rainbow(void) {
  WS2812FX::Segment *seg = ws2812fx->getSegment();
  uint16_t segLen = seg->stop - seg->start + 1;
  uint8_t counter = (millis() * 256 / (seg->speed + 1)) & 0xFF;

  for (uint16_t i = 0; i < segLen; i++) {
    uint8_t index = (i * 256 / segLen) + counter;
    ws2812fx->setPixelColor(seg->start + i, ws2812fx->color_wheel(index));
  }
  return 20;
}

/**
 * @brief Port of WLED's mode_lake.
 * A calm, waving effect using overlapping sine waves to simulate water movement.
 * 
 * @return uint16_t Next service interval in ms.
 */
uint16_t mode_wled_lake(void) {
  WS2812FX::Segment *seg = ws2812fx->getSegment();
  uint16_t segLen = seg->stop - seg->start + 1;
  // Map speed to a floating point factor for smooth math
  float sp = 200.0 / (seg->speed + 1);

  float t = millis() / 1000.0;
  float wave1 = sin(t * (sp + 2.0)) * 64.0;
  float wave2 = sin(t * (sp + 1.0)) * 64.0;
  float wave3 = (sin(t * (sp + 2.0)) + 1.0) * 40.0;

  for (uint16_t i = 0; i < segLen; i++) {
    float angle1 = (i * 15.0 + wave1) * PI / 180.0;
    float angle2 = (i * 23.0 + wave2) * PI / 180.0;
    float index_f = (cos(angle1) + 1.0) * 127.5 / 2.0 +
                    (sin(angle2) + 1.0) * 127.5 / 2.0;
    uint8_t index = (uint8_t)index_f;
    uint8_t lum = (index > wave3) ? index - (uint8_t)wave3 : 0;

    ws2812fx->setPixelColor(
        seg->start + i,
        ws2812fx->color_blend(seg->colors[0], seg->colors[1], lum));
  }
  return 33; // 30 FPS
}

/**
 * @brief Port of WLED's mode_chunchun.
 * Little "birds" (pixels) flying in a pendulum motion.
 * 
 * @return uint16_t Next service interval in ms.
 */
uint16_t mode_wled_chunchun(void) {
  WS2812FX::Segment *seg = ws2812fx->getSegment();
  uint16_t segLen = seg->stop - seg->start + 1;

  // Simple decay/fade for trails to make the "birds" look like they are flying
  for (uint16_t i = 0; i < segLen; i++) {
    uint32_t col = ws2812fx->getPixelColor(seg->start + i);
    if (col != 0) {
      ws2812fx->setPixelColor(seg->start + i,
                              ws2812fx->color_blend(col, BLACK, 60));
    }
  }

  uint32_t now = millis();
  float sp = 5.0 / (seg->speed + 1);
  uint16_t numBirds = 2 + (segLen >> 3);

  for (uint16_t i = 0; i < numBirds; i++) {
    float angle = (now * sp) - (i * 2.0 * PI / numBirds);
    float sin_val = (sin(angle) + 1.0) / 2.0;
    uint16_t bird = (uint16_t)(sin_val * (segLen - 1));

    // Use a rainbow palette based on bird index for visual variety
    uint8_t colorIndex = (i * 256 / numBirds);
    ws2812fx->setPixelColor(seg->start + bird, ws2812fx->color_wheel(colorIndex));
  }
  return 33;
}

/**
 * @brief Standard Arduino setup function.
 * Initializes serial, config, LEDs, WiFi, OTA, Filesystem, and Web Server.
 * Also starts the background polling task on Core 0.
 */
void setup() {
  Serial.begin(115200);
  delay(1000);

  // Initialize configuration from NVS
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

  // Register custom WLED effects to WS2812FX custom mode slots
  // These are referenced by the UI using IDs 72-75.
  ws2812fx->setCustomMode(0, F("Colorloop (WLED)"), mode_wled_colorloop);
  ws2812fx->setCustomMode(1, F("Rainbow (WLED)"), mode_wled_rainbow);
  ws2812fx->setCustomMode(2, F("Lake (WLED)"), mode_wled_lake);
  ws2812fx->setCustomMode(3, F("Chunchun (WLED)"), mode_wled_chunchun);

  // Initialize Wi-Fi (using WiFiManager for captive portal)
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

  // Initialize Filesystem for the web dashboard (serves gzipped static files)
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS Mount Failed");
  }

  // Initialize Web Server API and static file serving
  setupWebServer();

  /**
   * @brief Pin the Polling Task to Core 0.
   * The main loop() runs on Core 1 by default. By putting the network polling
   * on Core 0, we prevent network-related delays or JSON parsing spikes from
   * affecting the LED timings which are serviced in loop().
   */
  xTaskCreatePinnedToCore(pollingTaskCode, "PollingTask", 8192, NULL, 1,
                          &PollingTask, 0);
}

/**
 * @brief Standard Arduino loop function.
 * Runs on Core 1. Services LEDs, WiFi, and OTA.
 */
void loop() {
  processWiFi();       ///< Background Wi-Fi maintenance
  ArduinoOTA.handle(); ///< Process incoming OTA updates
  ws2812fx->service(); ///< Service the LED animation engine

  static unsigned long lastUpdate = 0;
  static PrinterStatus lastLedStatus;

  // Periodically sync the LED state with the current printer status.
  // We don't do this every loop to reduce CPU load and mutex contention.
  if (millis() - lastUpdate > 500) {
    lastUpdate = millis();
    PrinterStatus localStatus;

    // Use critical section to safely copy data modified by Core 0 (Polling Task)
    portENTER_CRITICAL(&statusMux);
    localStatus = currentStatus;
    portEXIT_CRITICAL(&statusMux);

    // Only trigger an update if the state has meaningfully changed to avoid
    // unnecessary WS2812FX segment re-initializations.
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
 * 
 * @param parameter Unused task parameter.
 */
void pollingTaskCode(void *parameter) {
  for (;;) {
    AppConfig &config = configManager.getConfig();
    String ip = config.moonrakerIP;

    static int pollCount = 0;
    static int failCount = 0;
    PrinterStatus newStatus;

    pollCount++;
    if (pollCount >= 30) {
      Serial.println("Moonraker poll heartbeat: System is alive and polling.");
      pollCount = 0;
    }

    // Copy current state to preserve progress/eta if the next poll fails
    portENTER_CRITICAL(&statusMux);
    newStatus = currentStatus;
    portEXIT_CRITICAL(&statusMux);

    if (WiFi.status() == WL_CONNECTED && ip.length() > 0) {
      WiFiClient wifiClient;
      HTTPClient http;
      http.setTimeout(5000); // 5s timeout for network jitter

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
        if (failCount > 0) {
          Serial.printf("Moonraker poll recovered after %d failures\n",
                        failCount);
        }
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
          float filamentUsed =
              doc["result"]["status"]["print_stats"]["filament_used"]
                  .as<float>();
          int totalLayer =
              doc["result"]["status"]["print_stats"]["info"]["total_layer"]
                  .as<int>();
          int currentLayer =
              doc["result"]["status"]["print_stats"]["info"]["current_layer"]
                  .as<int>();

          float calcProgress = 0;
          float eta = 0;

          /**
           * PREPARATION DETECTION LOGIC:
           * Even if the printer state is "printing", we might still be heating,
           * homing, or probing. We distinguish this by checking:
           * 1. Layer count (if current layer <= 0, we haven't started).
           * 2. Filament usage (if 0, we haven't extruded yet).
           */
          bool isPreparing = false;
          if (newStatus.state == "printing") {
            if (totalLayer > 0 && currentLayer <= 0) {
              isPreparing = true;
            } else if (filamentUsed == 0) {
              isPreparing = true;
            }
          }

          if (isPreparing) {
            newStatus.state = "preparing";
            calcProgress = 0;
            eta = 0;
          } else if (m73Progress > 0 && m73Progress <= 1.0) {
            // Priority 1: M73 Progress (Slicer linear time)
            calcProgress = m73Progress * 100.0;
            float estimatedTotal = elapsed / m73Progress;
            eta = estimatedTotal - elapsed;
          } else if (sdProgress > 0 && sdProgress <= 1.0) {
            // Priority 2: Virtual SD Card (File byte position)
            calcProgress = sdProgress * 100.0;
            float estimatedTotal = elapsed / sdProgress;
            eta = estimatedTotal - elapsed;
          }

          if (isnan(calcProgress))
            calcProgress = 0;
          if (isnan(eta))
            eta = 0;

          // Prevent showing 100% (which triggers "Complete" visuals) on the 
          // LED strip while the state is still "printing".
          if (newStatus.state == "printing" && calcProgress >= 100.0) {
            calcProgress = 99.9;
          }

          newStatus.progress = calcProgress;
          newStatus.etaSeconds = eta > 0 ? eta : 0;
        } else {
          failCount++;
          Serial.printf("Moonraker JSON parse error: %s\n", error.c_str());
        }
      } else {
        failCount++;
        Serial.printf("Moonraker poll failed. HTTP Code: %d\n", httpCode);
      }
      http.end();
    } else {
      failCount++;
      if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Moonraker poll failed: WiFi disconnected");
      } else {
        Serial.println("Moonraker poll failed: IP address empty");
      }
    }

    // Consider disconnected after 10 failed polls to prevent flickering on
    // temporary jitter. With a 2s poll rate, this is ~20 seconds.
    if (failCount >= 10) {
      newStatus.connected = false;
      newStatus.state = "disconnected";
    }

    // Safely update the global status for consumption by loop() on Core 1
    portENTER_CRITICAL(&statusMux);
    currentStatus = newStatus;
    portEXIT_CRITICAL(&statusMux);

    vTaskDelay(2000 / portTICK_PERIOD_MS); // Poll every 2 seconds
  }
}

/**
 * @brief Logic for mapping printer status to specific LED segments and effects.
 * 
 * @param status The current printer status to apply.
 */
void applyLedState(const PrinterStatus &status) {
  AppConfig &config = configManager.getConfig();
  uint16_t totalLeds = config.ledCount;
  StateConfig stateConf;

  // Choose the appropriate state mapping from config
  if (!status.connected) {
    stateConf = config.disconnected;
  } else if (status.state == "preparing") {
    stateConf = config.preparation;
  } else if (status.state == "printing") {
    stateConf = config.printing;
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

  // Determine how many LEDs should be active based on progress.
  // For non-progress states, all LEDs are active in one segment.
  uint16_t activeLeds = totalLeds;
  if ((status.state == "printing" && status.progress > 0.0) ||
      status.state == "paused") {
    activeLeds = max((uint16_t)1,
                     (uint16_t)round((status.progress / 100.0) * totalLeds));
  }

  // Segment 0: Progress segment (Active LEDs)
  uint32_t colors[] = {(uint32_t)stateConf.color, (uint32_t)stateConf.color2,
                       0};
  ws2812fx->setSegment(0, 0, activeLeds - 1, stateConf.effect, colors,
                       stateConf.speed, false);

  // Segment 1: "Shadow" segment for remaining LEDs (Background)
  // This creates the "filling up" effect on the strip.
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
 * Provides endpoints for status polling, config management, and OTA updates.
 */
void setupWebServer() {
  // GET /api/status - Current printer status for the Dashboard tab
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

  // GET /api/config - Fetch current configuration for the Config tab
  server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *request) {
    AppConfig &config = configManager.getConfig();
    JsonDocument doc;
    doc["moonrakerIP"] = config.moonrakerIP;
    doc["moonrakerApiKey"] = config.moonrakerApiKey;
    doc["ledPin"] = config.ledPin;
    doc["ledCount"] = config.ledCount;
    doc["ledBrightness"] = config.ledBrightness;
    doc["ledType"] = config.ledType;

    // Helper to serialize individual state mappings
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

  // POST /api/config - Save new configuration from the web UI
  server.addHandler(new AsyncCallbackJsonWebHandler(
      "/api/config", [](AsyncWebServerRequest *request, JsonVariant &json) {
        AppConfig &config = configManager.getConfig();
        JsonObject jsonObj = json.as<JsonObject>();

        // Update top-level settings if present
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

        // Helper to load individual state mappings
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

        // Apply changes immediately to the LEDs so the user sees feedback
        PrinterStatus currentLocalStatus;
        portENTER_CRITICAL(&statusMux);
        currentLocalStatus = currentStatus;
        portEXIT_CRITICAL(&statusMux);
        applyLedState(currentLocalStatus);

        request->send(200, "application/json", "{\"status\":\"ok\"}");
      }));

  // POST /api/restart - Trigger device reboot
  server.on("/api/restart", HTTP_POST, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json", "{\"status\":\"restarting\"}");
    delay(500);
    ESP.restart();
  });

  // GET /update - Simple built-in update UI for Web OTA
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

  // POST /update - Handle the OTA upload stream
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

  // Serve the React frontend from LittleFS (Static Assets)
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  // Enable CORS for development (allowing localhost:5173 to hit the ESP32)
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers",
                                       "content-type");

  server.begin();
}
