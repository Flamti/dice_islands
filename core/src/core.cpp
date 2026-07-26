#include "dicecore/core.hpp"

#include "ecs/components_building.hpp"
#include "ecs/components_disaster.hpp"
#include "ecs/components_research.hpp"
#include "gen/glb_export.hpp"
#include "gen/island_gen.hpp"
#include "math/rng.hpp"
#include "save/json.hpp"
#include "net/intents.hpp"
#include "systems/combat/battle.hpp"
#include "systems/combat/resolve.hpp"
#include "systems/danger.hpp"
#include "systems/dice.hpp"
#include "systems/economy.hpp"
#include "systems/expansion.hpp"
#include "systems/placement.hpp"
#include "systems/research.hpp"
#include "systems/upkeep.hpp"
#include "systems/victory.hpp"
#include "turn/turn_machine.hpp"

#include <entt/entt.hpp>

#include <algorithm>
#include <cstdlib>
#include <set>

namespace dicecore {

IntentResult process_intent(const Intent &intent) {
    IntentResult result;
    result.type = intent.type;

    if (intent.type.empty()) {
        result.reason = kRejectEmptyType;
        return result;
    }

    if (intent.type == kIntentEcho) {
        // Подтверждение возвращает полезную нагрузку без изменений.
        result.accepted = true;
        result.payload = intent.payload;
        return result;
    }

    result.reason = kRejectUnknownIntent;
    return result;
}

struct Match::Impl {
    entt::registry registry;
    bool active = false;
    std::vector<BattleLog> battle_logs; // накоплены в фазе Боя, отдаются адаптеру
    std::vector<IslandUpdate> island_updates; // ре-полигонизация после деструкции
    // Параметры генератора и сиды островов — для ре-генерации меша (SPEC §11.5).
    gen::GeneratorParams gen_params;
    std::map<int32_t, uint64_t> island_seeds;
    MatchConfig config; // конфиг старта — для сериализации сейва (SPEC §13)

    entt::entity find_player_entity(int32_t player_id) {
        for (auto [entity, info] : registry.view<const ecs::PlayerInfo>().each()) {
            if (info.id == player_id) {
                return entity;
            }
        }
        return entt::null;
    }

    // Игроки с dirty-вырезкой: ре-полигонизуем меш с вырезанными клетками и
    // складываем новый GLB для рассылки клиентам (SPEC §11.5).
    void regenerate_carved_islands() {
        for (auto [entity, carved] : registry.view<ecs::PlayerCarved>().each()) {
            if (!carved.dirty) {
                continue;
            }
            carved.dirty = false;
            const int32_t player_id = registry.get<const ecs::PlayerInfo>(entity).id;
            const auto seed_it = island_seeds.find(player_id);
            if (seed_it == island_seeds.end()) {
                continue;
            }
            std::vector<gen::GridCell> cells;
            for (const ecs::CarvedCell &c : carved.cells) {
                cells.push_back({c.cell_x, c.cell_z});
            }
            const gen::IslandData island =
                    gen::generate_island_carved(seed_it->second, gen_params, cells);
            IslandUpdate update;
            update.player = player_id;
            update.glb = gen::export_glb(island);
            island_updates.push_back(std::move(update));
        }
    }

