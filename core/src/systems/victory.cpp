#include "systems/victory.hpp"

#include "ecs/components.hpp"
#include "ecs/components_building.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <string>

namespace dicecore::systems {

namespace {

// Культурная победа (SPEC §10, [баланс]): множитель и нижний порог.
constexpr int32_t kCulturalMultiplier = 2;
constexpr int32_t kCulturalMinScore = 20;

entt::entity match_entity(const entt::registry &registry) {
    return registry.view<const ecs::MatchState>().front();
}

// Замок игрока разрушен (или отсутствует) — игрок выбывает (SPEC §10).
bool castle_destroyed(entt::registry &registry, int32_t player_id) {
    const auto *catalog = registry.try_get<const ecs::BuildingCatalog>(match_entity(registry));
    if (catalog == nullptr) {
        return false;
    }
    for (auto [entity, building] : registry.view<const ecs::Building>().each()) {
        if (building.player_id == player_id && catalog->defs[building.def_index].preplaced) {
            return building.status == static_cast<int32_t>(BuildingStatus::Destroyed);
        }
    }
    return false; // замок не найден — считаем живым
}

// Есть ли у команды достроенное и выстоявшее 1 ход Чудо (SPEC §10).
bool team_holds_wonder(entt::registry &registry, int32_t team, int32_t turn) {
    const auto *catalog = registry.try_get<const ecs::BuildingCatalog>(match_entity(registry));
    if (catalog == nullptr) {
        return false;
    }
    // Соответствие player -> team.
    std::map<int32_t, int32_t> player_team;
    for (auto [entity, info] : registry.view<const ecs::PlayerInfo>().each()) {
        player_team[info.id] = info.team;
    }
    for (auto [entity, building] : registry.view<const ecs::Building>().each()) {
        const ecs::BuildingDef &def = catalog->defs[building.def_index];
        if (def.wonder_stages == 0 || player_team[building.player_id] != team) {
            continue;
        }
        if (building.status != static_cast<int32_t>(BuildingStatus::Destroyed) &&
                building.wonder_stage >= def.wonder_stages && building.wonder_complete_turn > 0 &&
                turn >= building.wonder_complete_turn + 1) {
            return true;
        }
    }
    return false;
}

} // namespace

void resolve_checks_phase(entt::registry &registry, std::vector<Event> &events) {
    auto &state = registry.get<ecs::MatchState>(match_entity(registry));
    if (state.finished) {
        return;
    }

    // --- Выбывание игроков: замок разрушен (SPEC §10) ---
    for (auto [entity, info] : registry.view<ecs::PlayerInfo>().each()) {
        if (info.alive && castle_destroyed(registry, info.id)) {
            info.alive = false;
            Event e;
            e.type = kEventElimination;
            e.payload["player"] = std::to_string(info.id);
            events.push_back(std::move(e));
        }
    }

    // --- Команды, живые команды, культура ---
    std::set<int32_t> teams;
    std::map<int32_t, int32_t> alive_by_team;
    std::map<int32_t, int64_t> culture_by_team;
    for (auto [entity, info, res] :
            registry.view<const ecs::PlayerInfo, const ecs::Resources>().each()) {
        teams.insert(info.team);
        if (info.alive) {
            alive_by_team[info.team] += 1;
            culture_by_team[info.team] += res.culture;
        }
    }

    const int32_t turn = state.turn;
    // Военная/культурная победа требуют наличия чужой команды (SPEC §10): с
    // единственной командой (соло/кооп) побеждают только Чудом.
    const bool has_opponents = teams.size() >= 2;
    // Кандидаты-победители по каждому пути (в порядке возрастания team id).
    int32_t wonder_winner = -1;
    int32_t military_winner = -1;
    int32_t cultural_winner = -1;

    for (int32_t team : teams) {
        if (alive_by_team[team] <= 0) {
            continue; // мёртвая команда не побеждает
        }
        // Чудо.
        if (wonder_winner < 0 && team_holds_wonder(registry, team, turn)) {
            wonder_winner = team;
        }
        // Военная: во всех прочих командах нет живых игроков.
        int32_t enemies_alive = 0;
        for (const auto &[t, count] : alive_by_team) {
            if (t != team) {
                enemies_alive += count;
            }
        }
        if (has_opponents && military_winner < 0 && enemies_alive == 0) {
            military_winner = team;
        }
        // Культурная: >= 2x любой другой команды И >= 20.
        int64_t best_other = 0;
        for (const auto &[t, c] : culture_by_team) {
            if (t != team) {
                best_other = std::max(best_other, c);
            }
        }
        if (has_opponents && cultural_winner < 0 && culture_by_team[team] >= kCulturalMinScore &&
                culture_by_team[team] >= static_cast<int64_t>(kCulturalMultiplier) * best_other) {
            cultural_winner = team;
        }
    }

    // Приоритет Чудо -> Военная -> Культурная (SPEC §10).
    int32_t winner = -1;
    std::string path;
    if (wonder_winner >= 0) {
        winner = wonder_winner;
        path = "wonder";
    } else if (military_winner >= 0) {
        winner = military_winner;
        path = "military";
    } else if (cultural_winner >= 0) {
        winner = cultural_winner;
        path = "cultural";
    }

    if (winner >= 0) {
        state.finished = true;
        state.winner_team = winner;
        Event e;
        e.type = kEventVictory;
        e.payload["team"] = std::to_string(winner);
        e.payload["path"] = path;
        events.push_back(std::move(e));
    }
}

} // namespace dicecore::systems
