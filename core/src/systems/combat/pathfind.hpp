#pragma once

#include <cstdint>
#include <utility>
#include <vector>

// A* по сетке боя (SPEC §9.3). Клетки зданий непроходимы; стены проходимы с
// доплатой за слом (HP/DPS), поэтому путь через стену выбирается, только если
// обход существенно дороже.

namespace dicecore::systems::combat {

// Сетка для поиска пути: стоимость входа в клетку.
struct PathGrid {
    int32_t cells_x = 0;
    int32_t cells_z = 0;
    // < 0 — непроходимо; иначе доп. стоимость входа (0 для обычной клетки,
    // >0 для стены = эквивалент времени слома в клетках пути).
    std::vector<float> extra_cost;

    bool in_bounds(int32_t x, int32_t z) const {
        return x >= 0 && x < cells_x && z >= 0 && z < cells_z;
    }
    float cost_at(int32_t x, int32_t z) const {
        return extra_cost[static_cast<size_t>(z) * cells_x + x];
    }
};

// Путь от (sx,sz) до ближайшей из целевых клеток. Возвращает список клеток
// (без стартовой, включая цель) либо пустой вектор, если пути нет.
// 4-связность, детерминированный разбор при равенстве.
std::vector<std::pair<int32_t, int32_t>> find_path(const PathGrid &grid, int32_t sx, int32_t sz,
        const std::vector<std::pair<int32_t, int32_t>> &goals);

} // namespace dicecore::systems::combat
