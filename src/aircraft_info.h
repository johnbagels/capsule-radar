#pragma once
// Shared aircraft-info state (registration / model / manufacturer / operator by ICAO hex).
// Portable: mirrors route.h's request/pending/store/get pattern exactly — the UI thread
// requests a lookup, a network task fulfils it, the UI reads the result.
#include <stddef.h>
#include <stdint.h>

void aircraft_info_request(const char *hex);                  // UI: want info for this aircraft
bool aircraft_info_pending(char *hexOut, size_t n);            // task: is a lookup needed? returns hex
// ageSec: 0 if this came from a live adsbdb fetch this session, otherwise how old the NVS
// cache entry is. Unlike routes, an aircraft's identity barely ever changes, so this is
// mostly informational rather than a "trust this less" signal.
void aircraft_info_store(const char *hex, const char *reg, const char *model,
                         const char *operatorName, uint32_t ageSec);        // task/sim: store result
bool aircraft_info_get(const char *hex, char *reg, size_t rn, char *model, size_t mn,
                       char *operatorName, size_t on, uint32_t *ageSec);    // UI: read result
