#pragma once

#include "dicecore/core.hpp"
#include "ecs/components_research.hpp"

#include <entt/entt.hpp>

// Исследования (SPEC §8): покупка узлов за культуру при активном Университете
// в фазе Развития; узлы ветки открываются последовательно. Трата культуры
// уменьшает и баланс, и счёт культурной победы (культура — один счётчик, §3).

namespace dicecore::systems {

// Разбор data/research.json. При ошибке — false и текст в error.
bool parse_research_json(const std::string &json_text, ecs::ResearchCatalog &catalog,
        std::string &error);

// Намерение «исследовать»: payload {node}.
IntentResult handle_research(entt::registry &registry, int32_t player_id, const Intent &intent);

// Активен ли Университет игрока (доступность экрана исследований).
bool has_active_university(const entt::registry &registry, int32_t player_id);

// Изучил ли игрок эффект (для систем, применяющих узлы).
bool player_has_research(const entt::registry &registry, entt::entity player, const std::string &effect);

// Фаза Ресурсов: пассивный доход от узлов (Ремесло: +молотки/ход).
void apply_research_income(entt::registry &registry);

} // namespace dicecore::systems
