#include "systems/raid.hpp"

#include "ecs/components.hpp"
#include "ecs/components_building.hpp"
#include "systems/combat/battle.hpp"

#include <cstdlib>
#include <string>

namespace dicecore::systems {

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

int32_t parse_int(const Intent &intent, const char *key, int32_t fallback) {
    const auto it = intent.payload.find(key);
    if (it == intent.payload.end() || it->second.empty()) {
        return fallback;
    }
    return static_cast<int32_t>(std::strtol(it->second.c_str(), nullptr, 10));
}

// Есть ли у игрока активный Порт (открывает рейды, SPEC §5, §9.4).
bool has_active_port(const entt::registry &registry, const ecs::BuildingCatalog &catalog,
        int32_t player_id) {
    for (auto [entity, building] : registry.view<const ecs::Building>().each()) {
        if (building.player_id == player_id &&
                building.status == static_cast<int32_t>(BuildingStatus::Active) &&
                catalog.defs[building.def_index].unlocks_raids) {
            return true;
        }
    }
    return false;
}

// Здание с указанным ID у игрока target (не разрушенное); entt::null, если нет.
entt::entity find_target_building(const entt::registry &registry, int32_t target_player,
        int32_t building_id) {
    for (auto [entity, building] : registry.view<const ecs::Building>().each()) {
        if (static_cast<int32_t>(entity) == building_id && building.player_id == target_player &&
                building.status != static_cast<int32_t>(BuildingStatus::Destroyed)) {
            return entity;
        }
    }
    return entt::null;
}

int32_t raids_by_owner(const ecs::PendingRaids &pending, int32_t owner) {
    int32_t count = 0;
    for (const ecs::PendingRaid &raid : pending.raids) {
        if (raid.attacker_owner == owner) {
            ++count;
        }
    }
    return count;
}

} // namespace

IntentResult handle_raid(entt::registry &registry, int32_t player_id, const Intent &intent) {
    IntentResult result;
    result.type = intent.type;

    const entt::entity match = match_entity(registry);
    const auto &state = registry.get<const ecs::MatchState>(match);
    if (state.phase != static_cast<int32_t>(Phase::Raids)) {
        result.reason = kRejectWrongPhase;
        return result;
    }
    const entt::entity attacker = find_player(registry, player_id);
    const auto *catalog = registry.try_get<const ecs::BuildingCatalog>(match);
    const auto *config = registry.try_get<const combat::CombatConfig>(match);
    if (attacker == entt::null || catalog == nullptr || config == nullptr) {
        result.reason = kRejectBadRaidTarget;
        return result;
    }

    if (!has_active_port(registry, *catalog, player_id)) {
        result.reason = kRejectNoPort;
        return result;
    }

    const int32_t target_player = parse_int(intent, kPayloadTargetPlayer, -1);
    const int32_t target_building = parse_int(intent, kPayloadTargetBuilding, -1);
    const int32_t count = parse_int(intent, kPayloadCount, 0);
    const int32_t side = parse_int(intent, kPayloadSide, 0);

    const entt::entity target_entity = find_player(registry, target_player);
    if (target_entity == entt::null || target_player == player_id) {
        result.reason = kRejectBadRaidTarget;
        return result;
    }
    const auto &attacker_info = registry.get<const ecs::PlayerInfo>(attacker);
    const auto &target_info = registry.get<const ecs::PlayerInfo>(target_entity);
    if (!target_info.alive) {
        result.reason = kRejectBadRaidTarget;
        return result;
    }
    // Союзники (одна команда) не атакуют друг друга (SPEC §9.4).
    if (attacker_info.team == target_info.team) {
        result.reason = kRejectAllyTarget;
        return result;
    }
    if (find_target_building(registry, target_player, target_building) == entt::null) {
        result.reason = kRejectBadRaidTarget;
        return result;
    }
    if (count < 1 || count > config->raid_capacity) {
        result.reason = kRejectBadRaidCount;
        return result;
    }
    auto &attacker_res = registry.get<ecs::Resources>(attacker);
    if (attacker_res.swords < count) {
        result.reason = kRejectNotEnoughGarrison;
        return result;
    }
    auto &pending = registry.get_or_emplace<ecs::PendingRaids>(match);
    if (raids_by_owner(pending, player_id) >= config->raids_per_turn) {
        result.reason = kRejectRaidLimit;
        return result;
    }

    // Списываем бойцов из гарнизона; выжившие вернутся в фазе Боя.
    attacker_res.swords -= count;
    ecs::PendingRaid raid;
    raid.defender_player = target_player;
    raid.attacker_team = attacker_info.team;
    raid.attacker_owner = player_id;
    raid.count = count;
    raid.landing_side = side & 3; // 0..3
    raid.target_building = target_building;
    pending.raids.push_back(raid);

    result.accepted = true;
    result.payload[kPayloadTargetPlayer] = std::to_string(target_player);
    result.payload[kPayloadTargetBuilding] = std::to_string(target_building);
    result.payload[kPayloadCount] = std::to_string(count);
    return result;
}

} // namespace dicecore::systems
