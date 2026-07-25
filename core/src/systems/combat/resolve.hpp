#pragma once

#include "dicecore/core.hpp"

#include <entt/entt.hpp>

#include <vector>

// Фаза Боя (SPEC §4 фаза 8, §9.3): все запланированные рейды хода на каждый
// остров разрешаются одной симуляцией. Исход применяется к стейту (разрушенные
// здания, гарнизон, награда), лог боя складывается в logs для стрима клиентам.

namespace dicecore::systems::combat {

void resolve_combat_phase(entt::registry &registry, std::vector<Event> &events,
        std::vector<BattleLog> &logs);

} // namespace dicecore::systems::combat
