#pragma once
#include <stdint.h>

float    thermal_read_celsius();
uint32_t thermal_nonce_hw(uint32_t base_nonce_hw);
uint32_t thermal_nonce_sw(uint32_t base_nonce_sw);
void     thermal_apply_frequency();
