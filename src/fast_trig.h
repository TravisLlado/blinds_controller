// fast_trig.h ////////////////////////////////////////////////////////////////
#pragma once
#include <array>
#include <cstdint>
#include <cmath>

// =======================================================
// Configuration
// =======================================================
constexpr int SIN_LUT_BITS  = 10;                     // 2^10 = 1024 entries
constexpr int SIN_LUT_SIZE  = (1 << SIN_LUT_BITS);
constexpr float INDEX_SCALE = SIN_LUT_SIZE / TWO_PI; // use Arduino TWO_PI macro

// =======================================================
// Q15 sine table (int16_t, -32768..32767)
// Generated at compile-time using constexpr lambda
// =======================================================
static constexpr std::array<int16_t, SIN_LUT_SIZE> sin_lut = []() {
    std::array<int16_t, SIN_LUT_SIZE> table{};
    for (int i = 0; i < SIN_LUT_SIZE; ++i) {
        float angle = (TWO_PI * i) / SIN_LUT_SIZE;
        table[i] = static_cast<int16_t>(std::round(std::sin(angle) * 32767.0f));
    }
    return table;
}();

// =======================================================
// Fast sine: input in radians, output in [-1,1]
// Simple table lookup (no interpolation)
// =======================================================
inline float fast_sin(float x) {
    x = fmodf(x, TWO_PI);
    if (x < 0.0f) x += TWO_PI;

    int idx = static_cast<int>(x * INDEX_SCALE) & (SIN_LUT_SIZE - 1);
    return static_cast<float>(sin_lut[idx]) * (1.0f / 32767.0f);
}

// =======================================================
// Optional: fast sine with linear interpolation
// Smoother output, slightly more computation
// =======================================================
inline float fast_sin_interp(float x) {
    x = fmodf(x, TWO_PI);
    if (x < 0.0f) x += TWO_PI;

    float f_idx = x * INDEX_SCALE;
    int idx = static_cast<int>(f_idx) & (SIN_LUT_SIZE - 1);
    int next_idx = (idx + 1) & (SIN_LUT_SIZE - 1);

    float frac = f_idx - static_cast<float>(idx);
    float val0 = static_cast<float>(sin_lut[idx]);
    float val1 = static_cast<float>(sin_lut[next_idx]);
    return ((1.0f - frac) * val0 + frac * val1) * (1.0f / 32767.0f);
}
