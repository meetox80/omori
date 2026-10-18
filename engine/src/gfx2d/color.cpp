#include "color.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace g2d {

static int hexv(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

struct Named {
    const char* name;
    int r, g, b;
};

static const Named NAMED[] = {{"black", 0, 0, 0},       {"white", 255, 255, 255}, {"red", 255, 0, 0},     {"green", 0, 128, 0},
                              {"blue", 0, 0, 255},      {"yellow", 255, 255, 0},  {"cyan", 0, 255, 255},  {"magenta", 255, 0, 255},
                              {"gray", 128, 128, 128},  {"grey", 128, 128, 128},  {"orange", 255, 165, 0}, {"purple", 128, 0, 128},
                              {"pink", 255, 192, 203},  {"lime", 0, 255, 0},      {"silver", 192, 192, 192}, {"maroon", 128, 0, 0},
                              {"navy", 0, 0, 128},      {"teal", 0, 128, 128},    {"olive", 128, 128, 0}, {"brown", 165, 42, 42},
                              {"gold", 255, 215, 0},    {"darkgray", 169, 169, 169}, {"lightgray", 211, 211, 211}, {nullptr, 0, 0, 0}};

Color parse_color(const std::string& in) {
    Color c;
    std::string s;
    for (char ch : in) {
        if (ch != ' ' && ch != '\t') s += (char)tolower((unsigned char)ch);
    }
    if (s.empty()) return c;
    if (s[0] == '#') {
        if (s.size() == 4 || s.size() == 5) {
            c.r = hexv(s[1]) * 17;
            c.g = hexv(s[2]) * 17;
            c.b = hexv(s[3]) * 17;
            if (s.size() == 5) c.a = hexv(s[4]) * 17 / 255.0;
        } else if (s.size() >= 7) {
            c.r = hexv(s[1]) * 16 + hexv(s[2]);
            c.g = hexv(s[3]) * 16 + hexv(s[4]);
            c.b = hexv(s[5]) * 16 + hexv(s[6]);
            if (s.size() >= 9) c.a = (hexv(s[7]) * 16 + hexv(s[8])) / 255.0;
        }
        return c;
    }
    if (!s.compare(0, 4, "rgba") || !s.compare(0, 3, "rgb")) {
        size_t p = s.find('(');
        if (p == std::string::npos) return c;
        double v[4] = {0, 0, 0, 1};
        int n = 0;
        const char* q = s.c_str() + p + 1;
        while (*q && *q != ')' && n < 4) {
            char* end;
            double d = std::strtod(q, &end);
            if (end == q) break;
            if (*end == '%') {
                d = n < 3 ? d * 2.55 : d / 100.0;
                end++;
            }
            v[n++] = d;
            q = end;
            while (*q == ',' || *q == '/') q++;
        }
        c.r = std::clamp((int)std::lround(v[0]), 0, 255);
        c.g = std::clamp((int)std::lround(v[1]), 0, 255);
        c.b = std::clamp((int)std::lround(v[2]), 0, 255);
        c.a = std::clamp(v[3], 0.0, 1.0);
        return c;
    }
    if (s == "transparent") {
        c.a = 0;
        return c;
    }
    for (int i = 0; NAMED[i].name; i++) {
        if (s == NAMED[i].name) {
            c.r = NAMED[i].r;
            c.g = NAMED[i].g;
            c.b = NAMED[i].b;
            return c;
        }
    }
    return c;
}

std::string css_rgba(int r, int g, int b, double a) {
    char buf[96];
    char abuf[32];
    std::snprintf(abuf, sizeof(abuf), "%g", a);
    std::snprintf(buf, sizeof(buf), "rgba(%d,%d,%d,%s)", r, g, b, abuf);
    return buf;
}

std::string css_hex(int r, int g, int b) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", r & 255, g & 255, b & 255);
    return buf;
}

uint32_t premul_rgba(const Color& c, double opacity) {
    int a = std::clamp((int)std::lround(c.a * opacity * 255.0), 0, 255);
    int r = c.r * a / 255, g = c.g * a / 255, b = c.b * a / 255;
    return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | ((uint32_t)a << 24);
}

}
