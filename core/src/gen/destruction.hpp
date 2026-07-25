#pragma once

#include "gen/island_gen.hpp"

#include "math/rng.hpp"

#include <cstdint>
#include <vector>

// Деструктор ландшафта (SPEC §11.5): удаление клеток из строительной сетки
// (обрушение края, кратер). Работает на GridData; вырезанные клетки затем
// передаются генератору для ре-полигонизации меша.

namespace dicecore::gen {

// Выбор связного куска [min,max] крайних клеток острова для обрушения.
// protectedCells — клетки, которые нельзя включать (замок). Пусто, если места
// нет. Крайняя клетка = поверхность, граничащая с Void (SPEC §7.2, §11.5).
std::vector<GridCell> pick_edge_region(const GridData &grid,
        const std::vector<GridCell> &protected_cells, int32_t min_cells, int32_t max_cells,
        math::Rng &rng);

} // namespace dicecore::gen
