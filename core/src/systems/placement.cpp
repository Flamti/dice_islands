#include "systems/placement.hpp"

#include "gen/island_gen.hpp"
#include "save/json.hpp"

#include <cstdlib>

namespace dicecore::systems {

namespace {

// Доля возврата ресурсов при сносе (SPEC §5, [баланс]; округление вниз).
constexpr int32_t kDemolishRefundPercent = 50;
// Любое действие строительства/сноса стоит 1 молоток (SPEC §3).
constexpr int32_t kHammersPerAction = 1;

entt::entity match_entity(const entt::registry &registry) {
    return registry.view<const ecs::MatchState>().front();
}

// Сущность игрока по ID; entt::null, если нет.
entt::entity find_player(const entt::registry &registry, int32_t player_id) {
    for (auto [entity, info] : registry.view<const ecs::PlayerInfo>().each()) {
        if (info.id == player_id) {
            return entity;
        }
    }
    return entt::null;
}

bool in_development_phase(const entt::registry &registry) {
    const auto &state = registry.get<const ecs::MatchState>(match_entity(registry));
    return state.phase == static_cast<int32_t>(Phase::Development);
}

int32_t parse_cell(const Intent &intent, const char *key) {
    const auto it = intent.payload.find(key);
    if (it == intent.payload.end() || it->second.empty()) {
        return -1;
    }
    return static_cast<int32_t>(std::strtol(it->second.c_str(), nullptr, 10));
}

// Прямоугольник full free+buildable? Возвращает код отказа либо nullptr.
const char *check_footprint(const ecs::PlayerGrid &grid, int32_t cx, int32_t cz,
        int32_t size_x, int32_t size_z) {
    if (cx < 0 || cz < 0 || !grid.in_bounds(cx, cz) ||
            !grid.in_bounds(cx + size_x - 1, cz + size_z - 1)) {
        return kRejectOutOfBounds;
    }
    for (int32_t dz = 0; dz < size_z; ++dz) {
        for (int32_t dx = 0; dx < size_x; ++dx) {
            const size_t index = grid.index_of(cx + dx, cz + dz);
            if (grid.types[index] != static_cast<int32_t>(gen::CellType::Buildable)) {
                return kRejectCellNotBuildable;
            }
            if (grid.occupancy[index] != ecs::kCellFree) {
                return kRejectCellOccupied;
            }
        }
    }
    return nullptr;
}

void fill_occupancy(ecs::PlayerGrid &grid, const ecs::Building &building, uint32_t value) {
    for (int32_t dz = 0; dz < building.size_z; ++dz) {
        for (int32_t dx = 0; dx < building.size_x; ++dx) {
            grid.occupancy[grid.index_of(building.cell_x + dx, building.cell_z + dz)] = value;
        }
    }
}

entt::entity spawn_building(entt::registry &registry, entt::entity player_entity,
        int32_t player_id, int32_t def_index, const ecs::BuildingDef &def, int32_t cx, int32_t cz,
        BuildingStatus status) {
    const entt::entity entity = registry.create();
    auto &building = registry.emplace<ecs::Building>(entity);
    building.player_id = player_id;
    building.def_index = def_index;
    building.cell_x = cx;
    building.cell_z = cz;
    building.size_x = def.size_x;
    building.size_z = def.size_z;
    building.hp = def.hp;
    building.status = static_cast<int32_t>(status);
    fill_occupancy(registry.get<ecs::PlayerGrid>(player_entity), building,
            static_cast<uint32_t>(entity));
    return entity;
}

} // namespace

bool parse_buildings_json(const std::string &json_text, ecs::BuildingCatalog &catalog,
        std::string &error) {
    save::JsonValue root;
    if (!save::parse_json(json_text, root, error)) {
        return false;
    }
    if (!root.is_object() || !root.has("buildings")) {
        error = "ожидается объект с ключом buildings";
        return false;
    }

    const save::JsonValue &starting = root.as_object().find("starting_resources") != root.as_object().end()
            ? root.as_object().at("starting_resources")
            : save::JsonValue();
    catalog.starting_resources.wood = starting.int_at("wood", 0);
    catalog.starting_resources.stone = starting.int_at("stone", 0);
    catalog.starting_resources.gold = starting.int_at("gold", 0);
    catalog.starting_resources.food = starting.int_at("food", 0);
    catalog.starting_resources.hammers = starting.int_at("hammers", 0);

    for (const auto &[id, def_json] : root.as_object().at("buildings").as_object()) {
        ecs::BuildingDef def;
        def.id = id;
        def.name = def_json.as_object().count("name") ? def_json.as_object().at("name").as_string() : id;
        def.size_x = def_json.int_at("size_x", 1);
        def.size_z = def_json.int_at("size_z", 1);
        def.hp = def_json.int_at("hp", 1);
        def.preplaced = def_json.as_object().count("preplaced") &&
                def_json.as_object().at("preplaced").as_bool();
        def.dice = def_json.as_object().count("dice") ? def_json.as_object().at("dice").as_string() : "";
        if (def_json.has("cost")) {
            const save::JsonValue &cost = def_json.as_object().at("cost");
            def.cost_wood = cost.int_at("wood", 0);
            def.cost_stone = cost.int_at("stone", 0);
            def.cost_gold = cost.int_at("gold", 0);
        }
        if (def.size_x <= 0 || def.size_z <= 0 || def.hp <= 0) {
            error = "недопустимые габариты/HP здания " + id;
            return false;
        }
        catalog.defs.push_back(std::move(def));
    }
    if (catalog.defs.empty()) {
        error = "пустой набор зданий";
        return false;
    }
    // Каталог в детерминированном порядке (JsonObject — std::map, уже сортирован).
    return true;
}

int32_t find_building_def(const ecs::BuildingCatalog &catalog, const std::string &id) {
    for (size_t i = 0; i < catalog.defs.size(); ++i) {
        if (catalog.defs[i].id == id) {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

bool preplace_castle(entt::registry &registry, entt::entity player_entity,
        const ecs::BuildingCatalog &catalog, std::string &error) {
    const int32_t castle_index = find_building_def(catalog, "castle");
    if (castle_index < 0) {
        error = kErrorBadBuildingsConfig;
        return false;
    }
    const ecs::BuildingDef &castle = catalog.defs[castle_index];
    const auto &info = registry.get<const ecs::PlayerInfo>(player_entity);
    const auto &grid = registry.get<const ecs::PlayerGrid>(player_entity);

    // Ближайшее к центру острова валидное место (детерминированный обход).
    const float center_x = grid.cells_x / 2.0f;
    const float center_z = grid.cells_z / 2.0f;
    int32_t best_x = -1;
    int32_t best_z = -1;
    float best_dist = 1e9f;
    for (int32_t cz = 0; cz + castle.size_z <= grid.cells_z; ++cz) {
        for (int32_t cx = 0; cx + castle.size_x <= grid.cells_x; ++cx) {
            if (check_footprint(grid, cx, cz, castle.size_x, castle.size_z) != nullptr) {
                continue;
            }
            const float dx = cx + castle.size_x / 2.0f - center_x;
            const float dz = cz + castle.size_z / 2.0f - center_z;
            const float dist = dx * dx + dz * dz;
            if (dist < best_dist) {
                best_dist = dist;
                best_x = cx;
                best_z = cz;
            }
        }
    }
    if (best_x < 0) {
        error = kErrorNoCastleSpot;
        return false;
    }
    spawn_building(registry, player_entity, info.id, castle_index, castle, best_x, best_z,
            BuildingStatus::Active);
    return true;
}

IntentResult handle_build(entt::registry &registry, int32_t player_id, const Intent &intent) {
    IntentResult result;
    result.type = intent.type;

    if (!in_development_phase(registry)) {
        result.reason = kRejectWrongPhase;
        return result;
    }
    const entt::entity player_entity = find_player(registry, player_id);
    if (player_entity == entt::null) {
        result.reason = kRejectUnknownPlayer;
        return result;
    }

    // Партия могла стартовать без каталога зданий (конфиг не передан).
    const auto *catalog = registry.try_get<const ecs::BuildingCatalog>(match_entity(registry));
    const auto *grid_check = registry.try_get<const ecs::PlayerGrid>(player_entity);
    if (catalog == nullptr || grid_check == nullptr) {
        result.reason = kRejectUnknownBuilding;
        return result;
    }
    const auto building_it = intent.payload.find(kPayloadBuilding);
    const int32_t def_index =
            building_it != intent.payload.end() ? find_building_def(*catalog, building_it->second) : -1;
    if (def_index < 0) {
        result.reason = kRejectUnknownBuilding;
        return result;
    }
    const ecs::BuildingDef &def = catalog->defs[def_index];
    if (def.preplaced) {
        result.reason = kRejectNotConstructible;
        return result;
    }

    const int32_t cell_x = parse_cell(intent, kPayloadCellX);
    const int32_t cell_z = parse_cell(intent, kPayloadCellZ);
    const auto &grid = registry.get<const ecs::PlayerGrid>(player_entity);
    if (const char *reject = check_footprint(grid, cell_x, cell_z, def.size_x, def.size_z)) {
        result.reason = reject;
        return result;
    }

    auto &resources = registry.get<ecs::Resources>(player_entity);
    if (resources.wood < def.cost_wood || resources.stone < def.cost_stone ||
            resources.gold < def.cost_gold || resources.hammers < kHammersPerAction) {
        result.reason = kRejectNotEnoughResources;
        return result;
    }
    resources.wood -= def.cost_wood;
    resources.stone -= def.cost_stone;
    resources.gold -= def.cost_gold;
    resources.hammers -= kHammersPerAction;

    const entt::entity entity = spawn_building(registry, player_entity, player_id, def_index, def,
            cell_x, cell_z, BuildingStatus::UnderConstruction);

    result.accepted = true;
    result.payload[kPayloadBuilding] = def.id;
    result.payload[kPayloadCellX] = std::to_string(cell_x);
    result.payload[kPayloadCellZ] = std::to_string(cell_z);
    result.payload["id"] = std::to_string(static_cast<uint32_t>(entity));
    return result;
}

IntentResult handle_demolish(entt::registry &registry, int32_t player_id, const Intent &intent) {
    IntentResult result;
    result.type = intent.type;

    if (!in_development_phase(registry)) {
        result.reason = kRejectWrongPhase;
        return result;
    }
    const entt::entity player_entity = find_player(registry, player_id);
    if (player_entity == entt::null) {
        result.reason = kRejectUnknownPlayer;
        return result;
    }

    auto *grid_ptr = registry.try_get<ecs::PlayerGrid>(player_entity);
    if (grid_ptr == nullptr) {
        result.reason = kRejectNoBuildingHere;
        return result;
    }
    ecs::PlayerGrid &grid = *grid_ptr;
    const int32_t cell_x = parse_cell(intent, kPayloadCellX);
    const int32_t cell_z = parse_cell(intent, kPayloadCellZ);
    if (!grid.in_bounds(cell_x, cell_z)) {
        result.reason = kRejectOutOfBounds;
        return result;
    }
    const uint32_t occupant = grid.occupancy[grid.index_of(cell_x, cell_z)];
    if (occupant == ecs::kCellFree) {
        result.reason = kRejectNoBuildingHere;
        return result;
    }
    const entt::entity building_entity = static_cast<entt::entity>(occupant);
    const ecs::Building building = registry.get<ecs::Building>(building_entity);
    if (building.player_id != player_id) {
        result.reason = kRejectNotYourBuilding;
        return result;
    }
    const auto &catalog = registry.get<const ecs::BuildingCatalog>(match_entity(registry));
    const ecs::BuildingDef &def = catalog.defs[building.def_index];
    if (def.preplaced) {
        result.reason = kRejectCastleProtected;
        return result;
    }
    auto &resources = registry.get<ecs::Resources>(player_entity);
    if (resources.hammers < kHammersPerAction) {
        result.reason = kRejectNotEnoughResources;
        return result;
    }

    resources.hammers -= kHammersPerAction;
    resources.wood += def.cost_wood * kDemolishRefundPercent / 100;
    resources.stone += def.cost_stone * kDemolishRefundPercent / 100;
    fill_occupancy(grid, building, ecs::kCellFree);
    registry.destroy(building_entity);

    result.accepted = true;
    result.payload[kPayloadBuilding] = def.id;
    result.payload[kPayloadCellX] = std::to_string(cell_x);
    result.payload[kPayloadCellZ] = std::to_string(cell_z);
    return result;
}

void activate_constructions(entt::registry &registry) {
    for (auto [entity, building] : registry.view<ecs::Building>().each()) {
        if (building.status == static_cast<int32_t>(BuildingStatus::UnderConstruction)) {
            building.status = static_cast<int32_t>(BuildingStatus::Active);
        }
    }
}

} // namespace dicecore::systems
