#include "basemap_client.h"
#include "basemap.h"
#include "net_fetch.h"
#include "config.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <PNGdec.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <string.h>
#include <new>

#define TILE_PX 256

static PNG *s_png = nullptr;

// Set just before decoding each tile; consumed by tile_png_line() to place that tile's
// pixels at the right spot in the (BASEMAP_W x BASEMAP_H) canvas.
static uint16_t *s_targetBuf = nullptr;
static double s_canvasWorldX0 = 0, s_canvasWorldY0 = 0;
static double s_tileWorldX0 = 0, s_tileWorldY0 = 0;
static uint32_t s_tilePixelsWritten = 0;

static bool ensure_decoder(void) {
    if (s_png) return true;
    void *mem = heap_caps_malloc(sizeof(PNG), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!mem) { Serial.println("[basemap] PSRAM decoder allocation failed"); return false; }
    s_png = new (mem) PNG();
    return true;
}

static int tile_png_line(PNGDRAW *draw) {
    uint16_t *dst = s_targetBuf;
    if (!dst) return 1;
    uint16_t line[TILE_PX];
    s_png->getLineAsRGB565(draw, line, PNG_RGB565_LITTLE_ENDIAN, 0x00000000);
    const int canvasY = (int)lround(s_tileWorldY0 + draw->y - s_canvasWorldY0);
    if (canvasY < 0 || canvasY >= BASEMAP_H) return 1;
    for (int x = 0; x < draw->iWidth; ++x) {
        const int canvasX = (int)lround(s_tileWorldX0 + x - s_canvasWorldX0);
        if (canvasX < 0 || canvasX >= BASEMAP_W) continue;
        dst[canvasY * BASEMAP_W + canvasX] = line[x];
        ++s_tilePixelsWritten;
    }
    return 1;
}

// Streams one 256x256 OSM tile straight into the canvas at (tileWorldX0,tileWorldY0).
// Ocean/land are both always present on OSM, so a fetch failure here means network
// trouble, not a missing tile — that tile's canvas region is simply left as-is.
static bool fetch_one_tile(int zoom, long txWrapped, long ty, double tileWorldX0, double tileWorldY0) {
    char url[128];
    snprintf(url, sizeof(url), "https://tile.openstreetmap.org/%d/%ld/%ld.png", zoom, txWrapped, ty);

    uint8_t *image = nullptr; size_t imageLen = 0;
    // OpenStreetMap's tile usage policy asks for a descriptive User-Agent and no bulk/
    // automated hammering — ADSB_USER_AGENT already identifies this device+build, and
    // basemap_fetch() only ever calls this a handful of times per home/range change.
    if (!net_fetch_psram(url, ADSB_USER_AGENT, &image, &imageLen, 60000, 3500, 6000)) {
        Serial.printf("[basemap] tile %d/%ld/%ld fetch failed\n", zoom, txWrapped, ty);
        return false;
    }

    s_tileWorldX0 = tileWorldX0;
    s_tileWorldY0 = tileWorldY0;
    s_tilePixelsWritten = 0;
    const int opened = s_png->openRAM(image, imageLen, tile_png_line);
    if (opened != PNG_SUCCESS) {
        Serial.printf("[basemap] tile %d/%ld/%ld PNG open error %d\n", zoom, txWrapped, ty, opened);
        heap_caps_free(image); return false;
    }
    const bool sizeOk = (s_png->getWidth() == TILE_PX && s_png->getHeight() == TILE_PX);
    const int decoded = sizeOk ? s_png->decode(nullptr, 0) : PNG_INVALID_PARAMETER;
    s_png->close();
    heap_caps_free(image);
    if (!sizeOk) { Serial.printf("[basemap] tile %d/%ld/%ld unexpected size\n", zoom, txWrapped, ty); return false; }
    if (decoded != PNG_SUCCESS) { Serial.printf("[basemap] tile %d/%ld/%ld decode error %d\n", zoom, txWrapped, ty, decoded); return false; }
    return s_tilePixelsWritten > 0;
}

bool basemap_fetch(double lat, double lon, float rangeKm) {
    if (WiFi.status() != WL_CONNECTED || !ensure_decoder() || rangeKm <= 0) return false;

    uint16_t *buf = basemap_claim_slot(rangeKm);   // evicts LRU if the cache is full
    if (!buf) return false;
    s_targetBuf = buf;

    // Pick a zoom whose meters-per-pixel roughly matches the radar scope's own scale
    // (rangeKm at the outer ring, RADAR_R_OUTER_PX pixels), so the street map lines up
    // with the rings drawn on top of it.
    const double latRad = lat * M_PI / 180.0;
    const double desiredMpp = (rangeKm * 1000.0) / (double)RADAR_R_OUTER_PX;
    double zoomF = log2(156543.03392 * cos(latRad) / desiredMpp);
    if (!(zoomF == zoomF)) zoomF = 12.0;              // NaN guard (e.g. cos(lat) underflow at the poles)
    int zoom = (int)lround(zoomF);
    if (zoom < 3)  zoom = 3;
    if (zoom > 17) zoom = 17;

    const long n = 1L << zoom;
    const double xtile = (lon + 180.0) / 360.0 * (double)n;
    const double ytile = (1.0 - log(tan(latRad) + 1.0 / cos(latRad)) / M_PI) / 2.0 * (double)n;
    const double worldPx = xtile * TILE_PX;
    const double worldPy = ytile * TILE_PX;
    const long centerTx = (long)floor(xtile);
    const long centerTy = (long)floor(ytile);

    s_canvasWorldX0 = worldPx - (double)BASEMAP_W / 2.0;
    s_canvasWorldY0 = worldPy - (double)BASEMAP_H / 2.0;

    memset(s_targetBuf, 0, BASEMAP_W * BASEMAP_H * sizeof(uint16_t));

    // A 3x3 block of 256px tiles is always enough to fully cover the 466x466 canvas
    // no matter where the home position falls within its own tile.
    int ok = 0, attempted = 0;
    for (long ty = centerTy - 1; ty <= centerTy + 1; ++ty) {
        if (ty < 0 || ty >= n) continue;              // off the top/bottom of the projection
        for (long tx = centerTx - 1; tx <= centerTx + 1; ++tx) {
            const long txWrapped = ((tx % n) + n) % n;  // wrap the antimeridian
            ++attempted;
            if (fetch_one_tile(zoom, txWrapped, ty, (double)tx * TILE_PX, (double)ty * TILE_PX)) ++ok;
            delay(120);                                 // be polite to the tile server
        }
    }
    Serial.printf("[basemap] zoom=%d tiles=%d/%d for %.5f,%.5f range=%.0fkm\n",
                  zoom, ok, attempted, lat, lon, (double)rangeKm);
    if (ok == 0) return false;                          // slot stays unclaimed/invalid, reused next time
    basemap_commit_slot(s_targetBuf, lat, lon, rangeKm);
    return true;
}
