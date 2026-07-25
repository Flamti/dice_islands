#pragma once

#include "dicecore/core.hpp"
#include "ecs/components_disaster.hpp"

#include <entt/entt.hpp>

#include <vector>

// Шкала опасности и катастрофы (SPEC §7). Фаза 5 (SPEC §4): кресты пула
// добавляются в личные шкалы, достигнутые пороги триггерят катастрофы,
// шкала сбрасывается. На этапе 7 реализована катастрофа «Воры» (тяжесть I,
// Self). Пул кубиков очищается здесь (конец жизненного цикла кубиков хода).

namespace dicecore::systems {

// Разбор data/disasters.json в каталог. При ошибке — false и текст в error.
bool parse_disasters_json(const std::string &json_text, ecs::DisasterCatalog &catalog,
        std::string &error);

// Фаза Катастроф: начисление крестов, срабатывание катастроф, очистка пула.
// Порождённые события катастроф добавляются в events.
void resolve_danger_phase(entt::registry &registry, std::vector<Event> &events);

} // namespace dicecore::systems
