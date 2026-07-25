#pragma once

#include "dicecore/core.hpp"

#include <entt/entt.hpp>

// Добыча POI (SPEC §11.2): в фазу Развития за 1 молоток игрок собирает
// ресурс с POI-клетки своего острова (камень/дерево, объём задан генератором);
// клетка освобождается и становится пригодной для застройки.

namespace dicecore::systems {

// Намерение «добыть»: payload {cell_x, cell_z}.
IntentResult handle_harvest(entt::registry &registry, int32_t player_id, const Intent &intent);

} // namespace dicecore::systems
