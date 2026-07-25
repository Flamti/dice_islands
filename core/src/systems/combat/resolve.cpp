#include "systems/combat/resolve.hpp"

#include "ecs/components.hpp"
#include "ecs/components_building.hpp"
#include "gen/island_gen.hpp"
#include "math/rng.hpp"
#include "systems/combat/battle.hpp"

#include <algorithm>
#include <map>
#include <string>

namespace dicecore::systems::combat {

namespace {

entt::entity match_entity(const entt::registry &registry) {
    return registry.view<const ecs::MatchState>().front();
}

entt::entity find_player(const entt::registry &registry, int32_t player_id) {
    for (auto [entity, info] : registry.view<const ecs::PlayerInfo>().each()) {
        if (info.id == player_id) {
            return entity;
        }
    }
    return entt::null;
}

// Награда за разрушение цели: 30% запасов защитника (SPEC §9.3) нападавшему.
void grant_reward(ecs::Resources &attacker, ecs::Resources &defender, int32_t percent) {
    const int32_t wood = defender.wood * percent / 100;
    const int32_t stone = defender.stone * percent / 100;
    const int32_t food = defender.food * percent / 100;
    const int32_t gold = defender.gold * percent / 100;
    defender.wood -= wood;
    defender.stone -= stone;
    defender.food -= food;
    defender.gold -= gold;
    attacker.wood += wood;
    attacker.stone += stone;
    attacker.food += food;
    attacker.gold += gold;
}

BattleLog to_public_log(int32_t defender, const BattleResult &result) {
    BattleLog log;
    log.defender_player = defender;
    log.defender_survivors = result.defender_survivors;
    log.destroyed_buildings = result.destroyed_buildings;
    for (const BattleFrame &frame : result.frames) {
        BattleLogFrame out_frame;
        out_frame.tick = frame.tick;
        for (const BattleUnitFrame &u : frame.units) {
            out_frame.units.push_back({u.id, u.team, u.attacker, u.x, u.z, u.hp});
        }
        log.frames.push_back(std::move(out_frame));
    }
    return log;
}

} // namespace

void resolve_combat_phase(entt::registry &registry, std::vector<Event> &events,
        std::vector<BattleLog> &logs) {
    const entt::entity match = match_entity(registry);
    auto *pending = registry.try_get<ecs::PendingRaids>(match);
    const auto *config = registry.try_get<const CombatConfig>(match);
    const auto *catalog = registry.try_get<const ecs::BuildingCatalog>(match);
    auto *match_rng = registry.try_get<ecs::MatchRng>(match);
    if (pending == nullptr || config == nullptr || catalog == nullptr || match_rng == nullptr ||
            pending->raids.empty()) {
        if (pending != nullptr) {
            pending->raids.clear();
        }
        return;
    }

    // Рейды по острову-защитнику, в детерминированном порядке ID.
    std::map<int32_t, std::vector<ecs::PendingRaid>> by_defender;
    for (const ecs::PendingRaid &raid : pending->raids) {
        by_defender[raid.defender_player].push_back(raid);
    }

    math::Rng rng(match_rng->state);
    for (const auto &[defender_id, raids] : by_defender) {
        const entt::entity defender_entity = find_player(registry, defender_id);
        if (defender_entity == entt::null) {
            continue;
        }
        const auto *grid = registry.try_get<const ecs::PlayerGrid>(defender_entity);
        if (grid == nullptr) {
            continue;
        }
        const auto &defender_info = registry.get<const ecs::PlayerInfo>(defender_entity);
        auto &defender_res = registry.get<ecs::Resources>(defender_entity);

        BattleSetup setup;
        setup.config = *config;
        setup.cells_x = grid->cells_x;
        setup.cells_z = grid->cells_z;
        setup.walkable.assign(grid->types.size(), 0);
        for (size_t i = 0; i < grid->types.size(); ++i) {
            setup.walkable[i] = grid->types[i] != static_cast<int32_t>(gen::CellType::Void) ? 1 : 0;
        }
        setup.defender_player = defender_id;
        setup.defender_team = defender_info.team;
        setup.defender_garrison = defender_res.swords;

        // Живые (не Destroyed) здания защитника; карта entity id -> индекс.
        std::map<int32_t, int32_t> id_to_index;
        for (auto [entity, building] : registry.view<const ecs::Building>().each()) {
            if (building.player_id != defender_id ||
                    building.status == static_cast<int32_t>(BuildingStatus::Destroyed)) {
                continue;
            }
            const ecs::BuildingDef &def = catalog->defs[building.def_index];
            BattleBuilding bb;
            bb.id = static_cast<int32_t>(entity);
            bb.cell_x = building.cell_x;
            bb.cell_z = building.cell_z;
            bb.size_x = building.size_x;
            bb.size_z = building.size_z;
            bb.hp = building.hp;
            bb.is_wall = def.is_wall;
            bb.is_tower = def.is_tower;
            bb.spawns_garrison = def.spawns_garrison;
            id_to_index[bb.id] = static_cast<int32_t>(setup.buildings.size());
            setup.buildings.push_back(bb);
        }

        for (const ecs::PendingRaid &raid : raids) {
            AttackerGroup group;
            group.team = raid.attacker_team;
            group.owner_player = raid.attacker_owner;
            group.count = raid.count;
            group.landing_side = raid.landing_side;
            const auto it = id_to_index.find(raid.target_building);
            group.target_building = it != id_to_index.end() ? it->second : -1;
            setup.attackers.push_back(group);
        }

        const uint64_t battle_seed = rng.next_u64();
        const BattleResult result = simulate_battle(setup, battle_seed);

        // --- Применение исхода ---
        for (int32_t destroyed_id : result.destroyed_buildings) {
            for (auto [entity, building] : registry.view<ecs::Building>().each()) {
                if (static_cast<int32_t>(entity) == destroyed_id) {
                    building.status = static_cast<int32_t>(BuildingStatus::Destroyed);
                }
            }
        }
        defender_res.swords = result.defender_survivors;
        for (const GroupOutcome &group : result.groups) {
            if (group.owner_player < 0) {
                continue; // пираты — без возврата и награды
            }
            // Рейды игроков (этап 10): выжившие возвращаются в гарнизон.
            const entt::entity owner = find_player(registry, group.owner_player);
            if (owner != entt::null) {
                registry.get<ecs::Resources>(owner).swords += group.survivors;
                if (group.target_destroyed) {
                    grant_reward(registry.get<ecs::Resources>(owner), defender_res,
                            config->reward_percent);
                }
            }
        }

        Event event;
        event.type = kEventBattle;
        event.payload["defender"] = std::to_string(defender_id);
        event.payload["destroyed"] = std::to_string(result.destroyed_buildings.size());
        event.payload["defenders_left"] = std::to_string(result.defender_survivors);
        events.push_back(std::move(event));

        logs.push_back(to_public_log(defender_id, result));
    }

    match_rng->state = rng.raw_state();
    pending->raids.clear();
}

} // namespace dicecore::systems::combat
