#pragma once
#include <cstddef>
#include <cstdint>

bool jpeg_soft_decode(const uint8_t* jpeg, size_t len, uint32_t* dst, int stride, int rows);
