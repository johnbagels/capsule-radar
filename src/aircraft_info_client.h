#pragma once
// Look up registration / model / operator by ICAO24 hex via adsbdb.com — the same free,
// no-key API already used for route lookups (api.adsbdb.com/v0/aircraft/{MODE_S}).
// Device-only (uses WiFi/HTTPS).
#include <stddef.h>
#include <stdint.h>

bool aircraft_info_fetch(const char *hex, char *reg, size_t rn, char *model, size_t mn,
                         char *operatorName, size_t on);

// NVS cache (avoids re-querying adsbdb for the same aircraft across reboots). Unlike routes,
// an aircraft's registration/model/operator essentially never changes, so this uses a much
// longer TTL — see aircraft_info_client.cpp.
void aircraft_info_cache_begin();   // call once at boot; clears the cache if the format changed
bool aircraft_info_cache_get(const char *hex, char *reg, size_t rn, char *model, size_t mn,
                             char *operatorName, size_t on, uint32_t *ageSec);
void aircraft_info_cache_put(const char *hex, const char *reg, const char *model,
                             const char *operatorName);
