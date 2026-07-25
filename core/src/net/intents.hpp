#pragma once

#include "dicecore/core.hpp"

#include <entt/entt.hpp>

// Протокол намерений: маршрутизация и валидация команд игроков (SPEC §12.1).
// Единственная точка, где строковые типы намерений превращаются в вызовы систем.

namespace dicecore::net {

// Разбор булева значения из полезной нагрузки; отсутствие ключа — fallback.
bool parse_bool(const std::map<std::string, std::string> &payload, const char *key, bool fallback);

// Диспетчеризация намерения игрока в активной партии.
IntentResult dispatch_intent(entt::registry &registry, int32_t player_id, const Intent &intent);

} // namespace dicecore::net
