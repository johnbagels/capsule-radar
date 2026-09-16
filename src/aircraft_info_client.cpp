// Aircraft-info lookup via adsbdb.com (free, no API key): GET /v0/aircraft/{MODE_S}.
// Returns registration, full model name, and the registered operator/owner. Device-only.
#include "aircraft_info_client.h"
#include "config.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <string.h>
#include <time.h>   // cache TTL

#define AIRCRAFT_INFO_CACHE_MAX 200   // wrap the cache before it can crowd NVS

#define AIRCRAFT_INFO_FMT_VER 1   // bump to invalidate cached entries when the format changes

void aircraft_info_cache_begin() {
    Preferences p;
    if (!p.begin("acinfo", false)) return;
    if (p.getUChar("__v", 0) != AIRCRAFT_INFO_FMT_VER) { p.clear(); p.putUChar("__v", AIRCRAFT_INFO_FMT_VER); }
    p.end();
}

bool aircraft_info_cache_get(const char *hex, char *reg, size_t rn, char *model, size_t mn,
                             char *operatorName, size_t on, uint32_t *ageSec) {
    if (rn) reg[0] = 0;
    if (mn) model[0] = 0;
    if (on) operatorName[0] = 0;
    if (ageSec) *ageSec = 0;
    if (!hex || !hex[0]) return false;
    Preferences p;
    if (!p.begin("acinfo", true)) return false;
    String v = p.getString(hex, "");     // stored as "epoch|reg|model|operator"
    p.end();
    if (v.length() == 0) return false;
    const int b1 = v.indexOf('|');
    if (b1 < 0) return false;
    const uint32_t ts = (uint32_t)v.substring(0, b1).toInt();
    const String rest = v.substring(b1 + 1);
    const int b2 = rest.indexOf('|');
    if (b2 < 0) return false;
    const String rest2 = rest.substring(b2 + 1);
    const int b3 = rest2.indexOf('|');
    if (b3 < 0) return false;
    const uint32_t now = (uint32_t)time(nullptr);
    // An aircraft's registration/model/operator essentially never changes (unlike a
    // callsign's route), so this is a long TTL purely to eventually pick up re-registrations
    // or database corrections — not a "this might be stale" concern like routes are.
    if (now > 1700000000UL && ts > 1700000000UL && (now - ts) > 30UL * 86400UL) return false;
    snprintf(reg, rn, "%s", rest.substring(0, b2).c_str());
    snprintf(model, mn, "%s", rest2.substring(0, b3).c_str());
    snprintf(operatorName, on, "%s", rest2.substring(b3 + 1).c_str());
    if (ageSec && now > 1700000000UL && ts > 1700000000UL && now >= ts) *ageSec = now - ts;
    return true;
}

void aircraft_info_cache_put(const char *hex, const char *reg, const char *model,
                             const char *operatorName) {
    if (!hex || !hex[0]) return;
    Preferences p;
    if (!p.begin("acinfo", false)) return;
    int n = p.getInt("__n", 0);
    if (n >= AIRCRAFT_INFO_CACHE_MAX) { p.clear(); p.putUChar("__v", AIRCRAFT_INFO_FMT_VER); n = 0; }
    String v = String((uint32_t)time(nullptr)) + "|" + String(reg ? reg : "") + "|" +
               String(model ? model : "") + "|" + String(operatorName ? operatorName : "");
    if (p.putString(hex, v) > 0) p.putInt("__n", n + 1);
    p.end();
}

bool aircraft_info_fetch(const char *hex, char *reg, size_t rn, char *model, size_t mn,
                         char *operatorName, size_t on) {
    if (rn) reg[0] = 0;
    if (mn) model[0] = 0;
    if (on) operatorName[0] = 0;
    if (!hex || !hex[0] || WiFi.status() != WL_CONNECTED) return false;

    char url[96];
    snprintf(url, sizeof(url), "https://api.adsbdb.com/v0/aircraft/%s", hex);

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setReuse(false);
    http.setConnectTimeout(3000);   // short: runs on the feed task, don't stall the live poll
    http.setTimeout(6000);
    if (!http.begin(client, url)) return false;
    // MUST be setUserAgent(): addHeader() silently drops User-Agent (it is on
    // HTTPClient's "handled by code" list), leaving the default "ESP32HTTPClient".
    http.setUserAgent(ADSB_USER_AGENT);

    const int code = http.GET();
    if (code != 200) { http.end(); return false; }

    JsonDocument filter;
    filter["response"]["aircraft"]["type"] = true;              // full model name, e.g. "Boeing 777-36N"
    filter["response"]["aircraft"]["registration"] = true;
    filter["response"]["aircraft"]["registered_owner"] = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, http.getStream(),
                                               DeserializationOption::Filter(filter));
    http.end();
    if (err) return false;

    JsonObjectConst ac = doc["response"]["aircraft"].as<JsonObjectConst>();
    if (ac.isNull()) return false;   // unknown hex, etc.

    snprintf(reg, rn, "%s", (const char *)(ac["registration"] | ""));
    snprintf(model, mn, "%s", (const char *)(ac["type"] | ""));
    snprintf(operatorName, on, "%s", (const char *)(ac["registered_owner"] | ""));
    return (reg[0] || model[0] || operatorName[0]);
}
