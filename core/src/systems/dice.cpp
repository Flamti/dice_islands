#include "systems/dice.hpp"

#include "ecs/components_building.hpp"
#include "math/rng.hpp"
#include "save/json.hpp"

#include <algorithm>
#include <cstdlib>
#include <set>

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

int32_t find_die_def(const ecs::DiceCatalog &catalog, const std::string &id) {
    for (size_t i = 0; i < catalog.defs.size(); ++i) {
        if (catalog.defs[i].id == id) {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

// Игроки в стабильном порядке ID — расход RNG детерминирован.
std::vector<entt::entity> players_sorted(entt::registry &registry) {
    std::vector<entt::entity> players;
    for (auto [entity, info] : registry.view<const ecs::PlayerInfo>().each()) {
        players.push_back(entity);
    }
    std::sort(players.begin(), players.end(), [&](entt::entity a, entt::entity b) {
        return registry.get<const ecs::PlayerInfo>(a).id < registry.get<const ecs::PlayerInfo>(b).id;
    });
    return players;
}

// Разбор "0,2,5" в множество индексов; false при мусоре в списке.
bool parse_indices(const std::string &text, std::set<int32_t> &out) {
    size_t pos = 0;
    while (pos < text.size()) {
        const size_t comma = text.find(',', pos);
        const std::string token = text.substr(pos, comma == std::string::npos ? comma : comma - pos);
        if (token.empty()) {
            return false;
        }
        char *end = nullptr;
        const long value = std::strtol(token.c_str(), &end, 10);
        if (end != token.c_str() + token.size() || value < 0) {
            return false;
        }
        out.insert(static_cast<int32_t>(value));
        if (comma == std::string::npos) {
            break;
        }
        pos = comma + 1;
    }
    return !out.empty();
}

} // namespace

bool parse_dice_json(const std::string &json_text, ecs::DiceCatalog &catalog, std::string &error) {
    save::JsonValue root;
    if (!save::parse_json(json_text, root, error)) {
        return false;
    }
    if (!root.is_object() || !root.has("dice")) {
        error = "ожидается объект с ключом dice";
        return false;
    }

    const save::JsonValue &caps = root.as_object().count("caps") ? root.as_object().at("caps")
                                                                 : save::JsonValue();
    catalog.caps.wood = caps.int_at("wood", 0);
    catalog.caps.stone = caps.int_at("stone", 0);
    catalog.caps.food = caps.int_at("food", 0);
    catalog.caps.gold = caps.int_at("gold", 0);
    catalog.caps.hammers = caps.int_at("hammers", 0);
    catalog.caps.swords = caps.int_at("swords", 0);
    catalog.caps.culture = caps.int_at("culture", 0);

    for (const auto &[id, faces_json] : root.as_object().at("dice").as_object()) {
        if (!faces_json.is_array() || faces_json.as_array().size() != 6) {
            error = "у кубика " + id + " должно быть ровно 6 граней";
            return false;
        }
        ecs::DiceDef def;
        def.id = id;
        for (size_t i = 0; i < 6; ++i) {
            const save::JsonValue &face = faces_json.as_array()[i];
            def.faces[i].gain.wood = face.int_at("wood", 0);
            def.faces[i].gain.stone = face.int_at("stone", 0);
            def.faces[i].gain.food = face.int_at("food", 0);
            def.faces[i].gain.gold = face.int_at("gold", 0);
            def.faces[i].gain.hammers = face.int_at("hammers", 0);
            def.faces[i].gain.swords = face.int_at("swords", 0);
            def.faces[i].gain.culture = face.int_at("culture", 0);
            def.faces[i].crosses = face.int_at("cross", 0);
        }
        catalog.defs.push_back(std::move(def));
    }
    if (catalog.defs.empty()) {
        error = "пустой набор кубиков";
        return false;
    }
    return true;
}

void collect_income(entt::registry &registry) {
    const auto *catalog = registry.try_get<const ecs::DiceCatalog>(match_entity(registry));
    const auto *buildings_catalog =
            registry.try_get<const ecs::BuildingCatalog>(match_entity(registry));
    if (catalog == nullptr || buildings_catalog == nullptr) {
        return; // партия без кубиков или без зданий
    }

    // Кубики зданий в порядке ID сущностей — стабильные индексы пула.
    std::vector<std::pair<uint32_t, std::pair<int32_t, int32_t>>> collected; // (bid, (player, die))
    for (auto [entity, building] : registry.view<const ecs::Building>().each()) {
        if (building.status != static_cast<int32_t>(BuildingStatus::Active)) {
            continue;
        }
        const std::string &die_id = buildings_catalog->defs[building.def_index].dice;
        if (die_id.empty()) {
            continue;
        }
        const int32_t die_index = find_die_def(*catalog, die_id);
        if (die_index < 0) {
            continue;
        }
        collected.push_back({static_cast<uint32_t>(entity), {building.player_id, die_index}});
    }
    std::sort(collected.begin(), collected.end());

    for (auto [entity, dice] : registry.view<ecs::PlayerDice>().each()) {
        dice.dice.clear();
        dice.rerolls_left = kMaxRerolls;
    }
    for (const auto &[building_id, entry] : collected) {
        const entt::entity player = find_player(registry, entry.first);
        if (player == entt::null) {
            continue;
        }
        auto &dice = registry.get_or_emplace<ecs::PlayerDice>(player);
        ecs::DieState die;
        die.type_index = entry.second;
        dice.dice.push_back(die);
        dice.rerolls_left = kMaxRerolls;
    }
}

void roll_initial(entt::registry &registry) {
    auto *match_rng = registry.try_get<ecs::MatchRng>(match_entity(registry));
    if (match_rng == nullptr) {
        return;
    }
    math::Rng rng(match_rng->state);
    for (const entt::entity player : players_sorted(registry)) {
        auto *dice = registry.try_get<ecs::PlayerDice>(player);
        if (dice == nullptr) {
            continue;
        }
        for (ecs::DieState &die : dice->dice) {
            die.face = rng.range_int(0, 5);
        }
    }
    match_rng->state = rng.raw_state();
}

IntentResult handle_reroll(entt::registry &registry, int32_t player_id, const Intent &intent) {
    IntentResult result;
    result.type = intent.type;

    const auto &state = registry.get<const ecs::MatchState>(match_entity(registry));
    if (state.phase != static_cast<int32_t>(Phase::Rolls)) {
        result.reason = kRejectWrongPhase;
        return result;
    }
    const entt::entity player = find_player(registry, player_id);
    if (player == entt::null) {
        result.reason = kRejectUnknownPlayer;
        return result;
    }
    auto *dice = registry.try_get<ecs::PlayerDice>(player);
    auto *match_rng = registry.try_get<ecs::MatchRng>(match_entity(registry));
    const auto *catalog = registry.try_get<const ecs::DiceCatalog>(match_entity(registry));
    if (dice == nullptr || match_rng == nullptr || catalog == nullptr) {
        result.reason = kRejectBadDiceSelection;
        return result;
    }
    if (dice->rerolls_left <= 0) {
        result.reason = kRejectNoRerollsLeft;
        return result;
    }

    const auto selection_it = intent.payload.find(kPayloadDice);
    std::set<int32_t> selection;
    if (selection_it == intent.payload.end() || !parse_indices(selection_it->second, selection)) {
        result.reason = kRejectBadDiceSelection;
        return result;
    }
    for (const int32_t index : selection) {
        if (index >= static_cast<int32_t>(dice->dice.size())) {
            result.reason = kRejectBadDiceSelection;
            return result;
        }
        const ecs::DieState &die = dice->dice[index];
        if (die.face < 0) {
            result.reason = kRejectBadDiceSelection;
            return result;
        }
        // Блокиратор: грань с крестом фиксируется целиком (SPEC §6).
        if (catalog->defs[die.type_index].faces[die.face].crosses > 0) {
            result.reason = kRejectCrossLocked;
            return result;
        }
    }

    math::Rng rng(match_rng->state);
    for (const int32_t index : selection) {
        dice->dice[index].face = rng.range_int(0, 5);
    }
    match_rng->state = rng.raw_state();
    --dice->rerolls_left;

    result.accepted = true;
    result.payload[kPayloadDice] = selection_it->second;
    result.payload["rerolls_left"] = std::to_string(dice->rerolls_left);
    return result;
}

} // namespace dicecore::systems
