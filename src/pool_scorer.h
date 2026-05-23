#pragma once
#include <Arduino.h>

#define POOL_COUNT 3

struct PoolConfig {
    String  address;
    uint16_t port;
};

// Feed events from stratum layer; query for current best pool.
void     pool_scorer_init(const PoolConfig configs[POOL_COUNT]);
void     pool_scorer_on_submit(uint8_t pool_idx, uint32_t ts_ms);
void     pool_scorer_on_accept(uint8_t pool_idx, uint32_t ts_ms);
void     pool_scorer_on_reject(uint8_t pool_idx);
uint8_t  pool_scorer_best();            // index of highest-scoring available pool
float    pool_scorer_score(uint8_t idx);
const PoolConfig& pool_scorer_config(uint8_t idx);

// Active pool index managed by stratum worker.
extern volatile uint8_t g_active_pool_idx;
