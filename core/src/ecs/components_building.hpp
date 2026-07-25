#pragma once

#include "dicecore/core.hpp"
#include "ecs/components.hpp"

#include <cstdint>
#include <string>
#include <vector>

// Компоненты зданий и строительной сетки — только данные (ARCHITECTURE §1).

namespace dicecore::ecs {

// Описание типа здания из data/buildings.json (SPEC §5).
struct BuildingDef {
    std::string id; // машинный идентификатор ("farm")
    std::string name; // отображаемое имя
    int32_t size_x = 1;
    int32_t size_z = 1;
    int32_t hp = 1;
    int32_t cost_wood = 0;
    int32_t cost_stone = 0;
    int32_t cost_gold = 0;
    bool preplaced = false; // размещается при старте (замок), не строится
    std::string dice; // тип кубика (используется с этапа 5)
    // Эффекты (SPEC §5, §11.4):
    int32_t cap_food = 0; // склад: +к капу еды
    int32_t cap_wood = 0; // склад: +к капу дерева
    int32_t cap_stone = 0; // склад: +к капу камня
    int32_t mill_food_bonus = 0; // мельница: +еда за food-грань смежной фермы
    int32_t expansion_radius = 0; // платформа: радиус лесов (0 — не платформа)
    int32_t expansion_max_cells = 0; // платформа: предел добавленных клеток
    bool requires_edge = false; // порт: только на краю острова
    bool unlocks_research = false; // университет (экран — этап 11)
    bool unlocks_raids = false; // порт (рейды — этап 10)
    bool is_wall = false; // стена: разрушаемое препятствие в бою (SPEC §9.3)
    bool is_tower = false; // башня: стреляет в бою
    bool spawns_garrison = false; // казарма/замок: точка спавна защитников
};

// Компонент сущности партии: каталог зданий и стартовые ресурсы.
struct BuildingCatalog {
    std::vector<BuildingDef> defs;
    Resources starting_resources;
};

// Запланированный рейд, ожидающий фазы Боя (SPEC §9). Пиратский рейд ставится
// катастрофой в фазе 5; рейды игроков (этап 10) — в фазе 7.
struct PendingRaid {
    int32_t defender_player = 0;
    int32_t attacker_team = -1; // kPirateTeam = -1
    int32_t attacker_owner = -1; // игрок-отправитель; -1 — пираты
    int32_t count = 0;
    int32_t landing_side = 0; // 0=N,1=E,2=S,3=W
    int32_t target_building = -1; // ID сущности здания-цели; -1 — ближайшее
};

// Компонент сущности партии: очередь рейдов текущего хода.
struct PendingRaids {
    std::vector<PendingRaid> raids;
};

// Компонент сущности здания.
struct Building {
    int32_t player_id = 0;
    int32_t def_index = 0; // индекс в BuildingCatalog.defs
    int32_t cell_x = 0; // клетка левого-ближнего угла
    int32_t cell_z = 0;
    int32_t size_x = 1;
    int32_t size_z = 1;
    int32_t hp = 1;
    int32_t status = static_cast<int32_t>(BuildingStatus::UnderConstruction);
    bool expanded = false; // платформа уже достроила леса (SPEC §11.4)
};

// Свободная клетка в карте занятости (entt::null как uint32).
inline constexpr uint32_t kCellFree = 0xFFFFFFFFu;

// POI на клетке острова (SPEC §11.2): добывается молотком в фазу Развития.
struct PlayerPoi {
    int32_t cell_x = 0;
    int32_t cell_z = 0;
    std::string kind; // "stone" | "wood"
    int32_t amount = 0;
};

// Компонент игрока: строительная сетка его острова (типы — gen::CellType).
struct PlayerGrid {
    int32_t cells_x = 0;
    int32_t cells_z = 0;
    float cell_size = 0.0f;
    float origin_x = 0.0f;
    float origin_z = 0.0f;
    std::vector<int32_t> types;
    std::vector<float> heights;
    std::vector<uint32_t> occupancy; // entity здания на клетке либо kCellFree
    std::vector<PlayerPoi> pois; // оставшиеся POI (убираются при добыче)

    bool in_bounds(int32_t cx, int32_t cz) const {
        return cx >= 0 && cx < cells_x && cz >= 0 && cz < cells_z;
    }

    size_t index_of(int32_t cx, int32_t cz) const {
        return static_cast<size_t>(cz) * cells_x + cx;
    }
};

} // namespace dicecore::ecs
