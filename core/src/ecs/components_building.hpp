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
};

// Компонент сущности партии: каталог зданий и стартовые ресурсы.
struct BuildingCatalog {
    std::vector<BuildingDef> defs;
    Resources starting_resources;
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
};

// Свободная клетка в карте занятости (entt::null как uint32).
inline constexpr uint32_t kCellFree = 0xFFFFFFFFu;

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

    bool in_bounds(int32_t cx, int32_t cz) const {
        return cx >= 0 && cx < cells_x && cz >= 0 && cz < cells_z;
    }

    size_t index_of(int32_t cx, int32_t cz) const {
        return static_cast<size_t>(cz) * cells_x + cx;
    }
};

} // namespace dicecore::ecs
