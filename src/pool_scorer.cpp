#include "pool_scorer.h"

// EWMA decay constants
#define ALPHA_LATENCY      0.10f  // slow adaptation to latency changes
#define ALPHA_ACCEPT       0.20f  // faster adaptation to acceptance rate
#define ALPHA_JOB_INTERVAL 0.15f  // medium adaptation to template frequency

// Switch when primary score < SWITCH_RATIO * best_alt_score for SWITCH_STRIKES measurements.
#define SWITCH_RATIO   0.5f
#define SWITCH_STRIKES 5

// Initial job interval prior: 60s (conservative; updates quickly once jobs arrive)
#define INIT_JOB_INTERVAL_MS 60000.0f

volatile uint8_t g_active_pool_idx = 0;

struct PoolStats {
    float    ewma_latency_ms;      // lower is better
    float    ewma_accept_rate;     // 0..1, higher is better
    float    ewma_job_interval_ms; // lower = more templates = better
    uint32_t submit_ts;            // timestamp of last outstanding submit
    uint32_t last_job_ts;          // timestamp of last MINING_NOTIFY received
    uint32_t submits;
    uint32_t accepts;
    uint32_t rejects;
    uint32_t jobs_received;
    bool     initialized;
};

static PoolConfig s_configs[POOL_COUNT];
static PoolStats  s_stats[POOL_COUNT];
static uint8_t    s_switch_strike_count = 0;

void pool_scorer_init(const PoolConfig configs[POOL_COUNT]) {
    for (int i = 0; i < POOL_COUNT; i++) {
        s_configs[i] = configs[i];
        s_stats[i]   = { 200.0f, 0.5f, INIT_JOB_INTERVAL_MS, 0, 0, 0, 0, 0, 0, false };
    }
    g_active_pool_idx   = 0;
    s_switch_strike_count = 0;
}

void pool_scorer_on_submit(uint8_t idx, uint32_t ts_ms) {
    if (idx >= POOL_COUNT) return;
    s_stats[idx].submit_ts = ts_ms;
    s_stats[idx].submits++;
}

void pool_scorer_on_accept(uint8_t idx, uint32_t ts_ms) {
    if (idx >= POOL_COUNT) return;
    PoolStats& s = s_stats[idx];
    if (s.submit_ts > 0) {
        uint32_t latency = ts_ms - s.submit_ts;
        if (!s.initialized) {
            s.ewma_latency_ms = (float)latency;
            s.ewma_accept_rate = 1.0f;
            s.initialized = true;
        } else {
            s.ewma_latency_ms  = ALPHA_LATENCY * latency  + (1.0f - ALPHA_LATENCY) * s.ewma_latency_ms;
            s.ewma_accept_rate = ALPHA_ACCEPT  * 1.0f     + (1.0f - ALPHA_ACCEPT)  * s.ewma_accept_rate;
        }
        s.submit_ts = 0;
    }
    s.accepts++;
    s_switch_strike_count = 0; // primary performing — reset counter
}

void pool_scorer_on_reject(uint8_t idx) {
    if (idx >= POOL_COUNT) return;
    PoolStats& s = s_stats[idx];
    if (s.initialized) {
        s.ewma_accept_rate = ALPHA_ACCEPT * 0.0f + (1.0f - ALPHA_ACCEPT) * s.ewma_accept_rate;
    }
    s.rejects++;

    // Check whether active pool should be switched
    if (idx != g_active_pool_idx) return;
    uint8_t best = pool_scorer_best();
    if (best != g_active_pool_idx) {
        if (++s_switch_strike_count >= SWITCH_STRIKES) {
            Serial.printf("[PoolScorer] Switching pool %d -> %d (score %.4f vs %.4f)\n",
                          g_active_pool_idx, best,
                          pool_scorer_score(g_active_pool_idx),
                          pool_scorer_score(best));
            g_active_pool_idx   = best;
            s_switch_strike_count = 0;
        }
    } else {
        s_switch_strike_count = 0;
    }
}

void pool_scorer_on_job(uint8_t idx, uint32_t ts_ms) {
    if (idx >= POOL_COUNT) return;
    PoolStats& s = s_stats[idx];
    if (s.last_job_ts > 0 && ts_ms > s.last_job_ts) {
        uint32_t interval = ts_ms - s.last_job_ts;
        s.ewma_job_interval_ms = ALPHA_JOB_INTERVAL * (float)interval
                               + (1.0f - ALPHA_JOB_INTERVAL) * s.ewma_job_interval_ms;
        if (!s.initialized) s.initialized = true;  // first real interval = enough to score
    }
    s.last_job_ts = ts_ms;
    s.jobs_received++;
}

float pool_scorer_score(uint8_t idx) {
    if (idx >= POOL_COUNT) return 0.0f;
    const PoolStats& s = s_stats[idx];
    if (!s.initialized) return 0.5f / 201.0f; // neutral prior
    // template_rate is the dominant factor: more jobs/sec = more unique work explored
    float template_rate = 1000.0f / (s.ewma_job_interval_ms + 1.0f);
    return template_rate * s.ewma_accept_rate / (s.ewma_latency_ms + 1.0f);
}

uint8_t pool_scorer_best() {
    uint8_t best = 0;
    float   best_score = pool_scorer_score(0);
    for (uint8_t i = 1; i < POOL_COUNT; i++) {
        float sc = pool_scorer_score(i);
        if (sc > best_score) { best_score = sc; best = i; }
    }
    return best;
}

const PoolConfig& pool_scorer_config(uint8_t idx) {
    if (idx >= POOL_COUNT) idx = 0;
    return s_configs[idx];
}
