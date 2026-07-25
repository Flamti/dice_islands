#pragma once

#include <entt/entt.hpp>

// Апкип еды (SPEC §4 фаза 1): каждое здание с жителем требует 1 еду.
// Нехватка -> недокормленные здания получают Starving в случайном порядке,
// детерминированном RNG хоста (решение геймдизайнера от 25.07.2026).
// Starving-здание не даёт кубик в фазе Дохода; других последствий нет.
// Статус пересчитывается каждый ход: при достатке еды снимается.

namespace dicecore::systems {

void apply_food_upkeep(entt::registry &registry);

} // namespace dicecore::systems
