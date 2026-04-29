#include "WiFiSetup.h"
#include <WiFiManager.h>

WiFiManager wm;

void setupWiFi() {
    wm.setConfigPortalBlocking(false);
    bool res = wm.autoConnect("MoonrakerMonitor");
    if (!res) {
        // Failed to connect, but will keep trying in background
    }
}

void processWiFi() {
    wm.process();
}
