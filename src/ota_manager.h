#pragma once
#include <Arduino.h>

// Compile-time repo — cannot be changed at runtime (security boundary).
#ifndef OTA_GITHUB_OWNER
#define OTA_GITHUB_OWNER "shaftfx"
#endif
#ifndef OTA_GITHUB_REPO
#define OTA_GITHUB_REPO "NerdMinerAI"
#endif
#ifndef BOARD_NAME
#define BOARD_NAME "unknown"
#endif

// OTA check every 6 hours. Starts 60 s after boot.
#define OTA_CHECK_INTERVAL_MS (6UL * 3600 * 1000)
#define OTA_BOOT_DELAY_MS     60000UL

// Set to true by OTA task before flash; mining workers check and pause.
extern volatile bool g_ota_in_progress;

// FreeRTOS task entry point — start with xTaskCreate.
void runOTATask(void* params);

// Called once from setup() to validate a just-flashed image and confirm rollback cancel.
void ota_validate_on_boot();
