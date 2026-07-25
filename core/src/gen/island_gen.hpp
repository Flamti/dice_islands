#pragma once

#include "gen/marching_cubes.hpp"

#include <cstdint>
#include <string>
#include <vector>

// Генератор острова (SPEC §11.1): сид + параметры -> меш и данные сетки.
// Полностью независим от Godot; детерминирован по сиду.

namespace dicecore::gen {

// Типы клеток строительной сетки (SPEC §11.3).
enum class CellType : int32_t {
    Void = 0, // нет поверхности
    Buildable = 1, // пригодна для застройки
    Blocked = 2, // рельеф или POI
    Scaffold = 3, // клетки-леса (появятся с этапа 8)
};

// Виды POI на клетках (SPEC §11.2).
inline constexpr const char *kPoiKindStone = "stone";
inline constexpr const char *kPoiKindWood = "wood";

struct PoiSpot {
    int32_t cell_x = 0;
    int32_t cell_z = 0;
    std::string kind;
    int32_t amount = 0;
};

struct GridData {
    int32_t cells_x = 0;
    int32_t cells_z = 0;
    float cell_size = 0.0f;
    float origin_x = 0.0f; // мировой X угла клетки (0, 0)
    float origin_z = 0.0f;
    std::vector<int32_t> types; // cells_x * cells_z, row-major по z
    std::vector<float> heights; // высота поверхности; 0 для Void
    std::vector<PoiSpot> poi;

    int32_t type_at(int32_t cx, int32_t cz) const {
        return types[static_cast<size_t>(cz) * cells_x + cx];
    }
};

// Параметры генератора: числа читаются из data/generator.json [баланс].
struct GeneratorParams {
    float cell_size = 2.0f;
    int32_t grid_cells = 24;
    int32_t voxels_per_cell = 2;
    float island_radius = 16.2f;
    float top_height = 5.0f;
    float top_amplitude = 1.5f;
    float top_noise_scale = 0.06f;
    float bottom_depth = 16.0f;
    float bottom_amplitude = 4.0f;
    float bottom_noise_scale = 0.09f;
    float edge_noise_amplitude = 3.0f;
    float edge_noise_scale = 0.12f;
    float carve_amplitude = 2.0f;
    float carve_noise_scale = 0.16f;
    float flat_threshold = 0.8f;
    int32_t poi_count = 10;
    int32_t poi_min_amount = 2;
    int32_t poi_max_amount = 4;
    float uv_tile_world_size = 4.0f;
};

// Разбор параметров из JSON-текста; при ошибке — false и описание в error.
bool params_from_json(const std::string &json_text, GeneratorParams &params, std::string &error);

struct IslandData {
    Mesh mesh; // сшитый меш в мировых координатах, центр острова в (0, 0)
    std::vector<float> uvs; // 2 float на вершину меша (атлас 2x2, зеркальная укладка)
    GridData grid;
};

// Клетка сетки (для списка вырезанных обрушением клеток).
struct GridCell {
    int32_t cell_x = 0;
    int32_t cell_z = 0;
};

IslandData generate_island(uint64_t seed, const GeneratorParams &params);

// Генерация с вырезанными клетками (деструктор ландшафта, SPEC §11.5): в
// столбцах этих клеток поверхность удаляется (Void), меш ре-полигонизуется.
IslandData generate_island_carved(uint64_t seed, const GeneratorParams &params,
        const std::vector<GridCell> &carved);

// Только данные сетки, без полигонизации: быстрый путь для хоста (валидация
// строительства). Гарантированно совпадает с generate_island(...).grid.
GridData generate_grid(uint64_t seed, const GeneratorParams &params);

} // namespace dicecore::gen
