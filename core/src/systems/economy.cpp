#include "systems/economy.hpp"

#include "ecs/components.hpp"

#include <algorithm>

namespace dicecore::systems {

namespace {

// Кап с сжиганием излишка; cap <= 0 — без лимита (SPEC §3).
int32_t clamp_to_cap(int32_t value, int32_t cap) {
    return cap > 0 ? std::min(value, cap) : value;
}

} // namespace

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
            // Кресты не начисляются ресурсами: они уходят в шкалу опасности
            // (фаза Катастроф, systems/danger). Пул очищается там же.
        }
        resources.wood = clamp_to_cap(resources.wood, catalog->caps.wood);
        resources.stone = clamp_to_cap(resources.stone, catalog->caps.stone);
        resources.food = clamp_to_cap(resources.food, catalog->caps.food);
        resources.gold = clamp_to_cap(resources.gold, catalog->caps.gold);
        resources.hammers = clamp_to_cap(resources.hammers, catalog->caps.hammers);
        resources.swords = clamp_to_cap(resources.swords, catalog->caps.swords);
        resources.culture = clamp_to_cap(resources.culture, catalog->caps.culture);
    }
}

} // namespace dicecore::systems
