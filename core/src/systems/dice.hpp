#pragma once

#include "dicecore/core.hpp"
#include "ecs/components.hpp"

#include <entt/entt.hpp>

// Система кубиков (SPEC §6): сбор пула в фазу Дохода, броски и рероллы в фазу
// Бросков, блокиратор граней с крестами. Конвертация — в systems/economy.

namespace dicecore::systems {

// Разбор data/dice.json в каталог. При ошибке — false и описание в error.
bool parse_dice_json(const std::string &json_text, ecs::DiceCatalog &catalog, std::string &error);

// Фаза Дохода: каждое активное здание с кубиком кладёт кубик в пул владельца.
void collect_income(entt::registry &registry);

// Вход в фазу Бросков: первый бросок всех кубиков всех игроков (RNG хоста).
void roll_initial(entt::registry &registry);

// Намерение «перебросить»: payload {dice: "0,2,5"}. До kMaxRerolls раз,
// грани с крестами недоступны для переброса (SPEC §6).
IntentResult handle_reroll(entt::registry &registry, int32_t player_id, const Intent &intent);

} // namespace dicecore::systems
