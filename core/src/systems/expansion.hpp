#pragma once

#include "dicecore/core.hpp"

#include <entt/entt.hpp>

#include <vector>

// Расширение острова строительной платформой (SPEC §11.4). В фазу 0 хода,
// следующего за постройкой, активная платформа достраивает клетки-леса
// (Scaffold) в окружности вокруг себя — только там, где нет поверхности,
// не более max_cells. Клетки-леса строительно эквивалентны обычным.

namespace dicecore::systems {

// Фаза 0: платформы, ещё не расширявшиеся, добавляют клетки-леса. Порождённые
// события расширения добавляются в events.
void expand_platforms(entt::registry &registry, std::vector<Event> &events);

} // namespace dicecore::systems
