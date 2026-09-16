#pragma once
// Shared route state (origin -> destination by callsign). Portable: the UI thread
// requests a lookup, a network task fulfils it, the UI reads the result.
#include <stddef.h>
#include <stdint.h>

void route_request(const char *callsign);                     // UI: want a route for this callsign
bool route_pending(char *callOut, size_t n);                  // task: is a lookup needed? returns callsign
// ageSec: 0 if this came from a live adsbdb fetch this session, otherwise how old the
// on-device NVS cache entry is (see route_client.h's route_cache_get) — lets the UI show a
// subtle "this might be stale" marker rather than presenting a cached route as certain.
void route_store(const char *callsign, const char *from, const char *to, uint32_t ageSec);  // task/sim: store result
bool route_get(const char *callsign, char *from, size_t fn, char *to, size_t tn, uint32_t *ageSec); // UI: read result
