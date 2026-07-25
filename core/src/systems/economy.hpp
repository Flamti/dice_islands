#pragma once

#include "ecs/components.hpp"

#include <entt/entt.hpp>

// Экономика (SPEC §3, §4 фаза Ресурсов): конвертация зафиксированных граней
// в ресурсы, капы хранения со сжиганием излишков.

namespace dicecore::systems {

// Эффективные капы игрока: базовые (dice.json) + бонусы активных складов
// (SPEC §5). Поля с капом 0 — без лимита.
ecs::Resources effective_caps(const entt::registry &registry, entt::entity match,
        entt::entity player);

// Фаза Ресурсов: начислить выигрыши граней (с бонусом мельницы), применить капы.
void apply_dice_income(entt::registry &registry);

} // namespace dicecore::systems
