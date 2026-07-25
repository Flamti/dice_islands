#pragma once

#include <cstdint>

// Детерминированный RNG ядра (splitmix64): одинаковая последовательность на
// любой платформе при одном сиде. std::mt19937 не используется, чтобы не
// зависеть от реализации стандартной библиотеки.

namespace dicecore::math {

class Rng {
public:
    explicit Rng(uint64_t seed) : state_(seed) {}

    uint64_t next_u64() {
        state_ += 0x9E3779B97F4A7C15ull;
        uint64_t z = state_;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    uint32_t next_u32() {
        return static_cast<uint32_t>(next_u64() >> 32);
    }

    // Целое из [min, max] включительно. Модульное смещение пренебрежимо
    // для игровых диапазонов (куда меньше 2^32).
    int32_t range_int(int32_t min, int32_t max) {
        if (max <= min) {
            return min;
        }
        const uint64_t span = static_cast<uint64_t>(max) - static_cast<uint64_t>(min) + 1;
        return static_cast<int32_t>(min + static_cast<int64_t>(next_u64() % span));
    }

    // Вещественное из [min, max): 53 старших бита мантиссы.
    double range_double(double min, double max) {
        const double unit = static_cast<double>(next_u64() >> 11) * (1.0 / 9007199254740992.0);
        return min + unit * (max - min);
    }

    // Сырое состояние: для хранения RNG в компоненте партии и в снапшотах.
    uint64_t raw_state() const {
        return state_;
    }

private:
    uint64_t state_;
};

// Смешивание сида с солью (для производных сидов: остров игрока N и т.п.).
inline uint64_t mix_seed(uint64_t seed, uint64_t salt) {
    Rng rng(seed ^ (salt * 0xD6E8FEB86659FD93ull));
    return rng.next_u64();
}

} // namespace dicecore::math
