#include "systems/expansion.hpp"

#include "ecs/components_building.hpp"
#include "ecs/components_research.hpp"
#include "gen/island_gen.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace dicecore::systems {

namespace {

entt::entity match_entity(const entt::registry &registry) {
    return registry.view<const ecs::MatchState>().front();
}

entt::entity player_by_id(const entt::registry &registry, int32_t player_id) {
    for (auto [entity, info] : registry.view<const ecs::PlayerInfo>().each()) {
        if (info.id == player_id) {
            return entity;
        }
    }
    return entt::null;
}

// Кандидат в клетку-леса: расстояние до платформы (для порядка отбора).
struct Candidate {
    float dist;
    int32_t cx;
    int32_t cz;
};

// Расстояние от клетки до прямоугольника платформы в клетках (0 внутри).
float distance_to_rect(int32_t cx, int32_t cz, int32_t px0, int32_t pz0, int32_t px1, int32_t pz1) {
    const int32_t ddx = std::max({px0 - cx, cx - px1, 0});
    const int32_t ddz = std::max({pz0 - cz, cz - pz1, 0});
    return std::sqrt(static_cast<float>(ddx * ddx + ddz * ddz));
}

} // namespace

void expand_platforms(entt::registry &registry, std::vector<Event> &events) {
    const auto *catalog = registry.try_get<const ecs::BuildingCatalog>(match_entity(registry));
    if (catalog == nullptr) {
        return;
    }

    // Платформы в порядке ID сущностей — детерминированный отбор клеток.
    std::vector<entt::entity> platforms;
    for (auto [entity, building] : registry.view<const ecs::Building>().each()) {
        const ecs::BuildingDef &def = catalog->defs[building.def_index];
        if (def.expansion_radius > 0 && !building.expanded &&
                building.status == static_cast<int32_t>(BuildingStatus::Active)) {
            platforms.push_back(entity);
        }
    }
    std::sort(platforms.begin(), platforms.end());

    for (const entt::entity platform_entity : platforms) {
        auto &platform = registry.get<ecs::Building>(platform_entity);
        const ecs::BuildingDef &def = catalog->defs[platform.def_index];
        const entt::entity player_entity = player_by_id(registry, platform.player_id);
        if (player_entity == entt::null) {
            continue;
        }
        auto &grid = registry.get<ecs::PlayerGrid>(player_entity);

        const int32_t px0 = platform.cell_x;
        const int32_t pz0 = platform.cell_z;
        const int32_t px1 = platform.cell_x + platform.size_x - 1;
        const int32_t pz1 = platform.cell_z + platform.size_z - 1;
        const float radius = static_cast<float>(def.expansion_radius);

        // Высота лесов — уровень платформы (клетки-леса парят рядом с ней).
        const float scaffold_height = grid.heights[grid.index_of(platform.cell_x, platform.cell_z)];

        std::vector<Candidate> candidates;
        const int32_t r = def.expansion_radius;
        for (int32_t cz = pz0 - r; cz <= pz1 + r; ++cz) {
            for (int32_t cx = px0 - r; cx <= px1 + r; ++cx) {
                if (!grid.in_bounds(cx, cz)) {
                    continue;
                }
                // Только там, где нет поверхности острова (SPEC §11.4).
                if (grid.types[grid.index_of(cx, cz)] != static_cast<int32_t>(gen::CellType::Void)) {
                    continue;
                }
                const float dist = distance_to_rect(cx, cz, px0, pz0, px1, pz1);
                if (dist > 0.0f && dist <= radius) {
                    candidates.push_back({dist, cx, cz});
                }
            }
        }
        // Ближе к платформе — раньше; при равенстве по (z, x) для детерминизма.
        std::sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) {
            if (a.dist != b.dist) {
                return a.dist < b.dist;
            }
            return a.cz != b.cz ? a.cz < b.cz : a.cx < b.cx;
        });

        // Инженерия (SPEC §8): платформа даёт +% клеток-лесов.
        int32_t limit = def.expansion_max_cells;
        const auto *research_catalog = registry.try_get<const ecs::ResearchCatalog>(match_entity(registry));
        const auto *player_research = registry.try_get<const ecs::PlayerResearch>(player_entity);
        if (research_catalog != nullptr && player_research != nullptr &&
                player_research->has(ecs::kResearchEngineering)) {
            const int32_t percent = research_catalog->param(ecs::kResearchEngineering,
                    "expansion_percent", 0);
            limit += limit * percent / 100;
        }

        int32_t added = 0;
        for (const Candidate &c : candidates) {
            if (added >= limit) {
                break;
            }
            const size_t index = grid.index_of(c.cx, c.cz);
            grid.types[index] = static_cast<int32_t>(gen::CellType::Scaffold);
            grid.heights[index] = scaffold_height;
            ++added;
        }
        platform.expanded = true;

        Event event;
        event.type = kEventExpansion;
        event.payload["player"] = std::to_string(platform.player_id);
        event.payload["cells"] = std::to_string(added);
        events.push_back(std::move(event));
    }
}

} // namespace dicecore::systems
