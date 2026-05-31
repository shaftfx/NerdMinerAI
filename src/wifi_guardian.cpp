#include "wifi_guardian.h"
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define RSSI_WEAK_THRESHOLD  -80   // dBm
#define RSSI_CHECK_MS        10000 // 10 s between checks
#define RSSI_WEAK_STRIKES    3     // strikes before proactive reconnect (30 s total)
#define DISC_STRIKES         1     // force reconnect after 10s disconnected

void runWiFiGuardian(void* params) {
    uint8_t weak_count = 0;
    uint8_t disc_count = 0;
    for (;;) {
        vTaskDelay(RSSI_CHECK_MS / portTICK_PERIOD_MS);
        if (WiFi.status() != WL_CONNECTED) {
            weak_count = 0;
            if (++disc_count >= DISC_STRIKES) {
                Serial.printf("[WiFiGuard] Disconnected for %ds, forcing reconnect\n",
                              RSSI_CHECK_MS * disc_count / 1000);
                WiFi.reconnect();
                disc_count = 0;
            }
            continue;
        }
        disc_count = 0;
        int32_t rssi = WiFi.RSSI();
        if (rssi < RSSI_WEAK_THRESHOLD) {
            if (++weak_count >= RSSI_WEAK_STRIKES) {
                Serial.printf("[WiFiGuard] RSSI %d dBm for %ds, proactive reconnect\n",
                              rssi, RSSI_CHECK_MS * RSSI_WEAK_STRIKES / 1000);
                WiFi.reconnect();
                weak_count = 0;
            }
        } else {
            weak_count = 0;
        }
    }
}
