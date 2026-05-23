#pragma once

// Monitors RSSI every 10 s; triggers proactive reconnect after 30 s of weak signal.
// Start via xTaskCreate(runWiFiGuardian, ...) after WiFi is connected.
void runWiFiGuardian(void* params);
