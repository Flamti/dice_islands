#include "systems/upkeep.hpp"

#include "dicecore/core.hpp"
#include "ecs/components_building.hpp"
#include "math/rng.hpp"

#include <algorithm>
#include <vector>

namespace dicecore::systems {

namespace {

// Еды на жителя здания за ход (SPEC §4 фаза 1, [баланс]).
constexpr int32_t kFoodPerBuilding = 1;

// Здание ест, пока в нём есть житель: разрушенные и недостроенные не едят.
bool consumes_food(int32_t status) {
    return status == static_cast<int32_t>(BuildingStatus::Active) ||
            status == static_cast<int32_t>(BuildingStatus::Starving) ||
            status == static_cast<int32_t>(BuildingStatus::Diseased) ||
            status == static_cast<int32_t>(BuildingStatus::Disabled);
}

// Голод перекрывает только рабочие статусы: Diseased/Disabled не затираются.
bool starvable(int32_t status) {
    return status == static_cast<int32_t>(BuildingStatus::Active) ||
            status == static_cast<int32_t>(BuildingStatus::Starving);
}

} // namespace

void apply_food_upkeep(entt::registry &registry) {
    const entt::entity match = registry.view<const ecs::MatchState>().front();
    auto *match_rng = registry.try_get<ecs::MatchRng>(match);
    if (match_rng == nullptr) {
        return; // партия без RNG — без зданий и апкипа (тесты этапа 2)
    }

    // Игроки в стабильном порядке ID — расход RNG детерминирован.
    std::vector<std::pair<int32_t, entt::entity>> players;
    for (auto [entity, info] : registry.view<const ecs::PlayerInfo>().each()) {
        players.push_back({info.id, entity});
    }
    std::sort(players.begin(), players.end());

    math::Rng rng(match_rng->state);
    bool rng_used = false;
    for (const auto &[player_id, player_entity] : players) {
        // Здания игрока в стабильном порядке ID сущностей.
        std::vector<entt::entity> eaters;
        for (auto [entity, building] : registry.view<ecs::Building>().each()) {
            if (building.player_id == player_id && consumes_food(building.status)) {
                eaters.push_back(entity);
            }
        }
        std::sort(eaters.begin(), eaters.end());

        auto &resources = registry.get<ecs::Resources>(player_entity);
        const int32_t fed = std::min(resources.food / kFoodPerBuilding,
                static_cast<int32_t>(eaters.size()));
        const int32_t hungry = static_cast<int32_t>(eaters.size()) - fed;
        resources.food -= fed * kFoodPerBuilding;

        if (hungry > 0) {
            // Случайный порядок отключения: тасование Фишера–Йетса RNG хоста.
            rng_used = true;
            for (int32_t i = static_cast<int32_t>(eaters.size()) - 1; i > 0; --i) {
                const int32_t j = rng.range_int(0, i);
                std::swap(eaters[i], eaters[j]);
            }
        }
        for (size_t i = 0; i < eaters.size(); ++i) {
            auto &building = registry.get<ecs::Building>(eaters[i]);
            if (!starvable(building.status)) {
                continue; // Diseased/Disabled сохраняют свой статус
            }
            // Первые hungry в перетасованном списке голодают, остальные сыты.
            building.status = static_cast<int32_t>(i < static_cast<size_t>(hungry)
                            ? BuildingStatus::Starving
                            : BuildingStatus::Active);
        }
    }
    if (rng_used) {
        match_rng->state = rng.raw_state();
    }
}

} // namespace dicecore::systems
