#include "basemap.h"
#include <mutex>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifdef ARDUINO
#include <esp_heap_caps.h>
#include <Arduino.h>
#endif

struct Slot {
    uint16_t *pixels = nullptr;
    double   lat = 1e9, lon = 1e9;
    float    rangeKm = -1;
    bool     valid = false;
    uint32_t lastUsedMs = 0;
    uint32_t version = 0;
};

static std::mutex s_mutex;
static Slot s_slots[BASEMAP_CACHE_SLOTS];
static bool s_began = false;

#ifdef ARDUINO
static inline uint32_t now_ms() { return millis(); }
#else
static uint32_t s_simClock = 0;
static inline uint32_t now_ms() { return ++s_simClock; }   // monotonic-enough for LRU in the sim
#endif

static uint16_t *alloc_pixels(void) {
    const size_t bytes = BASEMAP_W * BASEMAP_H * sizeof(uint16_t);
#ifdef ARDUINO
    return (uint16_t *)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return (uint16_t *)malloc(bytes);
#endif
}

void basemap_begin(void) {
    if (s_began) return;
    s_began = true;
    const size_t bytes = BASEMAP_W * BASEMAP_H * sizeof(uint16_t);
    for (auto &s : s_slots) {
        s.pixels = alloc_pixels();
        if (s.pixels) memset(s.pixels, 0, bytes);
    }
}

static bool keyMatch(const Slot &s, double lat, double lon, float rangeKm) {
    // ~1m at these magnitudes; generous enough for float/double round-tripping through
    // NVS and JSON, tight enough that two distinct saved locations never collide.
    return s.valid && fabs(s.lat - lat) < 1e-5 && fabs(s.lon - lon) < 1e-5 &&
           fabsf(s.rangeKm - rangeKm) < 0.01f;
}

bool basemap_lookup(double lat, double lon, float rangeKm,
                    const uint16_t **pixels, uint32_t *version) {
    std::lock_guard<std::mutex> lock(s_mutex);
    for (auto &s : s_slots) {
        if (keyMatch(s, lat, lon, rangeKm)) {
            s.lastUsedMs = now_ms();
            if (pixels) *pixels = s.pixels;
            if (version) *version = s.version;
            return true;
        }
    }
    return false;
}

uint16_t *basemap_claim_slot(float rangeKm) {
    std::lock_guard<std::mutex> lock(s_mutex);
    Slot *pick = nullptr;
    for (auto &s : s_slots) {
        if (!s.pixels) continue;
        if (!s.valid) { pick = &s; break; }               // prefer an empty slot
        if (!pick || s.lastUsedMs < pick->lastUsedMs) pick = &s;  // else evict the LRU one
    }
    if (!pick) return nullptr;
    pick->valid = false;     // hide it from lookups while the caller overwrites it
    pick->rangeKm = rangeKm; // best-effort hint so a second concurrent claim doesn't pick the same slot
    return pick->pixels;
}

void basemap_commit_slot(uint16_t *pixels, double lat, double lon, float rangeKm) {
    std::lock_guard<std::mutex> lock(s_mutex);
    for (auto &s : s_slots) {
        if (s.pixels == pixels) {
            s.lat = lat; s.lon = lon; s.rangeKm = rangeKm;
            s.valid = true; s.lastUsedMs = now_ms(); ++s.version;
            return;
        }
    }
}
