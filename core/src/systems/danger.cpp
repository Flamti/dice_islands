#include "systems/danger.hpp"

#include "ecs/components.hpp"
#include "ecs/components_building.hpp"
#include "ecs/components_research.hpp"
#include "gen/destruction.hpp"
#include "gen/island_gen.hpp"
#include "math/rng.hpp"
#include "save/json.hpp"
#include "systems/adjacency.hpp"
#include "systems/research.hpp"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

namespace dicecore::systems {

namespace {

// Обряд бури: перезарядка (SPEC §8, [баланс]).
constexpr int32_t kStormRiteCooldownTurns = 5;
constexpr int32_t kStormRiteTier = 2;
// Саботаж: сколько ходов здание Disabled (пропускает следующий доход).
constexpr int32_t kSabotageTurns = 2;

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

int32_t current_turn(const entt::registry &registry) {
    return registry.get<const ecs::MatchState>(match_entity(registry)).turn;
}

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

std::vector<std::pair<int32_t, entt::entity>> players_sorted(const entt::registry &registry) {
    std::vector<std::pair<int32_t, entt::entity>> players;
    for (auto [entity, info] : registry.view<const ecs::PlayerInfo>().each()) {
        players.push_back({info.id, entity});
    }
    std::sort(players.begin(), players.end());
    return players;
}

std::vector<int32_t> disasters_of_tier(const ecs::DisasterCatalog &catalog, int32_t tier) {
    std::vector<int32_t> result;
    for (size_t i = 0; i < catalog.disasters.size(); ++i) {
        if (catalog.disasters[i].tier == tier) {
            result.push_back(static_cast<int32_t>(i));
        }
    }
    return result;
}

std::vector<int32_t> opponents_of(const entt::registry &registry, int32_t player_id, int32_t team) {
    std::vector<int32_t> opponents;
    for (auto [entity, info] : registry.view<const ecs::PlayerInfo>().each()) {
        if (info.id != player_id && info.team != team && info.alive) {
            opponents.push_back(info.id);
        }
    }
    std::sort(opponents.begin(), opponents.end());
    return opponents;
}

// Стабильный порядок зданий по клетке (не по ID сущности) — воспроизводится
// после загрузки сейва (SPEC §13), поэтому выбор цели RNG детерминирован.
bool building_before(const entt::registry &registry, entt::entity a, entt::entity b) {
    const auto &ba = registry.get<const ecs::Building>(a);
    const auto &bb = registry.get<const ecs::Building>(b);
    if (ba.player_id != bb.player_id) return ba.player_id < bb.player_id;
    if (ba.cell_z != bb.cell_z) return ba.cell_z < bb.cell_z;
    return ba.cell_x < bb.cell_x;
}

// Здания игрока (не разрушенные), в стабильном порядке (клетка).
std::vector<entt::entity> buildings_of(entt::registry &registry, int32_t player_id,
        bool active_only) {
    std::vector<entt::entity> result;
    for (auto [entity, building] : registry.view<const ecs::Building>().each()) {
        if (building.player_id != player_id) {
            continue;
        }
        if (building.status == static_cast<int32_t>(BuildingStatus::Destroyed)) {
            continue;
        }
        if (active_only && building.status != static_cast<int32_t>(BuildingStatus::Active)) {
            continue;
        }
        result.push_back(entity);
    }
    std::sort(result.begin(), result.end(),
            [&registry](entt::entity a, entt::entity b) { return building_before(registry, a, b); });
    return result;
}

// Свободить клетки здания в сетке (руины остаются заняты; при вырезании — нет).
void free_cells(ecs::PlayerGrid &grid, const ecs::Building &b) {
    for (int32_t dz = 0; dz < b.size_z; ++dz) {
        for (int32_t dx = 0; dx < b.size_x; ++dx) {
            if (grid.in_bounds(b.cell_x + dx, b.cell_z + dz)) {
                grid.occupancy[grid.index_of(b.cell_x + dx, b.cell_z + dz)] = ecs::kCellFree;
            }
        }
    }
}

// Урон зданию; при hp<=0 — Destroyed (руины). Возвращает true при разрушении.
bool damage_building(entt::registry &registry, entt::entity building_entity, int32_t amount) {
    auto &b = registry.get<ecs::Building>(building_entity);
    if (b.status == static_cast<int32_t>(BuildingStatus::Destroyed)) {
        return false;
    }
    b.hp -= amount;
    if (b.hp <= 0) {
        b.status = static_cast<int32_t>(BuildingStatus::Destroyed);
        return true;
    }
    return false;
}

// --- Эффекты катастроф (SPEC §7.2) ---

void apply_steal(const ecs::DisasterDef &def, ecs::Resources &resources, math::Rng &rng,
        std::map<std::string, std::string> &payload) {
    if (def.steal_from.empty()) {
        return;
    }
    const int32_t percent = def.param("steal_percent", 25);
    const int32_t pick = rng.range_int(0, static_cast<int32_t>(def.steal_from.size()) - 1);
    const std::string &key = def.steal_from[pick];
    int32_t *field = resource_field(resources, key);
    if (field == nullptr) {
        return;
    }
    const int32_t stolen = (*field * percent + 99) / 100; // округление вверх
    *field -= stolen;
    payload["resource"] = key;
    payload["amount"] = std::to_string(stolen);
}

void apply_damage_random(const ecs::DisasterDef &def, entt::registry &registry, int32_t victim,
        math::Rng &rng, std::map<std::string, std::string> &payload) {
    const std::vector<entt::entity> targets = buildings_of(registry, victim, false);
    if (targets.empty()) {
        return;
    }
    const entt::entity target = targets[rng.range_int(0, static_cast<int32_t>(targets.size()) - 1)];
    const bool destroyed = damage_building(registry, target, def.param("damage", 8));
    payload["building"] = std::to_string(static_cast<int32_t>(target));
    payload["destroyed"] = destroyed ? "1" : "0";
}

void apply_disease(entt::registry &registry, int32_t victim, math::Rng &rng,
        std::map<std::string, std::string> &payload) {
    const std::vector<entt::entity> targets = buildings_of(registry, victim, true);
    if (targets.empty()) {
        return;
    }
    const entt::entity target = targets[rng.range_int(0, static_cast<int32_t>(targets.size()) - 1)];
    registry.get<ecs::Building>(target).status = static_cast<int32_t>(BuildingStatus::Diseased);
    payload["building"] = std::to_string(static_cast<int32_t>(target));
}

void apply_disable(entt::registry &registry, int32_t victim, math::Rng &rng,
        std::map<std::string, std::string> &payload) {
    const std::vector<entt::entity> targets = buildings_of(registry, victim, true);
    if (targets.empty()) {
        return;
    }
    const entt::entity target = targets[rng.range_int(0, static_cast<int32_t>(targets.size()) - 1)];
    auto &b = registry.get<ecs::Building>(target);
    b.status = static_cast<int32_t>(BuildingStatus::Disabled);
    b.restore_turn = current_turn(registry) + kSabotageTurns;
    payload["building"] = std::to_string(static_cast<int32_t>(target));
}

// Здание стоит на клетках-лесах (Scaffold)?
bool on_scaffold(const ecs::PlayerGrid &grid, const ecs::Building &b) {
    for (int32_t dz = 0; dz < b.size_z; ++dz) {
        for (int32_t dx = 0; dx < b.size_x; ++dx) {
            if (grid.in_bounds(b.cell_x + dx, b.cell_z + dz) &&
                    grid.types[grid.index_of(b.cell_x + dx, b.cell_z + dz)] ==
                            static_cast<int32_t>(gen::CellType::Scaffold)) {
                return true;
            }
        }
    }
    return false;
}

void apply_storm(const ecs::DisasterDef &def, entt::registry &registry, int32_t victim,
        math::Rng &rng, std::map<std::string, std::string> &payload) {
    auto *grid = registry.try_get<ecs::PlayerGrid>(player_by_id(registry, victim));
    if (grid == nullptr) {
        return;
    }
    const int32_t scaffold_dmg = def.param("scaffold_damage", 6);
    const int32_t land_dmg = def.param("land_damage", 4);
    const int32_t land_targets = def.param("land_targets", 3);
    const int32_t destroy_pct = def.param("scaffold_destroy_percent", 50);

    // Урон зданиям на лесах; сбор наземных зданий для случайного добора.
    std::vector<entt::entity> land;
    for (const entt::entity e : buildings_of(registry, victim, false)) {
        if (on_scaffold(*grid, registry.get<const ecs::Building>(e))) {
            damage_building(registry, e, scaffold_dmg);
        } else {
            land.push_back(e);
        }
    }
    // 50% разрушить каждую пустую клетку-леса.
    int32_t destroyed_scaffolds = 0;
    for (size_t i = 0; i < grid->types.size(); ++i) {
        if (grid->types[i] == static_cast<int32_t>(gen::CellType::Scaffold) &&
                grid->occupancy[i] == ecs::kCellFree) {
            if (rng.range_int(1, 100) <= destroy_pct) {
                grid->types[i] = static_cast<int32_t>(gen::CellType::Void);
                ++destroyed_scaffolds;
            }
        }
    }
    // 4 урона случайным 3 наземным зданиям.
    std::sort(land.begin(), land.end(),
            [&registry](entt::entity a, entt::entity b) { return building_before(registry, a, b); });
    for (int32_t n = 0; n < land_targets && !land.empty(); ++n) {
        const int32_t idx = rng.range_int(0, static_cast<int32_t>(land.size()) - 1);
        damage_building(registry, land[idx], land_dmg);
        land.erase(land.begin() + idx);
    }
    payload["scaffolds_destroyed"] = std::to_string(destroyed_scaffolds);
}

void apply_meteor(const ecs::DisasterDef &def, entt::registry &registry, int32_t victim,
        math::Rng &rng, std::map<std::string, std::string> &payload) {
    auto *grid = registry.try_get<ecs::PlayerGrid>(player_by_id(registry, victim));
    if (grid == nullptr) {
        return;
    }
    const int32_t radius = def.param("radius", 1);
    const int32_t damage = def.param("damage", 20);
    const int32_t cx = rng.range_int(0, grid->cells_x - 1);
    const int32_t cz = rng.range_int(0, grid->cells_z - 1);
    payload["cell_x"] = std::to_string(cx);
    payload["cell_z"] = std::to_string(cz);

    // Урон всем зданиям, пересекающим область (2*radius+1)^2 вокруг точки.
    int32_t hit = 0;
    for (const entt::entity e : buildings_of(registry, victim, false)) {
        const auto &b = registry.get<const ecs::Building>(e);
        const bool overlaps = b.cell_x <= cx + radius && b.cell_x + b.size_x - 1 >= cx - radius &&
                b.cell_z <= cz + radius && b.cell_z + b.size_z - 1 >= cz - radius;
        if (overlaps) {
            damage_building(registry, e, damage);
            ++hit;
        }
    }
    payload["hit"] = std::to_string(hit);
}

void apply_edge_collapse(const ecs::DisasterDef &def, entt::registry &registry, int32_t victim,
        math::Rng &rng, std::map<std::string, std::string> &payload, std::vector<Event> &events) {
    auto *grid = registry.try_get<ecs::PlayerGrid>(player_by_id(registry, victim));
    if (grid == nullptr) {
        return;
    }
    // Клетки замка защищены (SPEC §7.2): обрушение выбирается вне замка.
    std::vector<gen::GridCell> protect;
    for (const entt::entity e : buildings_of(registry, victim, false)) {
        const auto &b = registry.get<const ecs::Building>(e);
        // Замок — предразмещённое здание (по каталогу).
        const auto *bcatalog = registry.try_get<const ecs::BuildingCatalog>(match_entity(registry));
        if (bcatalog != nullptr && bcatalog->defs[b.def_index].preplaced) {
            for (int32_t dz = 0; dz < b.size_z; ++dz) {
                for (int32_t dx = 0; dx < b.size_x; ++dx) {
                    protect.push_back({b.cell_x + dx, b.cell_z + dz});
                }
            }
        }
    }

    // Сетка-снимок для выбора края (gen::GridData поверх PlayerGrid).
    gen::GridData snapshot;
    snapshot.cells_x = grid->cells_x;
    snapshot.cells_z = grid->cells_z;
    snapshot.types = grid->types;
    const std::vector<gen::GridCell> region = gen::pick_edge_region(
            snapshot, protect, def.param("min_cells", 6), def.param("max_cells", 10), rng);
    if (region.empty()) {
        return;
    }

    // Вырезаем клетки, уничтожаем здания на них (без руин, SPEC §11.5).
    auto &carved = registry.get_or_emplace<ecs::PlayerCarved>(player_by_id(registry, victim));
    for (const gen::GridCell &c : region) {
        grid->types[grid->index_of(c.cell_x, c.cell_z)] = static_cast<int32_t>(gen::CellType::Void);
        carved.cells.push_back({c.cell_x, c.cell_z});
    }
    carved.dirty = true;
    int32_t destroyed = 0;
    for (const entt::entity e : buildings_of(registry, victim, false)) {
        auto &b = registry.get<ecs::Building>(e);
        bool on_removed = false;
        for (const gen::GridCell &c : region) {
            if (c.cell_x >= b.cell_x && c.cell_x < b.cell_x + b.size_x && c.cell_z >= b.cell_z &&
                    c.cell_z < b.cell_z + b.size_z) {
                on_removed = true;
            }
        }
        if (on_removed) {
            free_cells(*grid, b);
            b.status = static_cast<int32_t>(BuildingStatus::Destroyed);
            b.hp = 0;
            ++destroyed;
        }
    }
    payload["removed_cells"] = std::to_string(region.size());
    payload["destroyed"] = std::to_string(destroyed);

    Event landscape;
    landscape.type = kEventLandscape;
    landscape.payload["player"] = std::to_string(victim);
    landscape.payload["cells"] = std::to_string(region.size());
    events.push_back(std::move(landscape));
}

void queue_pirate_raid(const ecs::DisasterDef &def, entt::registry &registry, entt::entity match,
        int32_t victim, math::Rng &rng, std::map<std::string, std::string> &payload) {
    auto &pending = registry.get_or_emplace<ecs::PendingRaids>(match);
    ecs::PendingRaid raid;
    raid.defender_player = victim;
    raid.attacker_team = -1;
    raid.attacker_owner = -1;
    raid.count = def.param("pirate_count", 6);
    raid.landing_side = rng.range_int(0, 3);
    raid.target_building = -1;
    pending.raids.push_back(raid);
    payload["victim"] = std::to_string(victim);
}

// Применение эффекта к жертве (victim — id игрока-цели; для Self = сам).
void apply_effect(const ecs::DisasterDef &def, entt::registry &registry, entt::entity match,
        int32_t caster, int32_t victim, math::Rng &rng, std::vector<Event> &events) {
    Event event;
    event.type = kEventDisaster;
    event.payload["player"] = std::to_string(caster);
    event.payload["victim"] = std::to_string(victim);
    event.payload["disaster"] = def.id;
    event.payload["tier"] = std::to_string(def.tier);
    event.payload["target"] = def.target;

    if (def.effect == ecs::kEffectStealResource) {
        apply_steal(def, registry.get<ecs::Resources>(player_by_id(registry, victim)), rng,
                event.payload);
    } else if (def.effect == ecs::kEffectDamageRandomBuilding) {
        apply_damage_random(def, registry, victim, rng, event.payload);
    } else if (def.effect == ecs::kEffectDisease) {
        apply_disease(registry, victim, rng, event.payload);
    } else if (def.effect == ecs::kEffectDisableRandomBuilding) {
        apply_disable(registry, victim, rng, event.payload);
    } else if (def.effect == ecs::kEffectStorm) {
        apply_storm(def, registry, victim, rng, event.payload);
    } else if (def.effect == ecs::kEffectMeteor) {
        apply_meteor(def, registry, victim, rng, event.payload);
    } else if (def.effect == ecs::kEffectEdgeCollapse) {
        apply_edge_collapse(def, registry, victim, rng, event.payload, events);
    } else if (def.effect == ecs::kEffectPirateRaid) {
        queue_pirate_raid(def, registry, match, victim, rng, event.payload);
    }
    events.push_back(std::move(event));
}

// Распространение болезни: каждый Diseased с шансом заражает смежного Active.
void spread_disease(entt::registry &registry, const ecs::DisasterCatalog &catalog, math::Rng &rng) {
    const ecs::DisasterDef *disease = nullptr;
    for (const ecs::DisasterDef &d : catalog.disasters) {
        if (d.effect == ecs::kEffectDisease) {
            disease = &d;
        }
    }
    if (disease == nullptr) {
        return;
    }
    const int32_t percent = disease->param("spread_percent", 35);
    // Снимок Diseased-зданий (заражённые в этот ход не распространяют сразу).
    std::vector<entt::entity> sources;
    for (auto [entity, b] : registry.view<const ecs::Building>().each()) {
        if (b.status == static_cast<int32_t>(BuildingStatus::Diseased)) {
            sources.push_back(entity);
        }
    }
    std::sort(sources.begin(), sources.end(),
            [&registry](entt::entity a, entt::entity b) { return building_before(registry, a, b); });
    for (const entt::entity src : sources) {
        if (rng.range_int(1, 100) > percent) {
            continue;
        }
        // Один смежный Active-сосед заражается (SPEC §7.2).
        std::vector<entt::entity> neighbors;
        for (const entt::entity nb : adjacent_buildings(registry, src)) {
            if (registry.get<const ecs::Building>(nb).status ==
                    static_cast<int32_t>(BuildingStatus::Active)) {
                neighbors.push_back(nb);
            }
        }
        if (neighbors.empty()) {
            continue;
        }
        std::sort(neighbors.begin(), neighbors.end(),
                [&registry](entt::entity a, entt::entity b) { return building_before(registry, a, b); });
        const entt::entity victim =
                neighbors[rng.range_int(0, static_cast<int32_t>(neighbors.size()) - 1)];
        registry.get<ecs::Building>(victim).status = static_cast<int32_t>(BuildingStatus::Diseased);
    }
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
        if (def_json.has("steal_from")) {
            for (const save::JsonValue &res : def_json.as_object().at("steal_from").as_array()) {
                def.steal_from.push_back(res.as_string());
            }
        }
        // Все прочие числовые поля -> params.
        for (const auto &[key, value] : def_json.as_object()) {
            if (value.kind() == save::JsonValue::Kind::Number) {
                def.params[key] = static_cast<int32_t>(value.as_number(0.0));
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
        return;
    }

    math::Rng rng(match_rng->state);
    // Болезнь распространяется каждый ход (SPEC §7.2).
    spread_disease(registry, *catalog, rng);

    for (const auto &[player_id, player_entity] : players_sorted(registry)) {
        auto *meter = registry.try_get<ecs::DangerMeter>(player_entity);
        if (meter == nullptr || meter->value < catalog->thresholds.front().value) {
            continue;
        }
        for (int32_t i = static_cast<int32_t>(catalog->thresholds.size()) - 1; i >= 0; --i) {
            if (meter->value < catalog->thresholds[i].value) {
                continue;
            }
            const std::vector<int32_t> candidates =
                    disasters_of_tier(*catalog, catalog->thresholds[i].tier);
            if (candidates.empty()) {
                continue;
            }
            const int32_t pick = rng.range_int(0, static_cast<int32_t>(candidates.size()) - 1);
            const int32_t disaster_index = candidates[pick];
            const ecs::DisasterDef &def = catalog->disasters[disaster_index];
            meter->value = 0;

            if (def.target == ecs::kTargetOpponent) {
                const int32_t team = registry.get<const ecs::PlayerInfo>(player_entity).team;
                const std::vector<int32_t> opps = opponents_of(registry, player_id, team);
                if (opps.empty()) {
                    break; // некого атаковать
                }
                const bool dark = player_has_research(registry, player_entity,
                        ecs::kResearchDarkMagic);
                const bool human = !registry.get<const ecs::PlayerInfo>(player_entity).is_ai;
                if (dark && human && opps.size() >= 2) {
                    // Мини-решение: 2 случайных конкурента (SPEC §4/§8).
                    const int32_t a = opps[rng.range_int(0, static_cast<int32_t>(opps.size()) - 1)];
                    int32_t b = a;
                    while (b == a) {
                        b = opps[rng.range_int(0, static_cast<int32_t>(opps.size()) - 1)];
                    }
                    auto &pending = registry.get_or_emplace<ecs::PendingChoices>(match);
                    pending.choices.push_back({player_id, disaster_index, a, b});
                    Event choice;
                    choice.type = kEventTargetChoice;
                    choice.payload["chooser"] = std::to_string(player_id);
                    choice.payload["disaster"] = def.id;
                    choice.payload["candidate_a"] = std::to_string(a);
                    choice.payload["candidate_b"] = std::to_string(b);
                    events.push_back(std::move(choice));
                } else {
                    const int32_t victim = opps[rng.range_int(0, static_cast<int32_t>(opps.size()) - 1)];
                    apply_effect(def, registry, match, player_id, victim, rng, events);
                }
            } else {
                apply_effect(def, registry, match, player_id, player_id, rng, events);
            }
            break;
        }
    }
    match_rng->state = rng.raw_state();
}

bool disasters_held(const entt::registry &registry) {
    const auto *pending = registry.try_get<const ecs::PendingChoices>(match_entity(registry));
    return pending != nullptr && !pending->choices.empty();
}

void resolve_pending_choices_random(entt::registry &registry, std::vector<Event> &events) {
    const entt::entity match = match_entity(registry);
    auto *pending = registry.try_get<ecs::PendingChoices>(match);
    const auto *catalog = registry.try_get<const ecs::DisasterCatalog>(match);
    auto *match_rng = registry.try_get<ecs::MatchRng>(match);
    if (pending == nullptr || catalog == nullptr || match_rng == nullptr) {
        return;
    }
    math::Rng rng(match_rng->state);
    for (const ecs::DarkChoice &c : pending->choices) {
        const int32_t victim = rng.range_int(0, 1) == 0 ? c.candidate_a : c.candidate_b;
        apply_effect(catalog->disasters[c.disaster_index], registry, match, c.chooser, victim, rng,
                events);
    }
    pending->choices.clear();
    match_rng->state = rng.raw_state();
}

void restore_disabled(entt::registry &registry) {
    const int32_t turn = current_turn(registry);
    for (auto [entity, b] : registry.view<ecs::Building>().each()) {
        if (b.status == static_cast<int32_t>(BuildingStatus::Disabled) && b.restore_turn > 0 &&
                turn >= b.restore_turn) {
            b.status = static_cast<int32_t>(BuildingStatus::Active);
            b.restore_turn = 0;
        }
    }
}

int32_t clairvoyance_tier(const entt::registry &registry, entt::entity player) {
    if (!player_has_research(registry, player, ecs::kResearchClairvoyance)) {
        return 0;
    }
    const auto *catalog = registry.try_get<const ecs::DisasterCatalog>(match_entity(registry));
    const auto *meter = registry.try_get<const ecs::DangerMeter>(player);
    if (catalog == nullptr || meter == nullptr) {
        return 0;
    }
    int32_t tier = 0;
    for (const ecs::DangerThreshold &t : catalog->thresholds) {
        if (meter->value >= t.value) {
            tier = t.tier;
        }
    }
    return tier;
}

IntentResult handle_target_pick(entt::registry &registry, int32_t player_id, const Intent &intent) {
    IntentResult result;
    result.type = intent.type;
    const entt::entity match = match_entity(registry);
    auto *pending = registry.try_get<ecs::PendingChoices>(match);
    const auto *catalog = registry.try_get<const ecs::DisasterCatalog>(match);
    if (pending == nullptr || catalog == nullptr) {
        result.reason = kRejectNoChoice;
        return result;
    }
    for (size_t i = 0; i < pending->choices.size(); ++i) {
        if (pending->choices[i].chooser != player_id) {
            continue;
        }
        const ecs::DarkChoice choice = pending->choices[i];
        const auto pick_it = intent.payload.find(kPayloadPick);
        const int32_t pick = pick_it != intent.payload.end() && pick_it->second == "1" ? 1 : 0;
        const int32_t victim = pick == 0 ? choice.candidate_a : choice.candidate_b;

        auto &queue = registry.get_or_emplace<ecs::EventQueue>(match);
        auto *match_rng = registry.try_get<ecs::MatchRng>(match);
        math::Rng rng(match_rng != nullptr ? match_rng->state : 0);
        apply_effect(catalog->disasters[choice.disaster_index], registry, match, player_id, victim,
                rng, queue.events);
        if (match_rng != nullptr) {
            match_rng->state = rng.raw_state();
        }
        pending->choices.erase(pending->choices.begin() + i);
        result.accepted = true;
        result.payload["victim"] = std::to_string(victim);
        return result;
    }
    result.reason = kRejectNoChoice;
    return result;
}

IntentResult handle_cure(entt::registry &registry, int32_t player_id, const Intent &intent) {
    IntentResult result;
    result.type = intent.type;
    const entt::entity match = match_entity(registry);
    const auto &state = registry.get<const ecs::MatchState>(match);
    if (state.phase != static_cast<int32_t>(Phase::Development)) {
        result.reason = kRejectWrongPhase;
        return result;
    }
    const entt::entity player = player_by_id(registry, player_id);
    auto *grid = registry.try_get<ecs::PlayerGrid>(player);
    const auto *catalog = registry.try_get<const ecs::DisasterCatalog>(match);
    if (player == entt::null || grid == nullptr || catalog == nullptr) {
        result.reason = kRejectNoBuildingHere;
        return result;
    }
    int32_t cure_gold = 2;
    for (const ecs::DisasterDef &d : catalog->disasters) {
        if (d.effect == ecs::kEffectDisease) {
            cure_gold = d.param("cure_gold", 2);
        }
    }
    const auto cx_it = intent.payload.find(kPayloadCellX);
    const auto cz_it = intent.payload.find(kPayloadCellZ);
    if (cx_it == intent.payload.end() || cz_it == intent.payload.end()) {
        result.reason = kRejectNoBuildingHere;
        return result;
    }
    const int32_t cx = std::atoi(cx_it->second.c_str());
    const int32_t cz = std::atoi(cz_it->second.c_str());
    if (!grid->in_bounds(cx, cz)) {
        result.reason = kRejectOutOfBounds;
        return result;
    }
    const uint32_t occ = grid->occupancy[grid->index_of(cx, cz)];
    if (occ == ecs::kCellFree) {
        result.reason = kRejectNoBuildingHere;
        return result;
    }
    auto &b = registry.get<ecs::Building>(static_cast<entt::entity>(occ));
    if (b.player_id != player_id || b.status != static_cast<int32_t>(BuildingStatus::Diseased)) {
        result.reason = kRejectNotDiseased;
        return result;
    }
    auto &resources = registry.get<ecs::Resources>(player);
    if (resources.gold < cure_gold) {
        result.reason = kRejectNotEnoughResources;
        return result;
    }
    resources.gold -= cure_gold;
    b.status = static_cast<int32_t>(BuildingStatus::Active);
    result.accepted = true;
    return result;
}

IntentResult handle_storm_rite(entt::registry &registry, int32_t player_id, const Intent &intent) {
    IntentResult result;
    result.type = intent.type;
    const entt::entity match = match_entity(registry);
    const auto &state = registry.get<const ecs::MatchState>(match);
    if (state.phase != static_cast<int32_t>(Phase::Development)) {
        result.reason = kRejectWrongPhase;
        return result;
    }
    const entt::entity player = player_by_id(registry, player_id);
    const auto *catalog = registry.try_get<const ecs::DisasterCatalog>(match);
    auto *match_rng = registry.try_get<ecs::MatchRng>(match);
    if (player == entt::null || catalog == nullptr || match_rng == nullptr) {
        result.reason = kRejectBadRaidTarget;
        return result;
    }
    if (!player_has_research(registry, player, ecs::kResearchStormRite)) {
        result.reason = kRejectNoStormRite;
        return result;
    }
    auto &cooldowns = registry.get_or_emplace<ecs::PlayerCooldowns>(player);
    if (state.turn < cooldowns.storm_rite_ready_turn) {
        result.reason = kRejectStormRiteCooldown;
        return result;
    }
    const int32_t target = intent.payload.count(kPayloadTargetPlayer)
            ? std::atoi(intent.payload.at(kPayloadTargetPlayer).c_str())
            : -1;
    const entt::entity target_entity = player_by_id(registry, target);
    if (target_entity == entt::null || target == player_id) {
        result.reason = kRejectBadRaidTarget;
        return result;
    }
    if (registry.get<const ecs::PlayerInfo>(target_entity).team ==
            registry.get<const ecs::PlayerInfo>(player).team) {
        result.reason = kRejectAllyTarget;
        return result;
    }

    const std::vector<int32_t> tier_ii = disasters_of_tier(*catalog, kStormRiteTier);
    if (tier_ii.empty()) {
        result.reason = kRejectNoStormRite;
        return result;
    }
    math::Rng rng(match_rng->state);
    const int32_t pick = tier_ii[rng.range_int(0, static_cast<int32_t>(tier_ii.size()) - 1)];
    auto &queue = registry.get_or_emplace<ecs::EventQueue>(match);
    apply_effect(catalog->disasters[pick], registry, match, player_id, target, rng, queue.events);
    match_rng->state = rng.raw_state();
    cooldowns.storm_rite_ready_turn = state.turn + kStormRiteCooldownTurns;

    result.accepted = true;
    result.payload["target"] = std::to_string(target);
    return result;
}

} // namespace dicecore::systems
