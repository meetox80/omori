#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace bytes {

constexpr bool BIG_HOST = __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__;

inline uint32_t le32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

inline uint64_t le64(const uint8_t* p) { return (uint64_t)le32(p) | ((uint64_t)le32(p + 4) << 32); }

inline void put_le32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

inline double le_double(const uint8_t* p) {
    uint64_t bits = le64(p);
    double d;
    std::memcpy(&d, &bits, 8);
    return d;
}

inline void le16_to_host(int16_t* samples, size_t count) {
    if (!BIG_HOST) return;
    for (size_t i = 0; i < count; i++) {
        uint16_t v = (uint16_t)samples[i];
        samples[i] = (int16_t)(uint16_t)((v >> 8) | (v << 8));
    }
}

}
