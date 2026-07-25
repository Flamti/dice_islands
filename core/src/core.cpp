#include "dicecore/core.hpp"

#include "ecs/components_building.hpp"
#include "gen/island_gen.hpp"
#include "math/rng.hpp"
#include "net/intents.hpp"
#include "systems/dice.hpp"
#include "systems/economy.hpp"
#include "systems/placement.hpp"
#include "turn/turn_machine.hpp"

#include <entt/entt.hpp>

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

    entt::entity find_player_entity(int32_t player_id) {
        for (auto [entity, info] : registry.view<const ecs::PlayerInfo>().each()) {
            if (info.id == player_id) {
                return entity;
            }
        }
        return entt::null;
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
        gen::GeneratorParams gen_params;
        if (!config.generator_json.empty() &&
                !gen::params_from_json(config.generator_json, gen_params, parse_error)) {
            error = kErrorBadGeneratorConfig;
            return false;
        }

        const entt::entity match = registry.view<ecs::MatchState>().front();
        registry.emplace<ecs::BuildingCatalog>(match, catalog);

        for (const PlayerConfig &player : config.players) {
            const entt::entity entity = find_player_entity(player.id);
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
        // Соль отделяет RNG партии от производных сидов островов.
        impl_->registry.emplace<ecs::MatchRng>(match,
                ecs::MatchRng{math::mix_seed(config.match_seed, 0xD1CEB0A7)});
    }
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
    const turn::PhaseHook hook = [](entt::registry &registry, Phase phase) {
        switch (phase) {
            case Phase::TurnStart:
                systems::activate_constructions(registry);
                break;
            case Phase::Income:
                systems::collect_income(registry);
                break;
            case Phase::Rolls:
                systems::roll_initial(registry);
                break;
            case Phase::Resources:
                systems::apply_dice_income(registry);
                break;
            default:
                break;
        }
    };
    turn::tick(impl_->registry, dt, events, hook);
    return events;
}

TurnSnapshot Match::snapshot() const {
    if (!impl_->active) {
        return TurnSnapshot{};
    }
    return turn::make_snapshot(impl_->registry);
}

} // namespace dicecore
