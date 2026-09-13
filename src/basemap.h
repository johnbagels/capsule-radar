#pragma once
// Full-canvas background raster for the "Map" radar theme (real OpenStreetMap street
// tiles, stitched and centered on the home position). Same double-buffer swap pattern
// as wx_radar.h: the network/sim side decodes into the back buffer, then commits it so
// the render side can pick up a consistent frame without locking during decode.
#include <stdint.h>

#define BASEMAP_W 466   // matches SCREEN_W/H (config.h) — one buffer, no per-theme crop
#define BASEMAP_H 466

void basemap_begin(void);
uint16_t *basemap_back_buffer(void);                      // network/sim: decode here
void basemap_commit(double lat, double lon, float rangeKm);
bool basemap_front(const uint16_t **pixels, double *lat, double *lon,
                   float *rangeKm, uint32_t *version);
