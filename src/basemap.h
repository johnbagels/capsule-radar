#pragma once
// A small LRU cache of full-canvas OpenStreetMap rasters for the "Map" radar theme, keyed
// by (home lat, home lon, range km). Home position only ever changes via the settings
// "Save & restart" flow (a reboot), so it's effectively constant for the process lifetime
// — the only thing that changes live is which range you're viewing (on-screen zoom button).
// That means once a range has been fetched, switching back to it should be instant, with
// zero network wait: look it up, don't refetch it.
#include <stdint.h>

#define BASEMAP_W 466   // matches SCREEN_W/H (config.h) — one buffer per slot, no per-theme crop
#define BASEMAP_H 466

// How many distinct range levels to keep cached in PSRAM at once (LRU-evicted beyond
// this). Each slot costs BASEMAP_W*BASEMAP_H*2 bytes (~424 KB — see docs/HARDWARE.md for
// the 8 MB PSRAM budget this competes with). 3 covers "flicking between a couple of
// favourite ranges" comfortably; bump it if `PSRAM free` in the serial log has headroom.
#define BASEMAP_CACHE_SLOTS 3

void basemap_begin(void);

// Cache lookup — cheap, thread-safe, no network. Call this straight from the UI thread
// on a range change for an instant switch. False means nothing cached for this exact
// (lat,lon,rangeKm) yet; the network/sim side needs to fetch it (see below).
bool basemap_lookup(double lat, double lon, float rangeKm,
                    const uint16_t **pixels, uint32_t *version);

// Network/sim side: claim a scratch buffer to decode a fresh map into (evicts the
// least-recently-used slot if the cache is full), then commit it once decoding succeeds.
// On failure just drop it — don't call commit; the slot is simply not valid yet and will
// be reclaimed on the next claim.
uint16_t *basemap_claim_slot(float rangeKm);
void basemap_commit_slot(uint16_t *pixels, double lat, double lon, float rangeKm);
