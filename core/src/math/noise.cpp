#include "math/noise.hpp"

#include "math/rng.hpp"

#include <cmath>

namespace dicecore::math {

namespace {

// Градиенты 3D-симплекса: 12 рёбер куба.
constexpr int8_t kGrad3[12][3] = {
    {1, 1, 0}, {-1, 1, 0}, {1, -1, 0}, {-1, -1, 0},
    {1, 0, 1}, {-1, 0, 1}, {1, 0, -1}, {-1, 0, -1},
    {0, 1, 1}, {0, -1, 1}, {0, 1, -1}, {0, -1, -1},
};

constexpr float kSkew3 = 1.0f / 3.0f; // (sqrt(4)-1)/3 для 3D
constexpr float kUnskew3 = 1.0f / 6.0f;
// Нормировка амплитуды до ~[-1, 1] (константа Густавсона для 3D).
constexpr float kAmplitude3 = 32.0f;

float grad_dot(uint8_t hash, float x, float y, float z) {
    const int8_t *g = kGrad3[hash % 12];
    return g[0] * x + g[1] * y + g[2] * z;
}

float corner_contribution(float t, uint8_t hash, float x, float y, float z) {
    if (t < 0.0f) {
        return 0.0f;
    }
    const float t2 = t * t;
    return t2 * t2 * grad_dot(hash, x, y, z);
}

} // namespace

SimplexNoise::SimplexNoise(uint64_t seed) {
    // Fisher–Yates по детерминированному RNG.
    std::array<uint8_t, 256> table;
    for (int i = 0; i < 256; ++i) {
        table[i] = static_cast<uint8_t>(i);
    }
    Rng rng(seed);
    for (int i = 255; i > 0; --i) {
        const int j = rng.range_int(0, i);
        const uint8_t tmp = table[i];
        table[i] = table[j];
        table[j] = tmp;
    }
    for (int i = 0; i < 512; ++i) {
        perm_[i] = table[i & 255];
    }
}

float SimplexNoise::sample(float x, float y, float z) const {
    // Скос координат в решётку симплексов.
    const float s = (x + y + z) * kSkew3;
    const int i = static_cast<int>(std::floor(x + s));
    const int j = static_cast<int>(std::floor(y + s));
    const int k = static_cast<int>(std::floor(z + s));
    const float t = (i + j + k) * kUnskew3;
    const float x0 = x - (i - t);
    const float y0 = y - (j - t);
    const float z0 = z - (k - t);

    // Выбор симплекса (порядок убывания координат).
    int i1, j1, k1, i2, j2, k2;
    if (x0 >= y0) {
        if (y0 >= z0) {
            i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 1; k2 = 0;
        } else if (x0 >= z0) {
            i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 0; k2 = 1;
        } else {
            i1 = 0; j1 = 0; k1 = 1; i2 = 1; j2 = 0; k2 = 1;
        }
    } else {
        if (y0 < z0) {
            i1 = 0; j1 = 0; k1 = 1; i2 = 0; j2 = 1; k2 = 1;
        } else if (x0 < z0) {
            i1 = 0; j1 = 1; k1 = 0; i2 = 0; j2 = 1; k2 = 1;
        } else {
            i1 = 0; j1 = 1; k1 = 0; i2 = 1; j2 = 1; k2 = 0;
        }
    }

    const float x1 = x0 - i1 + kUnskew3;
    const float y1 = y0 - j1 + kUnskew3;
    const float z1 = z0 - k1 + kUnskew3;
    const float x2 = x0 - i2 + 2.0f * kUnskew3;
    const float y2 = y0 - j2 + 2.0f * kUnskew3;
    const float z2 = z0 - k2 + 2.0f * kUnskew3;
    const float x3 = x0 - 1.0f + 3.0f * kUnskew3;
    const float y3 = y0 - 1.0f + 3.0f * kUnskew3;
    const float z3 = z0 - 1.0f + 3.0f * kUnskew3;

    const int ii = i & 255;
    const int jj = j & 255;
    const int kk = k & 255;
    const uint8_t h0 = perm_[ii + perm_[jj + perm_[kk]]];
    const uint8_t h1 = perm_[ii + i1 + perm_[jj + j1 + perm_[kk + k1]]];
    const uint8_t h2 = perm_[ii + i2 + perm_[jj + j2 + perm_[kk + k2]]];
    const uint8_t h3 = perm_[ii + 1 + perm_[jj + 1 + perm_[kk + 1]]];

    float total = 0.0f;
    total += corner_contribution(0.6f - x0 * x0 - y0 * y0 - z0 * z0, h0, x0, y0, z0);
    total += corner_contribution(0.6f - x1 * x1 - y1 * y1 - z1 * z1, h1, x1, y1, z1);
    total += corner_contribution(0.6f - x2 * x2 - y2 * y2 - z2 * z2, h2, x2, y2, z2);
    total += corner_contribution(0.6f - x3 * x3 - y3 * y3 - z3 * z3, h3, x3, y3, z3);
    return kAmplitude3 * total;
}

float SimplexNoise::fbm(float x, float y, float z, int octaves, float gain) const {
    float total = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float max_total = 0.0f;
    for (int octave = 0; octave < octaves; ++octave) {
        total += sample(x * frequency, y * frequency, z * frequency) * amplitude;
        max_total += amplitude;
        amplitude *= gain;
        frequency *= 2.0f;
    }
    return max_total > 0.0f ? total / max_total : 0.0f;
}

} // namespace dicecore::math
