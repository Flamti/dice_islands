#pragma once

#include <entt/entt.hpp>

// Экономика (SPEC §3, §4 фаза Ресурсов): конвертация зафиксированных граней
// в ресурсы, капы хранения со сжиганием излишков.

namespace dicecore::systems {

// Фаза Ресурсов: начислить выигрыши граней, применить капы, очистить пул.
void apply_dice_income(entt::registry &registry);

} // namespace dicecore::systems
