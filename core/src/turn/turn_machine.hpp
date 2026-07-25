#pragma once

#include "dicecore/core.hpp"

#include <entt/entt.hpp>

#include <functional>
#include <vector>

// Turn State Machine: фазы 0–9 по SPEC §4, барьеры PhaseReady и таймеры
// фаз-решений (SPEC §12.2). Работает поверх реестра EnTT, ничего не хранит.

namespace dicecore::turn {

// Эффект входа в фазу: вызывается сразу при переходе, до продвижения дальше
// внутри того же тика (сбор кубиков, броски, конвертация и т.п.).
using PhaseHook = std::function<void(entt::registry &, Phase)>;
// Держится ли текущая авто-фаза (мини-решение выбора цели, SPEC §4 фаза 5).
using HoldPredicate = std::function<bool(const entt::registry &, Phase)>;
// Таймаут держащейся фазы: разрешить отложенное (случайный выбор цели).
using HoldTimeout = std::function<void(entt::registry &, Phase)>;

// Создаёт сущность партии и игроков, входит в ход 1 / фазу 0.
void start_match(entt::registry &registry, const MatchConfig &config, std::vector<Event> &events);

// Продвигает стейт-машину на dt секунд, пишет события переходов. Авто-фаза с
// активным hold ведёт себя как решение с таймером kDarkChoiceTimeoutSec.
void tick(entt::registry &registry, double dt, std::vector<Event> &events,
        const PhaseHook &on_phase_entered = {}, const HoldPredicate &is_held = {},
        const HoldTimeout &on_hold_timeout = {});

// Барьер фазы: отметка готовности игрока в текущей фазе-решении.
IntentResult set_phase_ready(entt::registry &registry, int32_t player_id, bool ready);

// Снимок текущего хода для адаптера/UI.
TurnSnapshot make_snapshot(const entt::registry &registry);

} // namespace dicecore::turn
