#include "turn/turn_machine.hpp"

#include "ecs/components.hpp"
#include "ecs/components_building.hpp"
#include "ecs/components_disaster.hpp"
#include "ecs/components_research.hpp"
#include "gen/island_gen.hpp"
#include "systems/danger.hpp"
#include "systems/economy.hpp"
#include "systems/research.hpp"

#include <algorithm>
#include <string>

namespace dicecore::turn {

namespace {

std::string to_string_bool(bool value) {
    return value ? "true" : "false";
}

entt::entity match_entity(const entt::registry &registry) {
    // Сущность партии одна; view по MatchState находит её без хранения ID.
    auto view = registry.view<const ecs::MatchState>();
    return view.front();
}

// Лимит таймера текущей фазы-решения; <= 0 — без лимита.
double phase_time_limit(const ecs::MatchTimers &timers, Phase phase) {
    switch (phase) {
        case Phase::Rolls:
            return timers.rolls_sec;
        case Phase::Development:
            return timers.development_sec;
        case Phase::Raids:
            return timers.raids_sec;
        default:
            return 0.0;
    }
}

bool all_alive_ready(const entt::registry &registry) {
    auto view = registry.view<const ecs::PlayerInfo, const ecs::PhaseReady>();
    for (auto [entity, info, ready] : view.each()) {
        if (info.alive && !ready.ready) {
            return false;
        }
    }
    return true;
}

void clear_ready_flags(entt::registry &registry) {
    auto view = registry.view<ecs::PhaseReady>();
    for (auto [entity, ready] : view.each()) {
        ready.ready = false;
    }
}

// ИИ-заглушка этапа 2: мгновенный PhaseReady при входе в фазу-решение.
// С этапа 5 ИИ будет подавать намерения через общий интерфейс (ARCHITECTURE §4).
void auto_ready_ai(entt::registry &registry) {
    auto view = registry.view<const ecs::PlayerInfo, ecs::PhaseReady>();
    for (auto [entity, info, ready] : view.each()) {
        if (info.alive && info.is_ai) {
            ready.ready = true;
        }
    }
}

void emit_turn_started(int32_t turn, std::vector<Event> &events) {
    Event event;
    event.type = kEventTurnStarted;
    event.payload["turn"] = std::to_string(turn);
    events.push_back(std::move(event));
}

void emit_phase_entered(const ecs::MatchState &state, std::vector<Event> &events) {
    Event event;
    event.type = kEventPhaseEntered;
    event.payload["turn"] = std::to_string(state.turn);
    event.payload["phase"] = std::to_string(state.phase);
    event.payload["is_decision"] = to_string_bool(phase_is_decision(static_cast<Phase>(state.phase)));
    events.push_back(std::move(event));
}

// Переход к следующей фазе (с переносом хода после фазы Проверок).
void advance_phase(entt::registry &registry, ecs::MatchState &state, std::vector<Event> &events,
        const PhaseHook &on_phase_entered) {
    state.phase_elapsed_sec = 0.0;
    state.phase = (state.phase + 1) % kPhaseCount;
    if (state.phase == static_cast<int32_t>(Phase::TurnStart)) {
        ++state.turn;
        emit_turn_started(state.turn, events);
    }
    clear_ready_flags(registry);
    if (phase_is_decision(static_cast<Phase>(state.phase))) {
        auto_ready_ai(registry);
    }
    emit_phase_entered(state, events);
    if (on_phase_entered) {
        // Эффекты фазы выполняются до дальнейшего продвижения в этом же тике.
        on_phase_entered(registry, static_cast<Phase>(state.phase));
    }
}

} // namespace

void start_match(entt::registry &registry, const MatchConfig &config, std::vector<Event> &events) {
    const entt::entity match = registry.create();
    auto &state = registry.emplace<ecs::MatchState>(match);
    auto &timers = registry.emplace<ecs::MatchTimers>(match);
    timers.rolls_sec = config.timers.rolls_sec;
    timers.development_sec = config.timers.development_sec;
    timers.raids_sec = config.timers.raids_sec;

    for (const PlayerConfig &player : config.players) {
        const entt::entity entity = registry.create();
        auto &info = registry.emplace<ecs::PlayerInfo>(entity);
        info.id = player.id;
        info.team = player.team;
        info.is_ai = player.is_ai;
        info.alive = true;
        registry.emplace<ecs::PhaseReady>(entity);
        registry.emplace<ecs::Resources>(entity);
        registry.emplace<ecs::DangerMeter>(entity);
    }

    state.turn = 1;
    state.phase = static_cast<int32_t>(Phase::TurnStart);
    state.phase_elapsed_sec = 0.0;
    emit_turn_started(state.turn, events);
    emit_phase_entered(state, events);
}

void tick(entt::registry &registry, double dt, std::vector<Event> &events,
        const PhaseHook &on_phase_entered, const HoldPredicate &is_held,
        const HoldTimeout &on_hold_timeout) {
    const entt::entity match = match_entity(registry);
    auto &state = registry.get<ecs::MatchState>(match);
    const auto &timers = registry.get<const ecs::MatchTimers>(match);
    if (state.finished) {
        return; // партия завершена — стейт-машина стоит (SPEC §10)
    }

    double budget = std::max(dt, 0.0);
    for (int32_t guard = 0; guard < kMaxTransitionsPerTick; ++guard) {
        if (state.finished) {
            return; // победа определена в хуке фазы Проверок — останавливаемся
        }
        const Phase phase = static_cast<Phase>(state.phase);
        if (phase_is_decision(phase)) {
            if (all_alive_ready(registry)) {
                advance_phase(registry, state, events, on_phase_entered);
                continue;
            }
            const double limit = phase_time_limit(timers, phase);
            if (limit <= 0.0) {
                return; // без лимита: ждём барьер PhaseReady
            }
            const double remaining = limit - state.phase_elapsed_sec;
            if (budget < remaining) {
                state.phase_elapsed_sec += budget;
                return;
            }
            // Таймер истёк: решения автозавершаются, кубики фиксируются как есть.
            budget -= remaining;
            advance_phase(registry, state, events, on_phase_entered);
        } else if (is_held && is_held(registry, phase)) {
            // Авто-фаза держится мини-решением (Тёмная магия): ждём выбор либо
            // таймер kDarkChoiceTimeoutSec, затем разрешаем отложенное случайно.
            const double remaining = kDarkChoiceTimeoutSec - state.phase_elapsed_sec;
            if (budget < remaining) {
                state.phase_elapsed_sec += budget;
                return;
            }
            budget -= remaining;
            if (on_hold_timeout) {
                on_hold_timeout(registry, phase);
            }
            // hold снят (или снимется); phase_elapsed сбросится при переходе.
            if (is_held(registry, phase)) {
                // На всякий случай: если выбор не снят, не зацикливаемся.
                state.phase_elapsed_sec = 0.0;
            }
            advance_phase(registry, state, events, on_phase_entered);
        } else {
            const double remaining = kAutoPhaseDurationSec - state.phase_elapsed_sec;
            if (budget < remaining) {
                state.phase_elapsed_sec += budget;
                return;
            }
            budget -= remaining;
            advance_phase(registry, state, events, on_phase_entered);
        }
    }
}

IntentResult set_phase_ready(entt::registry &registry, int32_t player_id, bool ready) {
    IntentResult result;
    result.type = kIntentPhaseReady;

    const entt::entity match = match_entity(registry);
    const auto &state = registry.get<const ecs::MatchState>(match);
    if (!phase_is_decision(static_cast<Phase>(state.phase))) {
        result.reason = kRejectNotDecisionPhase;
        return result;
    }

    auto view = registry.view<const ecs::PlayerInfo, ecs::PhaseReady>();
    for (auto [entity, info, ready_flag] : view.each()) {
        if (info.id == player_id) {
            ready_flag.ready = ready;
            result.accepted = true;
            result.payload[kPayloadReady] = to_string_bool(ready);
            return result;
        }
    }
    result.reason = kRejectUnknownPlayer;
    return result;
}

TurnSnapshot make_snapshot(const entt::registry &registry) {
    TurnSnapshot snapshot;
    const entt::entity match = match_entity(registry);
    const auto &state = registry.get<const ecs::MatchState>(match);
    const auto &timers = registry.get<const ecs::MatchTimers>(match);

    snapshot.active = true;
    snapshot.turn = state.turn;
    snapshot.phase = state.phase;
    const Phase phase = static_cast<Phase>(state.phase);
    snapshot.is_decision = phase_is_decision(phase);
    if (snapshot.is_decision) {
        const double limit = phase_time_limit(timers, phase);
        snapshot.timer_remaining_sec = limit > 0.0 ? std::max(limit - state.phase_elapsed_sec, 0.0) : -1.0;
    }

    const auto *dice_catalog = registry.try_get<const ecs::DiceCatalog>(match);
    const auto *disaster_catalog = registry.try_get<const ecs::DisasterCatalog>(match);
    snapshot.danger_max = disaster_catalog != nullptr ? disaster_catalog->danger_max : 0;
    snapshot.finished = state.finished;
    snapshot.winner_team = state.winner_team;
    auto view = registry.view<const ecs::PlayerInfo, const ecs::PhaseReady, const ecs::Resources>();
    for (auto [entity, info, ready, res] : view.each()) {
        PlayerSnapshot player;
        player.id = info.id;
        player.team = info.team;
        player.is_ai = info.is_ai;
        player.alive = info.alive;
        player.ready = ready.ready;
        player.wood = res.wood;
        player.stone = res.stone;
        player.food = res.food;
        player.gold = res.gold;
        player.hammers = res.hammers;
        player.swords = res.swords;
        player.culture = res.culture;
        const auto *meter = registry.try_get<const ecs::DangerMeter>(entity);
        player.danger = meter != nullptr ? meter->value : 0;
        player.clairvoyance_tier = systems::clairvoyance_tier(registry, entity);
        // Исследования: доступность (активный университет) и изученное (SPEC §8).
        player.research_available = systems::has_active_university(registry, info.id);
        const auto *research = registry.try_get<const ecs::PlayerResearch>(entity);
        if (research != nullptr) {
            for (const std::string &effect : research->unlocked) {
                player.research.push_back(effect);
            }
        }
        // Эффективные капы с учётом складов и динамика сетки (леса, POI).
        const auto *grid = registry.try_get<const ecs::PlayerGrid>(entity);
        if (grid != nullptr) {
            const ecs::Resources caps = systems::effective_caps(registry, match, entity);
            player.cap_food = caps.food;
            player.cap_wood = caps.wood;
            player.cap_stone = caps.stone;
            for (size_t i = 0; i < grid->types.size(); ++i) {
                if (grid->types[i] == static_cast<int32_t>(gen::CellType::Scaffold)) {
                    player.scaffolds.push_back({static_cast<int32_t>(i % grid->cells_x),
                            static_cast<int32_t>(i / grid->cells_x)});
                }
            }
            for (const ecs::PlayerPoi &poi : grid->pois) {
                player.pois.push_back({poi.cell_x, poi.cell_z, poi.kind, poi.amount});
            }
        }
        const auto *dice = registry.try_get<const ecs::PlayerDice>(entity);
        if (dice != nullptr && dice_catalog != nullptr) {
            player.rerolls_left = dice->rerolls_left;
            for (const ecs::DieState &die : dice->dice) {
                const ecs::DiceDef &def = dice_catalog->defs[die.type_index];
                DieSnapshot entry;
                entry.type = def.id;
                entry.face = die.face;
                entry.food_bonus = die.food_bonus;
                if (die.face >= 0) {
                    const ecs::DieFace &face = def.faces[die.face];
                    entry.crosses = face.crosses;
                    entry.wood = face.gain.wood;
                    entry.stone = face.gain.stone;
                    entry.food = face.gain.food;
                    entry.gold = face.gain.gold;
                    entry.hammers = face.gain.hammers;
                    entry.swords = face.gain.swords;
                    entry.culture = face.gain.culture;
                }
                player.dice.push_back(std::move(entry));
            }
        }
        snapshot.players.push_back(player);
    }
    // Порядок итерации EnTT не гарантирован — UI ждёт стабильный список.
    std::sort(snapshot.players.begin(), snapshot.players.end(),
            [](const PlayerSnapshot &a, const PlayerSnapshot &b) { return a.id < b.id; });

    const auto *catalog = registry.try_get<const ecs::BuildingCatalog>(match);
    if (catalog != nullptr) {
        auto buildings = registry.view<const ecs::Building>();
        for (auto [entity, building] : buildings.each()) {
            BuildingSnapshot entry;
            entry.id = static_cast<uint32_t>(entity);
            entry.player_id = building.player_id;
            entry.type = catalog->defs[building.def_index].id;
            entry.cell_x = building.cell_x;
            entry.cell_z = building.cell_z;
            entry.size_x = building.size_x;
            entry.size_z = building.size_z;
            entry.status = building.status;
            entry.hp = building.hp;
            entry.wonder_stage = building.wonder_stage;
            entry.wonder_stages = catalog->defs[building.def_index].wonder_stages;
            snapshot.buildings.push_back(std::move(entry));
        }
        std::sort(snapshot.buildings.begin(), snapshot.buildings.end(),
                [](const BuildingSnapshot &a, const BuildingSnapshot &b) { return a.id < b.id; });
    }
    return snapshot;
}

} // namespace dicecore::turn
