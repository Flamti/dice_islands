#pragma once

#include "dicecore/core.hpp"
#include "ecs/components_building.hpp"

#include <entt/entt.hpp>

// Система размещения зданий (SPEC §5): валидация и применение BuildIntent и
// DemolishIntent на хосте, предразмещение замка, активация построенного.

namespace dicecore::systems {

// Разбор data/buildings.json в каталог. При ошибке — false и описание в error.
bool parse_buildings_json(const std::string &json_text, ecs::BuildingCatalog &catalog,
        std::string &error);

// Индекс определения по id; -1, если нет.
int32_t find_building_def(const ecs::BuildingCatalog &catalog, const std::string &id);

// Предразмещение замка: ближайшее к центру сетки свободное место (SPEC §2.2).
// При отсутствии места — false (kErrorNoCastleSpot).
bool preplace_castle(entt::registry &registry, entt::entity player_entity,
        const ecs::BuildingCatalog &catalog, std::string &error);

// Намерение «строить»: payload {building, cell_x, cell_z}.
IntentResult handle_build(entt::registry &registry, int32_t player_id, const Intent &intent);

// Намерение «снести»: payload {cell_x, cell_z}. Возврат 50% дерева/камня.
IntentResult handle_demolish(entt::registry &registry, int32_t player_id, const Intent &intent);

// Фаза 0: UnderConstruction -> Active (SPEC §4).
void activate_constructions(entt::registry &registry);

} // namespace dicecore::systems
