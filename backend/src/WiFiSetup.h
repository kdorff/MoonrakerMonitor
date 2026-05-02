#ifndef WIFI_SETUP_H
#define WIFI_SETUP_H

/**
 * @brief Initialize the WiFi connection using WiFiManager.
 * If no credentials are saved, it starts a captive portal hotspot 
 * named "MoonrakerMonitor".
 */
void setupWiFi();

/**
 * @brief Handles background WiFi maintenance and captive portal processing.
 * Should be called frequently in the main loop().
 */
void processWiFi();

#endif
