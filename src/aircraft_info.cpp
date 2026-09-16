// Shared aircraft-info state. Mirrors route.cpp's mutex-guarded cross-thread handoff.
#include "aircraft_info.h"
#include <string.h>
#include <stdio.h>
#include <mutex>

static std::mutex s_m;
static char s_want[10]    = "";   // hex the UI asked about
static char s_doneHex[10] = "";   // hex the stored result belongs to
static char s_reg[16]     = "";
static char s_model[40]   = "";
static char s_operator[40]= "";
static uint32_t s_ageSec  = 0;

void aircraft_info_request(const char *hex) {
    std::lock_guard<std::mutex> g(s_m);
    snprintf(s_want, sizeof(s_want), "%s", hex ? hex : "");
}

bool aircraft_info_pending(char *hexOut, size_t n) {
    std::lock_guard<std::mutex> g(s_m);
    if (s_want[0] && strcmp(s_want, s_doneHex) != 0) {
        snprintf(hexOut, n, "%s", s_want);
        return true;
    }
    return false;
}

// Fold UTF-8 Latin accents (á, ñ, ü...) to ASCII so the LVGL font can render them
// (Montserrat has no accented glyphs -> they would show as a missing-glyph box).
static void ascii_fold(const char *in, char *out, size_t n) {
    size_t o = 0;
    for (size_t i = 0; in && in[i] && o + 1 < n;) {
        const unsigned char c = (unsigned char)in[i];
        if (c < 0x80) { out[o++] = in[i++]; continue; }
        if (c == 0xC3 && in[i + 1]) {            // Latin-1 Supplement
            const unsigned char d = (unsigned char)in[i + 1];
            char r;
            if      (d >= 0x80 && d <= 0x85) r = 'A';
            else if (d >= 0xA0 && d <= 0xA5) r = 'a';
            else if (d == 0x87)              r = 'C';
            else if (d == 0xA7)              r = 'c';
            else if (d >= 0x88 && d <= 0x8B) r = 'E';
            else if (d >= 0xA8 && d <= 0xAB) r = 'e';
            else if (d >= 0x8C && d <= 0x8F) r = 'I';
            else if (d >= 0xAC && d <= 0xAF) r = 'i';
            else if (d == 0x91)              r = 'N';
            else if (d == 0xB1)              r = 'n';
            else if (d >= 0x92 && d <= 0x96) r = 'O';
            else if (d >= 0xB2 && d <= 0xB6) r = 'o';
            else if (d >= 0x99 && d <= 0x9C) r = 'U';
            else if (d >= 0xB9 && d <= 0xBC) r = 'u';
            else if (d == 0x9F)              r = 's';   // ß
            else                             r = '?';
            out[o++] = r; i += 2; continue;
        }
        ++i;                                     // other multibyte: skip the sequence
        while ((unsigned char)in[i] >= 0x80 && (unsigned char)in[i] < 0xC0) ++i;
    }
    out[o] = 0;
}

void aircraft_info_store(const char *hex, const char *reg, const char *model,
                         const char *operatorName, uint32_t ageSec) {
    std::lock_guard<std::mutex> g(s_m);
    snprintf(s_doneHex, sizeof(s_doneHex), "%s", hex ? hex : "");
    ascii_fold(reg,          s_reg,      sizeof(s_reg));
    ascii_fold(model,        s_model,    sizeof(s_model));
    ascii_fold(operatorName, s_operator, sizeof(s_operator));
    s_ageSec = ageSec;
}

bool aircraft_info_get(const char *hex, char *reg, size_t rn, char *model, size_t mn,
                       char *operatorName, size_t on, uint32_t *ageSec) {
    std::lock_guard<std::mutex> g(s_m);
    if (hex && s_doneHex[0] && strcmp(hex, s_doneHex) == 0) {
        snprintf(reg, rn, "%s", s_reg);
        snprintf(model, mn, "%s", s_model);
        snprintf(operatorName, on, "%s", s_operator);
        if (ageSec) *ageSec = s_ageSec;
        return true;
    }
    return false;
}
