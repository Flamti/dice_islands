#pragma once

#include "dicecore/core.hpp"

#include <entt/entt.hpp>

// Рейды игроков (SPEC §9.4). Фаза Набегов (фаза 7): игрок с активным Портом
// отправляет часть гарнизона на здание/замок игрока чужой команды. Рейд
// ставится в очередь PendingRaids и разрешается общим боем в фазе Боя.

namespace dicecore::systems {

// Намерение «рейд»: payload {target_player, target_building, count, side}.
IntentResult handle_raid(entt::registry &registry, int32_t player_id, const Intent &intent);

} // namespace dicecore::systems
