#include "thermal.h"
#include <Arduino.h>

float thermal_read_celsius() {
#if defined(CONFIG_IDF_TARGET_ESP32)   || \
    defined(CONFIG_IDF_TARGET_ESP32S2) || \
    defined(CONFIG_IDF_TARGET_ESP32S3) || \
    defined(CONFIG_IDF_TARGET_ESP32C3)
    return temperatureRead();
#else
    return 30.0f;
#endif
}

// Returns throttled nonce count for HW worker based on chip temperature.
uint32_t thermal_nonce_hw(uint32_t base) {
    float t = thermal_read_celsius();
    if (t < 65.0f) return base;
    if (t < 75.0f) return base * 3 / 4;
    return base / 2;
}

// Returns throttled nonce count for SW worker based on chip temperature.
uint32_t thermal_nonce_sw(uint32_t base) {
    float t = thermal_read_celsius();
    if (t < 65.0f) return base;
    if (t < 75.0f) return base * 3 / 4;
    return base / 2;
}
