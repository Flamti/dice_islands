#pragma once

#include "dicecore/core.hpp"

#include <entt/entt.hpp>

#include <vector>

// Turn State Machine: фазы 0–9 по SPEC §4, барьеры PhaseReady и таймеры
// фаз-решений (SPEC §12.2). Работает поверх реестра EnTT, ничего не хранит.

namespace dicecore::turn {

// Создаёт сущность партии и игроков, входит в ход 1 / фазу 0.
void start_match(entt::registry &registry, const MatchConfig &config, std::vector<Event> &events);

// Продвигает стейт-машину на dt секунд, пишет события переходов.
void tick(entt::registry &registry, double dt, std::vector<Event> &events);

// Барьер фазы: отметка готовности игрока в текущей фазе-решении.
IntentResult set_phase_ready(entt::registry &registry, int32_t player_id, bool ready);

// Снимок текущего хода для адаптера/UI.
TurnSnapshot make_snapshot(const entt::registry &registry);

} // namespace dicecore::turn
