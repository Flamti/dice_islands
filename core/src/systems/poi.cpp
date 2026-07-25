#include "systems/poi.hpp"

#include "ecs/components.hpp"
#include "ecs/components_building.hpp"
#include "gen/island_gen.hpp"

#include <cstdlib>
#include <string>

namespace dicecore::systems {

namespace {

// Добыча стоит 1 молоток (SPEC §3: 1 молоток = 1 действие на карте).
constexpr int32_t kHammersPerHarvest = 1;

entt::entity match_entity(const entt::registry &registry) {
    return registry.view<const ecs::MatchState>().front();
}

entt::entity find_player(const entt::registry &registry, int32_t player_id) {
    for (auto [entity, info] : registry.view<const ecs::PlayerInfo>().each()) {
        if (info.id == player_id) {
            return entity;
        }
    }
    return entt::null;
}

int32_t parse_cell(const Intent &intent, const char *key) {
    const auto it = intent.payload.find(key);
    if (it == intent.payload.end() || it->second.empty()) {
        return -1;
    }
    return static_cast<int32_t>(std::strtol(it->second.c_str(), nullptr, 10));
}

int32_t *resource_field(ecs::Resources &resources, const std::string &kind) {
    if (kind == gen::kPoiKindStone) return &resources.stone;
    if (kind == gen::kPoiKindWood) return &resources.wood;
    return nullptr;
}

} // namespace

IntentResult handle_harvest(entt::registry &registry, int32_t player_id, const Intent &intent) {
    IntentResult result;
    result.type = intent.type;

    const auto &state = registry.get<const ecs::MatchState>(match_entity(registry));
    if (state.phase != static_cast<int32_t>(Phase::Development)) {
        result.reason = kRejectWrongPhase;
        return result;
    }
    const entt::entity player_entity = find_player(registry, player_id);
    if (player_entity == entt::null) {
        result.reason = kRejectUnknownPlayer;
        return result;
    }
    auto *grid = registry.try_get<ecs::PlayerGrid>(player_entity);
    if (grid == nullptr) {
        result.reason = kRejectNoPoiHere;
        return result;
    }

    const int32_t cell_x = parse_cell(intent, kPayloadCellX);
    const int32_t cell_z = parse_cell(intent, kPayloadCellZ);
    if (!grid->in_bounds(cell_x, cell_z)) {
        result.reason = kRejectOutOfBounds;
        return result;
    }
    // Ищем POI на клетке (POI одноклеточные).
    int32_t poi_index = -1;
    for (size_t i = 0; i < grid->pois.size(); ++i) {
        if (grid->pois[i].cell_x == cell_x && grid->pois[i].cell_z == cell_z) {
            poi_index = static_cast<int32_t>(i);
            break;
        }
    }
    if (poi_index < 0) {
        result.reason = kRejectNoPoiHere;
        return result;
    }

    auto &resources = registry.get<ecs::Resources>(player_entity);
    if (resources.hammers < kHammersPerHarvest) {
        result.reason = kRejectNotEnoughResources;
        return result;
    }

    const ecs::PlayerPoi poi = grid->pois[poi_index];
    int32_t *field = resource_field(resources, poi.kind);
    if (field == nullptr) {
        result.reason = kRejectNoPoiHere; // неизвестный вид POI
        return result;
    }
    resources.hammers -= kHammersPerHarvest;
    *field += poi.amount;
    // Клетка освобождается и становится пригодной под застройку (SPEC §11.2).
    grid->types[grid->index_of(cell_x, cell_z)] = static_cast<int32_t>(gen::CellType::Buildable);
    grid->pois.erase(grid->pois.begin() + poi_index);

    result.accepted = true;
    result.payload["kind"] = poi.kind;
    result.payload["amount"] = std::to_string(poi.amount);
    result.payload[kPayloadCellX] = std::to_string(cell_x);
    result.payload[kPayloadCellZ] = std::to_string(cell_z);
    return result;
}

} // namespace dicecore::systems
