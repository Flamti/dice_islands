#pragma once

#include <array>
#include <cstdint>

// Simplex Noise 3D (реализация по Стефану Густавсону, public domain),
// сидируемая перестановочная таблица. Детерминирован по сиду.

namespace dicecore::math {

class SimplexNoise {
public:
    explicit SimplexNoise(uint64_t seed);

    // Значение шума в точке, диапазон примерно [-1, 1].
    float sample(float x, float y, float z) const;

    // Фрактальный шум (fBm): octaves слоёв с удвоением частоты.
    float fbm(float x, float y, float z, int octaves, float gain) const;

private:
    std::array<uint8_t, 512> perm_;
};

} // namespace dicecore::math
