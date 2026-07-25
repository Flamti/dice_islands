#pragma once

#include "dicecore/core.hpp"

#include <entt/entt.hpp>

#include <vector>

// Победа и поражение (SPEC §10). Фаза Проверок (фаза 9): выбывание игроков
// (замок разрушен), затем определение победившей команды по приоритету
// Чудо -> Военная -> Культурная. При победе партия помечается завершённой.

namespace dicecore::systems {

void resolve_checks_phase(entt::registry &registry, std::vector<Event> &events);

} // namespace dicecore::systems
