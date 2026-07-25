#pragma once

#include "ecs/components_building.hpp"

#include <entt/entt.hpp>

#include <vector>

// Детектор смежности (SPEC §5): соседями считаются здания, чьи прямоугольники
// соприкасаются сторонами клеток (не углами). Используется мельницей, а позже
// болезнью и прочими синергиями.

namespace dicecore::systems {

// Соприкасаются ли два прямоугольника сторонами (не углами и без пересечения).
bool rects_side_adjacent(int32_t ax, int32_t az, int32_t aw, int32_t ad, int32_t bx, int32_t bz,
        int32_t bw, int32_t bd);

// Смежные с данным здания того же игрока (в порядке ID сущностей).
std::vector<entt::entity> adjacent_buildings(entt::registry &registry, entt::entity building);

} // namespace dicecore::systems
