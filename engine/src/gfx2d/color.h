#pragma once
#include <cstdint>
#include <string>

namespace g2d {

struct Color {
    int r = 0, g = 0, b = 0;
    double a = 1;
};

Color parse_color(const std::string& css);
std::string css_rgba(int r, int g, int b, double a);
std::string css_hex(int r, int g, int b);
uint32_t premul_rgba(const Color& c, double opacity = 1.0);

}
