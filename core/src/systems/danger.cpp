#include "systems/danger.hpp"

#include "ecs/components.hpp"
#include "math/rng.hpp"
#include "save/json.hpp"

#include <algorithm>
#include <string>

namespace dicecore::systems {

namespace {

entt::entity match_entity(const entt::registry &registry) {
    return registry.view<const ecs::MatchState>().front();
}

// Ссылка на поле ресурса по строковому ключу (для эффекта кражи).
int32_t *resource_field(ecs::Resources &resources, const std::string &key) {
    if (key == "wood") return &resources.wood;
    if (key == "stone") return &resources.stone;
    if (key == "food") return &resources.food;
    if (key == "gold") return &resources.gold;
    if (key == "hammers") return &resources.hammers;
    if (key == "swords") return &resources.swords;
    if (key == "culture") return &resources.culture;
    return nullptr;
}

// Кресты пула кубиков игрока -> в шкалу, затем очистка пула.
void accumulate_and_clear(entt::registry &registry) {
    const auto *catalog = registry.try_get<const ecs::DiceCatalog>(match_entity(registry));
    for (auto [entity, dice, meter] : registry.view<ecs::PlayerDice, ecs::DangerMeter>().each()) {
        if (catalog != nullptr) {
            for (const ecs::DieState &die : dice.dice) {
                if (die.face >= 0) {
                    meter.value += catalog->defs[die.type_index].faces[die.face].crosses;
                }
            }
        }
        dice.dice.clear();
        dice.rerolls_left = 0;
    }
}

// Игроки в стабильном порядке ID — расход RNG детерминирован.
std::vector<std::pair<int32_t, entt::entity>> players_sorted(const entt::registry &registry) {
    std::vector<std::pair<int32_t, entt::entity>> players;
    for (auto [entity, info] : registry.view<const ecs::PlayerInfo>().each()) {
        players.push_back({info.id, entity});
    }
    std::sort(players.begin(), players.end());
    return players;
}

// Индексы катастроф заданной тяжести (в порядке каталога).
std::vector<int32_t> disasters_of_tier(const ecs::DisasterCatalog &catalog, int32_t tier) {
    std::vector<int32_t> result;
    for (size_t i = 0; i < catalog.disasters.size(); ++i) {
        if (catalog.disasters[i].tier == tier) {
            result.push_back(static_cast<int32_t>(i));
        }
    }
    return result;
}

// Эффект «Воры» и прочие steal_resource: кража процента одного случайного
// ресурса (окр. вверх). Пишет детали в payload события.
void resolve_steal(const ecs::DisasterDef &def, ecs::Resources &resources, math::Rng &rng,
        std::map<std::string, std::string> &payload) {
    if (def.steal_from.empty()) {
        return;
    }
    const int32_t pick = rng.range_int(0, static_cast<int32_t>(def.steal_from.size()) - 1);
    const std::string &key = def.steal_from[pick];
    int32_t *field = resource_field(resources, key);
    if (field == nullptr) {
        return;
    }
    // Округление вверх: (value * percent + 99) / 100.
    const int32_t stolen = (*field * def.steal_percent + 99) / 100;
    *field -= stolen;
    payload["resource"] = key;
    payload["amount"] = std::to_string(stolen);
}

void resolve_disaster(const ecs::DisasterDef &def, int32_t player_id, entt::entity player_entity,
        entt::registry &registry, math::Rng &rng, std::vector<Event> &events) {
    Event event;
    event.type = kEventDisaster;
    event.payload["player"] = std::to_string(player_id);
    event.payload["disaster"] = def.id;
    event.payload["tier"] = std::to_string(def.tier);
    event.payload["target"] = def.target;

    // Этап 7: только Self-эффект кражи. Opponent-таргетинг — этап 12.
    if (def.effect == ecs::kEffectStealResource && def.target == ecs::kTargetSelf) {
        auto &resources = registry.get<ecs::Resources>(player_entity);
        resolve_steal(def, resources, rng, event.payload);
    }
    events.push_back(std::move(event));
}

} // namespace

bool parse_disasters_json(const std::string &json_text, ecs::DisasterCatalog &catalog,
        std::string &error) {
    save::JsonValue root;
    if (!save::parse_json(json_text, root, error)) {
        return false;
    }
    if (!root.is_object() || !root.has("thresholds") || !root.has("disasters")) {
        error = "ожидается объект с ключами thresholds и disasters";
        return false;
    }

    catalog.danger_max = root.int_at("danger_max", 12);
    for (const save::JsonValue &threshold : root.as_object().at("thresholds").as_array()) {
        ecs::DangerThreshold entry;
        entry.value = threshold.int_at("value", 0);
        entry.tier = threshold.int_at("tier", 0);
        catalog.thresholds.push_back(entry);
    }
    if (catalog.thresholds.empty()) {
        error = "пустой список порогов";
        return false;
    }
    std::sort(catalog.thresholds.begin(), catalog.thresholds.end(),
            [](const ecs::DangerThreshold &a, const ecs::DangerThreshold &b) {
                return a.value < b.value;
            });

    for (const auto &[id, def_json] : root.as_object().at("disasters").as_object()) {
        ecs::DisasterDef def;
        def.id = id;
        def.name = def_json.as_object().count("name") ? def_json.as_object().at("name").as_string() : id;
        def.tier = def_json.int_at("tier", 0);
        def.target = def_json.as_object().count("target") ? def_json.as_object().at("target").as_string()
                                                          : ecs::kTargetSelf;
        def.effect = def_json.as_object().count("effect") ? def_json.as_object().at("effect").as_string()
                                                          : "";
        def.steal_percent = def_json.int_at("steal_percent", 0);
        if (def_json.has("steal_from")) {
            for (const save::JsonValue &res : def_json.as_object().at("steal_from").as_array()) {
                def.steal_from.push_back(res.as_string());
            }
        }
        if (def.tier <= 0) {
            error = "у катастрофы " + id + " не задана тяжесть";
            return false;
        }
        catalog.disasters.push_back(std::move(def));
    }
    if (catalog.disasters.empty()) {
        error = "пустой пул катастроф";
        return false;
    }
    return true;
}

void resolve_danger_phase(entt::registry &registry, std::vector<Event> &events) {
    accumulate_and_clear(registry);

    const entt::entity match = match_entity(registry);
    const auto *catalog = registry.try_get<const ecs::DisasterCatalog>(match);
    auto *match_rng = registry.try_get<ecs::MatchRng>(match);
    if (catalog == nullptr || match_rng == nullptr) {
        return; // партия без катастроф
    }

    math::Rng rng(match_rng->state);
    bool rng_used = false;
    for (const auto &[player_id, player_entity] : players_sorted(registry)) {
        auto *meter = registry.try_get<ecs::DangerMeter>(player_entity);
        if (meter == nullptr || meter->value < catalog->thresholds.front().value) {
            continue;
        }
        // Старший достигнутый порог, у которого есть катастрофы (SPEC §7.1).
        for (int32_t i = static_cast<int32_t>(catalog->thresholds.size()) - 1; i >= 0; --i) {
            if (meter->value < catalog->thresholds[i].value) {
                continue;
            }
            const std::vector<int32_t> candidates =
                    disasters_of_tier(*catalog, catalog->thresholds[i].tier);
            if (candidates.empty()) {
                continue; // нет катастроф этой тяжести — падаем на порог ниже
            }
            rng_used = true;
            const int32_t pick = rng.range_int(0, static_cast<int32_t>(candidates.size()) - 1);
            resolve_disaster(catalog->disasters[candidates[pick]], player_id, player_entity,
                    registry, rng, events);
            meter->value = 0; // шкала сбрасывается (SPEC §7.1)
            break;
        }
    }
    if (rng_used) {
        match_rng->state = rng.raw_state();
    }
}

} // namespace dicecore::systems
