#include "WiFiSetup.h"
#include <WiFiManager.h>

WiFiManager wm;

/**
 * @brief Configures WiFiManager and attempts to connect to saved WiFi.
 */
void setupWiFi() {
    // Set non-blocking mode so the LED animations can start even if 
    // WiFi isn't connected yet.
    wm.setConfigPortalBlocking(false);
    
    // Attempt to connect to saved credentials
    bool res = wm.autoConnect("MoonrakerMonitor");
    if (!res) {
        Serial.println("WiFi AutoConnect failed. Captive portal started in background.");
    }
}

/**
 * @brief Periodically checks WiFi status and handles captive portal requests.
 */
void processWiFi() {
    wm.process();
}
