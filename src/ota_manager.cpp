#include "ota_manager.h"
#include <Arduino.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

// GitHub auto-OTA disabled — no OTA partition available on this 4MB flash layout.
// Updates are applied via WiFi push (/update endpoint) or USB flash (ota_push.py --port).

volatile bool g_ota_in_progress = false;

void ota_validate_on_boot() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(running, &state) != ESP_OK) return;
    if (state == ESP_OTA_IMG_PENDING_VERIFY) {
        Serial.println("[OTA] Confirming new firmware valid");
        esp_ota_mark_app_valid_cancel_rollback();
    }
}

void runOTATask(void* params) {
    // GitHub OTA disabled — task sleeps forever
    for (;;) vTaskDelay(portMAX_DELAY);
}
