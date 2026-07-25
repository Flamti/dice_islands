#pragma once

#include "dicecore/core.hpp"
#include "ecs/components_disaster.hpp"

#include <entt/entt.hpp>

#include <vector>

// Шкала опасности и катастрофы (SPEC §7, §11.5). Полный пул MVP: кража, молния,
// болезнь, саботаж, шторм, нашествие пиратов, метеорит, обрушение края.
// Таргетинг Opponent; Тёмная магия даёт выбор цели из 2 конкурентов.

namespace dicecore::systems {

// Разбор data/disasters.json в каталог. При ошибке — false и текст в error.
bool parse_disasters_json(const std::string &json_text, ecs::DisasterCatalog &catalog,
        std::string &error);

// Фаза Катастроф: начисление крестов, распространение болезни, срабатывание
// катастроф, очистка пула. Порождённые события добавляются в events.
void resolve_danger_phase(entt::registry &registry, std::vector<Event> &events);

// Держится ли фаза Катастроф (ждёт выбор цели Тёмной магией у людей).
bool disasters_held(const entt::registry &registry);

// Истёк таймер выбора цели: применить все оставшиеся выборы случайно.
void resolve_pending_choices_random(entt::registry &registry, std::vector<Event> &events);

// Фаза 0: снятие Disabled с истёкшим сроком (SPEC §7.2 «Disabled на 1 ход»).
void restore_disabled(entt::registry &registry);

// Предпросмотр тяжести следующей катастрофы (Ясновидение); 0 — недоступно.
int32_t clairvoyance_tier(const entt::registry &registry, entt::entity player);

// Намерения этапа 12.
IntentResult handle_target_pick(entt::registry &registry, int32_t player_id, const Intent &intent);
IntentResult handle_cure(entt::registry &registry, int32_t player_id, const Intent &intent);
IntentResult handle_storm_rite(entt::registry &registry, int32_t player_id, const Intent &intent);

} // namespace dicecore::systems