    // Стройплощадка партии: каталог зданий, сетки островов, стартовые
    // ресурсы, предразмещённые замки (SPEC §2.2). Вызывается после
    // turn::start_match, когда сущности партии и игроков уже созданы.
    bool setup_buildings(const MatchConfig &config, std::string &error) {
        ecs::BuildingCatalog catalog;
        std::string parse_error;
        if (!systems::parse_buildings_json(config.buildings_json, catalog, parse_error)) {
            error = kErrorBadBuildingsConfig;
            return false;
        }
        if (!config.generator_json.empty() &&
                !gen::params_from_json(config.generator_json, gen_params, parse_error)) {
            error = kErrorBadGeneratorConfig;
            return false;
        }

        const entt::entity match = registry.view<ecs::MatchState>().front();
        registry.emplace<ecs::BuildingCatalog>(match, catalog);

        for (const PlayerConfig &player : config.players) {
            const entt::entity entity = find_player_entity(player.id);
            island_seeds[player.id] = player.island_seed;
            const gen::GridData grid_data = gen::generate_grid(player.island_seed, gen_params);
            auto &grid = registry.emplace<ecs::PlayerGrid>(entity);
            grid.cells_x = grid_data.cells_x;
            grid.cells_z = grid_data.cells_z;
            grid.cell_size = grid_data.cell_size;
            grid.origin_x = grid_data.origin_x;
            grid.origin_z = grid_data.origin_z;
            grid.types = grid_data.types;
            grid.heights = grid_data.heights;
            grid.occupancy.assign(grid.types.size(), ecs::kCellFree);
            for (const gen::PoiSpot &poi : grid_data.poi) {
                grid.pois.push_back({poi.cell_x, poi.cell_z, poi.kind, poi.amount});
            }

            registry.get<ecs::Resources>(entity) = catalog.starting_resources;
            if (!systems::preplace_castle(registry, entity, catalog, error)) {
                return false;
            }
        }
        return true;
    }
};

Match::Match() : impl_(std::make_unique<Impl>()) {}

Match::~Match() = default;

bool Match::start(const MatchConfig &config, std::string &error) {
    if (impl_->active) {
        error = kErrorMatchAlreadyActive;
        return false;
    }
    if (config.players.empty()) {
        error = kErrorBadMatchConfig;
        return false;
    }
    std::set<int32_t> ids;
    for (const PlayerConfig &player : config.players) {
        if (!ids.insert(player.id).second) {
            error = kErrorBadMatchConfig; // дубликат ID игрока
            return false;
        }
    }

    std::vector<Event> start_events; // события старта отражены в первом снапшоте
    turn::start_match(impl_->registry, config, start_events);
    // Пустой buildings_json — партия без зданий и сеток (тесты этапа 2).
    if (!config.buildings_json.empty() && !impl_->setup_buildings(config, error)) {
        impl_->registry.clear();
        return false;
    }
    // Единственный RNG партии (SPEC §12.1); соль отделяет его от производных
    // сидов островов. Нужен всем системам с случайностью (кубики, голод).
    impl_->registry.emplace<ecs::MatchRng>(impl_->registry.view<ecs::MatchState>().front(),
            ecs::MatchRng{math::mix_seed(config.match_seed, 0xD1CEB0A7)});
    if (!config.dice_json.empty()) {
        ecs::DiceCatalog catalog;
        std::string parse_error;
        if (!systems::parse_dice_json(config.dice_json, catalog, parse_error)) {
            impl_->registry.clear();
            error = kErrorBadDiceConfig;
            return false;
        }
        const entt::entity match = impl_->registry.view<ecs::MatchState>().front();
        impl_->registry.emplace<ecs::DiceCatalog>(match, std::move(catalog));
    }
    if (!config.disasters_json.empty()) {
        ecs::DisasterCatalog catalog;
        std::string parse_error;
        if (!systems::parse_disasters_json(config.disasters_json, catalog, parse_error)) {
            impl_->registry.clear();
            error = kErrorBadDisastersConfig;
            return false;
        }
        const entt::entity match = impl_->registry.view<ecs::MatchState>().front();
        impl_->registry.emplace<ecs::DisasterCatalog>(match, std::move(catalog));
    }
    if (!config.combat_json.empty()) {
        systems::combat::CombatConfig combat_config;
        std::string parse_error;
        if (!systems::combat::parse_combat_json(config.combat_json, combat_config, parse_error)) {
            impl_->registry.clear();
            error = kErrorBadCombatConfig;
            return false;
        }
        const entt::entity match = impl_->registry.view<ecs::MatchState>().front();
        impl_->registry.emplace<systems::combat::CombatConfig>(match, combat_config);
    }
    if (!config.research_json.empty()) {
        ecs::ResearchCatalog catalog;
        std::string parse_error;
        if (!systems::parse_research_json(config.research_json, catalog, parse_error)) {
            impl_->registry.clear();
            error = kErrorBadResearchConfig;
            return false;
        }
        const entt::entity match = impl_->registry.view<ecs::MatchState>().front();
        impl_->registry.emplace<ecs::ResearchCatalog>(match, std::move(catalog));
    }
    impl_->config = config;
    impl_->active = true;
    error.clear();
    return true;
}

bool Match::active() const {
    return impl_->active;
}

IntentResult Match::submit_intent(int32_t player_id, const Intent &intent) {
    IntentResult result;
    result.type = intent.type;

    if (intent.type.empty()) {
        result.reason = kRejectEmptyType;
        return result;
    }
    // Стейтлес-намерения работают и до старта партии (лобби, эхо-тест).
    if (intent.type == kIntentEcho) {
        return process_intent(intent);
    }
    if (!impl_->active) {
        result.reason = kRejectNoActiveMatch;
        return result;
    }
    return net::dispatch_intent(impl_->registry, player_id, intent);
}

std::vector<Event> Match::tick(double dt) {
    std::vector<Event> events;
    if (!impl_->active) {
        return events;
    }
    // Эффекты фаз (SPEC §4) выполняются в момент входа, до продвижения дальше
    // внутри того же тика: активация -> сбор пула -> бросок -> конвертация.
    std::vector<BattleLog> &battle_logs = impl_->battle_logs;
    const turn::PhaseHook hook = [&events, &battle_logs](entt::registry &registry, Phase phase) {
        switch (phase) {
            case Phase::TurnStart:
                // Активация построек, снятие Disabled, расширение платформами.
                systems::restore_disabled(registry);
                systems::activate_constructions(registry);
                systems::expand_platforms(registry, events);
                break;
            case Phase::Food:
                systems::apply_food_upkeep(registry);
                break;
            case Phase::Income:
                systems::collect_income(registry);
                break;
            case Phase::Rolls:
                systems::roll_initial(registry);
                break;
            case Phase::Resources:
                systems::apply_dice_income(registry);
                systems::apply_research_income(registry); // Ремесло: +молотки/ход
                break;
            case Phase::Disasters:
                systems::resolve_danger_phase(registry, events);
                break;
            case Phase::Combat:
                systems::combat::resolve_combat_phase(registry, events, battle_logs);
                break;
            case Phase::Checks:
                systems::resolve_checks_phase(registry, events);
                break;
            default:
                break;
        }
    };
    const turn::HoldPredicate is_held = [](const entt::registry &registry, Phase phase) {
        return phase == Phase::Disasters && systems::disasters_held(registry);
    };
    const turn::HoldTimeout on_hold_timeout = [&events](entt::registry &registry, Phase phase) {
        if (phase == Phase::Disasters) {
            systems::resolve_pending_choices_random(registry, events);
        }
    };
    turn::tick(impl_->registry, dt, events, hook, is_held, on_hold_timeout);

    // События, порождённые вне тика (выбор цели Тёмной магией) — в общий поток.
    const entt::entity match = impl_->registry.view<ecs::MatchState>().front();
    if (auto *queue = impl_->registry.try_get<ecs::EventQueue>(match)) {
        for (Event &e : queue->events) {
            events.push_back(std::move(e));
        }
        queue->events.clear();
    }
    // Ре-полигонизация островов после деструкции ландшафта (SPEC §11.5).
    impl_->regenerate_carved_islands();
    return events;
}

std::vector<BattleLog> Match::take_battle_logs() {
    std::vector<BattleLog> logs;
    logs.swap(impl_->battle_logs);
    return logs;
}

std::vector<IslandUpdate> Match::take_island_updates() {
    std::vector<IslandUpdate> updates;
    updates.swap(impl_->island_updates);
    return updates;
}

namespace {

constexpr int32_t kSaveVersion = 1;

std::string quoted(const std::string &value) {
    return "\"" + save::escape(value) + "\"";
}

const ecs::BuildingDef &def_of(const ecs::BuildingCatalog &catalog, const ecs::Building &b) {
    return catalog.defs[b.def_index];
}

} // namespace

std::string Match::save() const {
    entt::registry &reg = impl_->registry;
    const entt::entity match = reg.view<ecs::MatchState>().front();
    const auto &state = reg.get<const ecs::MatchState>(match);
    const auto &rng = reg.get<const ecs::MatchRng>(match);
    const auto *bcatalog = reg.try_get<const ecs::BuildingCatalog>(match);

    std::string out = "{";
    out += "\"version\":" + std::to_string(kSaveVersion);
    out += ",\"match_seed\":\"" + std::to_string(impl_->config.match_seed) + "\"";
    out += ",\"generator_json\":" + quoted(impl_->config.generator_json);
    out += ",\"buildings_json\":" + quoted(impl_->config.buildings_json);
    out += ",\"dice_json\":" + quoted(impl_->config.dice_json);
    out += ",\"disasters_json\":" + quoted(impl_->config.disasters_json);
    out += ",\"combat_json\":" + quoted(impl_->config.combat_json);
    out += ",\"research_json\":" + quoted(impl_->config.research_json);
    out += ",\"turn\":" + std::to_string(state.turn);
    out += ",\"finished\":" + std::string(state.finished ? "true" : "false");
    out += ",\"winner_team\":" + std::to_string(state.winner_team);
    out += ",\"rng\":\"" + std::to_string(rng.state) + "\"";

    // --- Игроки ---
    out += ",\"players\":[";
    bool first_player = true;
    for (auto [entity, info, res] :
            reg.view<const ecs::PlayerInfo, const ecs::Resources>().each()) {
        if (!first_player) {
            out += ",";
        }
        first_player = false;
        out += "{\"id\":" + std::to_string(info.id);
        out += ",\"team\":" + std::to_string(info.team);
        out += ",\"is_ai\":" + std::string(info.is_ai ? "true" : "false");
        out += ",\"alive\":" + std::string(info.alive ? "true" : "false");
        out += ",\"island_seed\":\"" + std::to_string(impl_->island_seeds.count(info.id)
                        ? impl_->island_seeds.at(info.id)
                        : 0) + "\"";
        out += ",\"wood\":" + std::to_string(res.wood);
        out += ",\"stone\":" + std::to_string(res.stone);
        out += ",\"food\":" + std::to_string(res.food);
        out += ",\"gold\":" + std::to_string(res.gold);
        out += ",\"hammers\":" + std::to_string(res.hammers);
        out += ",\"swords\":" + std::to_string(res.swords);
        out += ",\"culture\":" + std::to_string(res.culture);
        const auto *meter = reg.try_get<const ecs::DangerMeter>(entity);
        out += ",\"danger\":" + std::to_string(meter != nullptr ? meter->value : 0);
        const auto *cd = reg.try_get<const ecs::PlayerCooldowns>(entity);
        out += ",\"storm_rite_ready\":" +
                std::to_string(cd != nullptr ? cd->storm_rite_ready_turn : 0);
        out += ",\"research\":[";
        const auto *research = reg.try_get<const ecs::PlayerResearch>(entity);
        if (research != nullptr) {
            bool fe = true;
            for (const std::string &e : research->unlocked) {
                out += (fe ? "" : ",") + quoted(e);
                fe = false;
            }
        }
        out += "]";
        out += ",\"carved\":[";
        const auto *carved = reg.try_get<const ecs::PlayerCarved>(entity);
        if (carved != nullptr) {
            bool fc = true;
            for (const ecs::CarvedCell &c : carved->cells) {
                out += (fc ? "" : ",") + ("[" + std::to_string(c.cell_x) + "," +
                        std::to_string(c.cell_z) + "]");
                fc = false;
            }
        }
        out += "]";
        out += ",\"pois\":[";
        const auto *grid = reg.try_get<const ecs::PlayerGrid>(entity);
        if (grid != nullptr) {
            bool fp = true;
            for (const ecs::PlayerPoi &p : grid->pois) {
                out += (fp ? "" : ",") + ("[" + std::to_string(p.cell_x) + "," +
                        std::to_string(p.cell_z) + "," + quoted(p.kind) + "," +
                        std::to_string(p.amount) + "]");
                fp = false;
            }
        }
        out += "]}";
    }
    out += "]";

    // --- Здания ---
    out += ",\"buildings\":[";
    bool first_building = true;
    if (bcatalog != nullptr) {
        for (auto [entity, b] : reg.view<const ecs::Building>().each()) {
            if (!first_building) {
                out += ",";
            }
            first_building = false;
            out += "{\"player\":" + std::to_string(b.player_id);
            out += ",\"def\":" + quoted(def_of(*bcatalog, b).id);
            out += ",\"cell_x\":" + std::to_string(b.cell_x);
            out += ",\"cell_z\":" + std::to_string(b.cell_z);
            out += ",\"hp\":" + std::to_string(b.hp);
            out += ",\"status\":" + std::to_string(b.status);
            out += ",\"expanded\":" + std::string(b.expanded ? "true" : "false");
            out += ",\"restore_turn\":" + std::to_string(b.restore_turn);
            out += ",\"wonder_stage\":" + std::to_string(b.wonder_stage);
            out += ",\"wonder_complete_turn\":" + std::to_string(b.wonder_complete_turn);
            out += "}";
        }
    }
    out += "]}";
    return out;
}

bool Match::load(const std::string &json_text, std::string &error) {
    save::JsonValue root;
    if (!save::parse_json(json_text, root, error) || !root.is_object() ||
            root.int_at("version", 0) != kSaveVersion) {
        error = kErrorBadSave;
        return false;
    }

    // Реконструируем конфиг старта из сейва.
    MatchConfig config;
    config.match_seed = std::strtoull(
            root.as_object().count("match_seed") ? root.as_object().at("match_seed").as_string().c_str()
                                                 : "0",
            nullptr, 10);
    auto config_str = [&](const char *key) {
        return root.as_object().count(key) ? root.as_object().at(key).as_string() : std::string();
    };
    config.generator_json = config_str("generator_json");
    config.buildings_json = config_str("buildings_json");
    config.dice_json = config_str("dice_json");
    config.disasters_json = config_str("disasters_json");
    config.combat_json = config_str("combat_json");
    config.research_json = config_str("research_json");
    for (const save::JsonValue &pj : root.as_object().at("players").as_array()) {
        PlayerConfig p;
        p.id = pj.int_at("id", 0);
        p.team = pj.int_at("team", 1);
        p.is_ai = pj.as_object().count("is_ai") && pj.as_object().at("is_ai").as_bool();
        p.island_seed = std::strtoull(
                pj.as_object().count("island_seed") ? pj.as_object().at("island_seed").as_string().c_str()
                                                    : "0",
                nullptr, 10);
        config.players.push_back(p);
    }

    // Чистый старт из конфига (сетки, каталоги, предразмещённые замки).
    impl_->registry.clear();
    impl_->island_seeds.clear();
    impl_->active = false;
    if (!start(config, error)) {
        return false;
    }

    entt::registry &reg = impl_->registry;
    const entt::entity match = reg.view<ecs::MatchState>().front();

    // Стейт хода и RNG.
    auto &state = reg.get<ecs::MatchState>(match);
    state.turn = root.int_at("turn", 1);
    state.phase = 0; // сейв — на границе хода (SPEC §13)
    state.phase_elapsed_sec = 0.0;
    state.finished = root.as_object().count("finished") && root.as_object().at("finished").as_bool();
    state.winner_team = root.int_at("winner_team", -1);
    reg.get<ecs::MatchRng>(match).state = std::strtoull(
            root.as_object().count("rng") ? root.as_object().at("rng").as_string().c_str() : "0",
            nullptr, 10);

    auto find_player = [&](int32_t id) -> entt::entity {
        for (auto [e, info] : reg.view<const ecs::PlayerInfo>().each()) {
            if (info.id == id) {
                return e;
            }
        }
        return entt::null;
    };

    // Динамика игроков.
    for (const save::JsonValue &pj : root.as_object().at("players").as_array()) {
        const entt::entity e = find_player(pj.int_at("id", 0));
        if (e == entt::null) {
            continue;
        }
        reg.get<ecs::PlayerInfo>(e).alive =
                !pj.as_object().count("alive") || pj.as_object().at("alive").as_bool();
        auto &res = reg.get<ecs::Resources>(e);
        res.wood = pj.int_at("wood", 0);
        res.stone = pj.int_at("stone", 0);
        res.food = pj.int_at("food", 0);
        res.gold = pj.int_at("gold", 0);
        res.hammers = pj.int_at("hammers", 0);
        res.swords = pj.int_at("swords", 0);
        res.culture = pj.int_at("culture", 0);
        reg.get_or_emplace<ecs::DangerMeter>(e).value = pj.int_at("danger", 0);
        reg.get_or_emplace<ecs::PlayerCooldowns>(e).storm_rite_ready_turn =
                pj.int_at("storm_rite_ready", 0);
        auto &research = reg.get_or_emplace<ecs::PlayerResearch>(e);
        research.unlocked.clear();
        if (pj.as_object().count("research")) {
            for (const save::JsonValue &r : pj.as_object().at("research").as_array()) {
                research.unlocked.insert(r.as_string());
            }
        }
        auto *grid = reg.try_get<ecs::PlayerGrid>(e);
        if (grid != nullptr) {
            // Вырезанные клетки -> Void; накопление для ре-полигонизации.
            auto &carved = reg.get_or_emplace<ecs::PlayerCarved>(e);
            carved.cells.clear();
            if (pj.as_object().count("carved")) {
                for (const save::JsonValue &c : pj.as_object().at("carved").as_array()) {
                    const int32_t cx = static_cast<int32_t>(c.as_array()[0].as_number(0));
                    const int32_t cz = static_cast<int32_t>(c.as_array()[1].as_number(0));
                    carved.cells.push_back({cx, cz});
                    if (grid->in_bounds(cx, cz)) {
                        grid->types[grid->index_of(cx, cz)] =
                                static_cast<int32_t>(gen::CellType::Void);
                    }
                }
            }
            // POI: замещаем оставшимися из сейва.
            grid->pois.clear();
            if (pj.as_object().count("pois")) {
                for (const save::JsonValue &p : pj.as_object().at("pois").as_array()) {
                    ecs::PlayerPoi poi;
                    poi.cell_x = static_cast<int32_t>(p.as_array()[0].as_number(0));
                    poi.cell_z = static_cast<int32_t>(p.as_array()[1].as_number(0));
                    poi.kind = p.as_array()[2].as_string();
                    poi.amount = static_cast<int32_t>(p.as_array()[3].as_number(0));
                    grid->pois.push_back(poi);
                }
            }
        }
    }

    // Здания: сносим созданные стартом (замки) и восстанавливаем из сейва.
    reg.view<ecs::Building>().each([&](entt::entity e, ecs::Building &) { reg.destroy(e); });
    for (auto [e, grid] : reg.view<ecs::PlayerGrid>().each()) {
        std::fill(grid.occupancy.begin(), grid.occupancy.end(), ecs::kCellFree);
    }
    const auto *bcatalog = reg.try_get<const ecs::BuildingCatalog>(match);
    if (bcatalog != nullptr && root.as_object().count("buildings")) {
        for (const save::JsonValue &bj : root.as_object().at("buildings").as_array()) {
            const int32_t def_index = systems::find_building_def(*bcatalog, bj.as_object().count("def")
                            ? bj.as_object().at("def").as_string()
                            : "");
            if (def_index < 0) {
                continue;
            }
            const ecs::BuildingDef &def = bcatalog->defs[def_index];
            const entt::entity entity = reg.create();
            auto &b = reg.emplace<ecs::Building>(entity);
            b.player_id = bj.int_at("player", 0);
            b.def_index = def_index;
            b.cell_x = bj.int_at("cell_x", 0);
            b.cell_z = bj.int_at("cell_z", 0);
            b.size_x = def.size_x;
            b.size_z = def.size_z;
            b.hp = bj.int_at("hp", def.hp);
            b.status = bj.int_at("status", static_cast<int32_t>(BuildingStatus::Active));
            b.expanded = bj.as_object().count("expanded") && bj.as_object().at("expanded").as_bool();
            b.restore_turn = bj.int_at("restore_turn", 0);
            b.wonder_stage = bj.int_at("wonder_stage", 0);
            b.wonder_complete_turn = bj.int_at("wonder_complete_turn", 0);
            // Занятость сетки владельца (руины тоже держат клетки).
            auto *grid = reg.try_get<ecs::PlayerGrid>(find_player(b.player_id));
            if (grid != nullptr) {
                for (int32_t dz = 0; dz < b.size_z; ++dz) {
                    for (int32_t dx = 0; dx < b.size_x; ++dx) {
                        if (grid->in_bounds(b.cell_x + dx, b.cell_z + dz)) {
                            grid->occupancy[grid->index_of(b.cell_x + dx, b.cell_z + dz)] =
                                    static_cast<uint32_t>(entity);
                        }
                    }
                }
            }
        }
    }
    error.clear();
    return true;
}

void Match::set_player_ai(int32_t player_id, bool is_ai) {
    for (auto [entity, info] : impl_->registry.view<ecs::PlayerInfo>().each()) {
        if (info.id != player_id) {
            continue;
        }
        info.is_ai = is_ai;
        // Слот, переданный ИИ во время фазы-решения, сразу готов — иначе барьер
        // текущей фазы завис бы без человека (SPEC §2.3, §12.2).
        if (is_ai) {
            if (auto *ready = impl_->registry.try_get<ecs::PhaseReady>(entity)) {
                ready->ready = true;
            }
        }
        return;
    }
}

TurnSnapshot Match::snapshot() const {
    if (!impl_->active) {
        return TurnSnapshot{};
    }
    return turn::make_snapshot(impl_->registry);
}

} // namespace dicecore
