#include "systems/economy.hpp"

#include "ecs/components.hpp"
#include "ecs/components_building.hpp"
#include "ecs/components_research.hpp"

#include <algorithm>

namespace dicecore::systems {

namespace {

// Кап с сжиганием излишка; cap <= 0 — без лимита (SPEC §3).
int32_t clamp_to_cap(int32_t value, int32_t cap) {
    return cap > 0 ? std::min(value, cap) : value;
}

} // namespace

ecs::Resources effective_caps(const entt::registry &registry, entt::entity match,
        entt::entity player) {
    ecs::Resources caps;
    const auto *dice_catalog = registry.try_get<const ecs::DiceCatalog>(match);
    if (dice_catalog != nullptr) {
        caps = dice_catalog->caps;
    }
    // Склады игрока (активные) поднимают капы еды/дерева/камня (SPEC §5).
    const auto *buildings = registry.try_get<const ecs::BuildingCatalog>(match);
    if (buildings == nullptr || caps.food <= 0) {
        return caps; // без базовых капов складам нечего поднимать
    }
    const int32_t player_id = registry.get<const ecs::PlayerInfo>(player).id;
    // Логистика (SPEC §8): активные склады игрока дают +к капам сверх базового.
    const auto *research_catalog = registry.try_get<const ecs::ResearchCatalog>(match);
    const auto *player_research = registry.try_get<const ecs::PlayerResearch>(player);
    int32_t logistics_bonus = 0;
    if (research_catalog != nullptr && player_research != nullptr &&
            player_research->has(ecs::kResearchLogistics)) {
        logistics_bonus = research_catalog->param(ecs::kResearchLogistics, "cap_bonus", 0);
    }
    for (auto [entity, building] : registry.view<const ecs::Building>().each()) {
        if (building.player_id != player_id ||
                building.status != static_cast<int32_t>(BuildingStatus::Active)) {
            continue;
        }
        const ecs::BuildingDef &def = buildings->defs[building.def_index];
        const int32_t bonus = def.cap_food > 0 || def.cap_wood > 0 || def.cap_stone > 0
                ? logistics_bonus
                : 0; // бонус — только к складам
        caps.food += def.cap_food + bonus;
        caps.wood += def.cap_wood + bonus;
        caps.stone += def.cap_stone + bonus;
    }
    return caps;
}

void apply_dice_income(entt::registry &registry) {
    const entt::entity match = registry.view<const ecs::MatchState>().front();
    const auto *catalog = registry.try_get<const ecs::DiceCatalog>(match);
    if (catalog == nullptr) {
        return;
    }

    auto view = registry.view<ecs::PlayerDice, ecs::Resources>();
    for (auto [entity, dice, resources] : view.each()) {
        for (const ecs::DieState &die : dice.dice) {
            if (die.face < 0) {
                continue;
            }
            const ecs::DieFace &face = catalog->defs[die.type_index].faces[die.face];
            resources.wood += face.gain.wood;
            resources.stone += face.gain.stone;
            resources.food += face.gain.food;
            resources.gold += face.gain.gold;
            resources.hammers += face.gain.hammers;
            resources.swords += face.gain.swords;
            resources.culture += face.gain.culture;
            // Мельница: +еда за выпавшую грань еды у смежной фермы (SPEC §5).
            if (face.gain.food > 0) {
                resources.food += die.food_bonus;
            }
            // Кресты не начисляются ресурсами: уходят в шкалу опасности
            // (фаза Катастроф). Пул очищается там же.
        }
        const ecs::Resources caps = effective_caps(registry, match, entity);
        resources.wood = clamp_to_cap(resources.wood, caps.wood);
        resources.stone = clamp_to_cap(resources.stone, caps.stone);
        resources.food = clamp_to_cap(resources.food, caps.food);
        resources.gold = clamp_to_cap(resources.gold, caps.gold);
        resources.hammers = clamp_to_cap(resources.hammers, caps.hammers);
        resources.swords = clamp_to_cap(resources.swords, caps.swords);
        resources.culture = clamp_to_cap(resources.culture, caps.culture);
    }
}

} // namespace dicecore::systems
