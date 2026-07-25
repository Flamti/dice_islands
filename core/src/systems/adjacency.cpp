#include "systems/adjacency.hpp"

#include <algorithm>

namespace dicecore::systems {

namespace {

// Пересечение полуинтервалов [a0, a1) и [b0, b1) непусто.
bool ranges_overlap(int32_t a0, int32_t a1, int32_t b0, int32_t b1) {
    return a0 < b1 && b0 < a1;
}

} // namespace

bool rects_side_adjacent(int32_t ax, int32_t az, int32_t aw, int32_t ad, int32_t bx, int32_t bz,
        int32_t bw, int32_t bd) {
    const int32_t ax1 = ax + aw;
    const int32_t az1 = az + ad;
    const int32_t bx1 = bx + bw;
    const int32_t bz1 = bz + bd;

    // Касание по вертикальной стороне: смежные по X, перекрытие по Z.
    const bool touch_x = (ax1 == bx || bx1 == ax) && ranges_overlap(az, az1, bz, bz1);
    // Касание по горизонтальной стороне: смежные по Z, перекрытие по X.
    const bool touch_z = (az1 == bz || bz1 == az) && ranges_overlap(ax, ax1, bx, bx1);
    return touch_x || touch_z;
}

std::vector<entt::entity> adjacent_buildings(entt::registry &registry, entt::entity building) {
    const auto &self = registry.get<const ecs::Building>(building);
    std::vector<entt::entity> result;
    for (auto [entity, other] : registry.view<const ecs::Building>().each()) {
        if (entity == building || other.player_id != self.player_id) {
            continue;
        }
        if (rects_side_adjacent(self.cell_x, self.cell_z, self.size_x, self.size_z, other.cell_x,
                    other.cell_z, other.size_x, other.size_z)) {
            result.push_back(entity);
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

} // namespace dicecore::systems
