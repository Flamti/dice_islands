#include "gen/island_gen.hpp"

#include "math/noise.hpp"
#include "math/rng.hpp"
#include "save/json.hpp"

#include <algorithm>
#include <cmath>

namespace dicecore::gen {

namespace {

// Смещения второго-четвёртого слоёв шума по «слоевой» оси, чтобы независимые
// карты (верх/низ/край/резьба) не коррелировали при общем сиде.
constexpr float kTopNoisePlane = 3.17f;
constexpr float kBottomNoisePlane = 9.73f;
constexpr float kEdgeNoisePlane = 17.29f;
constexpr int kFbmOctaves = 3;
constexpr float kFbmGain = 0.5f;

// Степень подъёма дна к краю: даёт сужающийся книзу «парящий» силуэт.
constexpr float kBottomTaperPower = 1.7f;

// Сканирование поверхности по столбцу: шаг и точность бисекции.
constexpr float kSurfaceScanStep = 0.25f;
constexpr int kSurfaceBisectIters = 10;

// Порог «верхней» нормали при выборе сегмента атласа.
constexpr float kUpNormalThreshold = 0.4f;

// Смещение для центральных разностей градиента плотности.
constexpr float kGradientEps = 0.35f;

// Поле плотности острова: положительное — внутри породы.
struct DensityField {
    const GeneratorParams &params;
    const math::SimplexNoise &noise;

    float top_height_at(float x, float z) const {
        const float n = noise.fbm(x * params.top_noise_scale, kTopNoisePlane,
                z * params.top_noise_scale, kFbmOctaves, kFbmGain);
        return params.top_height + n * params.top_amplitude;
    }

    float bottom_height_at(float x, float z, float r) const {
        const float taper = std::pow(std::clamp(r / params.island_radius, 0.0f, 1.0f),
                kBottomTaperPower);
        const float n = noise.fbm(x * params.bottom_noise_scale, kBottomNoisePlane,
                z * params.bottom_noise_scale, kFbmOctaves, kFbmGain);
        return -params.bottom_depth * (1.0f - taper) + n * params.bottom_amplitude;
    }

    float sample(float x, float y, float z) const {
        const float r = std::sqrt(x * x + z * z);
        const float edge = noise.fbm(x * params.edge_noise_scale, kEdgeNoisePlane,
                z * params.edge_noise_scale, kFbmOctaves, kFbmGain);
        const float radial = (params.island_radius - edge * params.edge_noise_amplitude) - r;
        const float h_top = top_height_at(x, z);
        const float h_bot = bottom_height_at(x, z, r);

        float d = std::min({h_top - y, y - h_bot, radial});

        // Резьба по нижней части: верхняя поверхность остаётся строительной.
        const float depth_weight = std::clamp((params.top_height - y) /
                (params.top_height + params.bottom_depth), 0.0f, 1.0f);
        const float carve = noise.sample(x * params.carve_noise_scale, y * params.carve_noise_scale,
                z * params.carve_noise_scale);
        d -= (carve * 0.5f + 0.5f) * params.carve_amplitude * depth_weight;
        return d;
    }

    // Высота поверхности в столбце (x, z): первый переход снаружи -> внутрь
    // сверху вниз; nullopt-семантика через флаг.
    bool surface_height(float x, float z, float y_from, float y_to, float &out_height) const {
        float prev_y = y_from;
        float prev_d = sample(x, prev_y, z);
        for (float y = y_from - kSurfaceScanStep; y >= y_to; y -= kSurfaceScanStep) {
            const float d = sample(x, y, z);
            if (prev_d < 0.0f && d >= 0.0f) {
                // Бисекция нуля между prev_y (снаружи) и y (внутри).
                float lo = y;
                float hi = prev_y;
                for (int i = 0; i < kSurfaceBisectIters; ++i) {
                    const float mid = 0.5f * (lo + hi);
                    if (sample(x, mid, z) >= 0.0f) {
                        lo = mid;
                    } else {
                        hi = mid;
                    }
                }
                out_height = 0.5f * (lo + hi);
                return true;
            }
            prev_y = y;
            prev_d = d;
        }
        return false;
    }
};

// Зеркальная укладка: непрерывна всюду, поэтому бесшовна внутри сегмента
// атласа при любом повторе (пилообразный fract дал бы швы).
float mirror01(float t) {
    const float half = t * 0.5f;
    const float frac = half - std::floor(half);
    return std::fabs(frac * 2.0f - 1.0f);
}

void compute_normals_and_uvs(IslandData &island, const DensityField &field,
        const GeneratorParams &params) {
    Mesh &mesh = island.mesh;
    mesh.normals.resize(mesh.positions.size());
    island.uvs.resize(mesh.positions.size() * 2);

    for (size_t i = 0; i < mesh.positions.size(); ++i) {
        const math::Vec3 p = mesh.positions[i];
        // Нормаль — антиградиент плотности (плотность растёт внутрь).
        const math::Vec3 gradient{
            field.sample(p.x + kGradientEps, p.y, p.z) - field.sample(p.x - kGradientEps, p.y, p.z),
            field.sample(p.x, p.y + kGradientEps, p.z) - field.sample(p.x, p.y - kGradientEps, p.z),
            field.sample(p.x, p.y, p.z + kGradientEps) - field.sample(p.x, p.y, p.z - kGradientEps),
        };
        const math::Vec3 normal = normalized(gradient * -1.0f);
        mesh.normals[i] = normal;

        // Сегмент атласа 2x2 по нормали: верх — трава, низ — тёмная порода,
        // бок — светлая порода. Планарная проекция по доминантной оси.
        float tile_u;
        float tile_v;
        float cu;
        float cv;
        if (normal.y > kUpNormalThreshold) {
            tile_u = 0.0f;
            tile_v = 0.0f;
            cu = p.x;
            cv = p.z;
        } else if (normal.y < -kUpNormalThreshold) {
            tile_u = 0.0f;
            tile_v = 0.5f;
            cu = p.x;
            cv = p.z;
        } else {
            tile_u = 0.5f;
            tile_v = 0.0f;
            if (std::fabs(normal.x) > std::fabs(normal.z)) {
                cu = p.z;
                cv = p.y;
            } else {
                cu = p.x;
                cv = p.y;
            }
        }
        const float scale = 1.0f / params.uv_tile_world_size;
        island.uvs[i * 2] = tile_u + mirror01(cu * scale) * 0.5f;
        island.uvs[i * 2 + 1] = tile_v + mirror01(cv * scale) * 0.5f;
    }
}

void build_grid(GridData &grid, const DensityField &field, const GeneratorParams &params,
        float y_top, float y_bottom) {
    grid.cells_x = params.grid_cells;
    grid.cells_z = params.grid_cells;
    grid.cell_size = params.cell_size;
    grid.origin_x = -0.5f * params.grid_cells * params.cell_size;
    grid.origin_z = grid.origin_x;
    grid.types.assign(static_cast<size_t>(grid.cells_x) * grid.cells_z,
            static_cast<int32_t>(CellType::Void));
    grid.heights.assign(grid.types.size(), 0.0f);

    // Высоты в углах клеток (разделяются соседями): (cells+1)^2 столбцов.
    const int corners = params.grid_cells + 1;
    std::vector<float> corner_height(static_cast<size_t>(corners) * corners, 0.0f);
    std::vector<uint8_t> corner_found(corner_height.size(), 0);
    for (int cz = 0; cz < corners; ++cz) {
        for (int cx = 0; cx < corners; ++cx) {
            const float x = grid.origin_x + cx * params.cell_size;
            const float z = grid.origin_z + cz * params.cell_size;
            float height = 0.0f;
            if (field.surface_height(x, z, y_top, y_bottom, height)) {
                corner_height[static_cast<size_t>(cz) * corners + cx] = height;
                corner_found[static_cast<size_t>(cz) * corners + cx] = 1;
            }
        }
    }

    for (int cz = 0; cz < grid.cells_z; ++cz) {
        for (int cx = 0; cx < grid.cells_x; ++cx) {
            const size_t cell_index = static_cast<size_t>(cz) * grid.cells_x + cx;
            const float center_x = grid.origin_x + (cx + 0.5f) * params.cell_size;
            const float center_z = grid.origin_z + (cz + 0.5f) * params.cell_size;
            float center_height = 0.0f;
            if (!field.surface_height(center_x, center_z, y_top, y_bottom, center_height)) {
                continue; // Void
            }

            bool all_corners = true;
            float min_h = center_height;
            float max_h = center_height;
            for (int dz = 0; dz <= 1 && all_corners; ++dz) {
                for (int dx = 0; dx <= 1; ++dx) {
                    const size_t corner_index = static_cast<size_t>(cz + dz) * corners + (cx + dx);
                    if (!corner_found[corner_index]) {
                        all_corners = false;
                        break;
                    }
                    min_h = std::min(min_h, corner_height[corner_index]);
                    max_h = std::max(max_h, corner_height[corner_index]);
                }
            }

            grid.heights[cell_index] = center_height;
            const bool flat = all_corners && (max_h - min_h) <= params.flat_threshold;
            grid.types[cell_index] = static_cast<int32_t>(flat ? CellType::Buildable : CellType::Blocked);
        }
    }
}

void place_poi(GridData &grid, const GeneratorParams &params, math::Rng &rng) {
    // Кандидаты в детерминированном порядке обхода сетки.
    std::vector<size_t> candidates;
    for (size_t i = 0; i < grid.types.size(); ++i) {
        if (grid.types[i] == static_cast<int32_t>(CellType::Buildable)) {
            candidates.push_back(i);
        }
    }

    const int32_t count = std::min<int32_t>(params.poi_count,
            static_cast<int32_t>(candidates.size()));
    for (int32_t n = 0; n < count; ++n) {
        const int32_t pick = rng.range_int(0, static_cast<int32_t>(candidates.size()) - 1);
        const size_t cell_index = candidates[pick];
        candidates.erase(candidates.begin() + pick);

        grid.types[cell_index] = static_cast<int32_t>(CellType::Blocked);
        PoiSpot spot;
        spot.cell_x = static_cast<int32_t>(cell_index % grid.cells_x);
        spot.cell_z = static_cast<int32_t>(cell_index / grid.cells_x);
        spot.kind = rng.range_int(0, 1) == 0 ? kPoiKindStone : kPoiKindWood;
        spot.amount = rng.range_int(params.poi_min_amount, params.poi_max_amount);
        grid.poi.push_back(std::move(spot));
    }
}

} // namespace

bool params_from_json(const std::string &json_text, GeneratorParams &params, std::string &error) {
    save::JsonValue root;
    if (!save::parse_json(json_text, root, error)) {
        return false;
    }
    if (!root.is_object()) {
        error = "конфиг генератора должен быть JSON-объектом";
        return false;
    }
    GeneratorParams defaults;
    params.cell_size = static_cast<float>(root.number_at("cell_size", defaults.cell_size));
    params.grid_cells = root.int_at("grid_cells", defaults.grid_cells);
    params.voxels_per_cell = root.int_at("voxels_per_cell", defaults.voxels_per_cell);
    params.island_radius = static_cast<float>(root.number_at("island_radius", defaults.island_radius));
    params.top_height = static_cast<float>(root.number_at("top_height", defaults.top_height));
    params.top_amplitude = static_cast<float>(root.number_at("top_amplitude", defaults.top_amplitude));
    params.top_noise_scale = static_cast<float>(root.number_at("top_noise_scale", defaults.top_noise_scale));
    params.bottom_depth = static_cast<float>(root.number_at("bottom_depth", defaults.bottom_depth));
    params.bottom_amplitude =
            static_cast<float>(root.number_at("bottom_amplitude", defaults.bottom_amplitude));
    params.bottom_noise_scale =
            static_cast<float>(root.number_at("bottom_noise_scale", defaults.bottom_noise_scale));
    params.edge_noise_amplitude =
            static_cast<float>(root.number_at("edge_noise_amplitude", defaults.edge_noise_amplitude));
    params.edge_noise_scale =
            static_cast<float>(root.number_at("edge_noise_scale", defaults.edge_noise_scale));
    params.carve_amplitude =
            static_cast<float>(root.number_at("carve_amplitude", defaults.carve_amplitude));
    params.carve_noise_scale =
            static_cast<float>(root.number_at("carve_noise_scale", defaults.carve_noise_scale));
    params.flat_threshold = static_cast<float>(root.number_at("flat_threshold", defaults.flat_threshold));
    params.poi_count = root.int_at("poi_count", defaults.poi_count);
    params.poi_min_amount = root.int_at("poi_min_amount", defaults.poi_min_amount);
    params.poi_max_amount = root.int_at("poi_max_amount", defaults.poi_max_amount);
    params.uv_tile_world_size =
            static_cast<float>(root.number_at("uv_tile_world_size", defaults.uv_tile_world_size));

    if (params.grid_cells <= 0 || params.voxels_per_cell <= 0 || params.cell_size <= 0.0f ||
            params.island_radius <= 0.0f || params.uv_tile_world_size <= 0.0f) {
        error = "недопустимые параметры генератора (нулевые или отрицательные размеры)";
        return false;
    }
    return true;
}

IslandData generate_island(uint64_t seed, const GeneratorParams &params) {
    IslandData island;
    const math::SimplexNoise noise(seed);
    math::Rng rng(math::mix_seed(seed, 0xD1CE));
    const DensityField field{params, noise};

    // Вертикальные пределы поля: рельеф + запас, чтобы поверхность замкнулась.
    const float y_top = params.top_height + params.top_amplitude + 2.0f;
    const float y_bottom = -(params.bottom_depth + params.bottom_amplitude + 2.0f);

    // Решётка вокселей: сетка + запас в 2 клетки по горизонтали.
    const float voxel = params.cell_size / params.voxels_per_cell;
    const float margin = 2.0f * params.cell_size;
    const float half_extent = 0.5f * params.grid_cells * params.cell_size + margin;
    const int nx = static_cast<int>(std::ceil(2.0f * half_extent / voxel)) + 1;
    const int ny = static_cast<int>(std::ceil((y_top - y_bottom) / voxel)) + 1;

    // Кэш значений поля по узлам решётки (MC читает каждый узел многократно).
    std::vector<float> values(static_cast<size_t>(nx) * nx * ny);
    for (int iz = 0; iz < nx; ++iz) {
        for (int iy = 0; iy < ny; ++iy) {
            for (int ix = 0; ix < nx; ++ix) {
                const float x = -half_extent + ix * voxel;
                const float y = y_bottom + iy * voxel;
                const float z = -half_extent + iz * voxel;
                values[(static_cast<size_t>(iz) * ny + iy) * nx + ix] = field.sample(x, y, z);
            }
        }
    }
    const DensityAt density_at = [&](int ix, int iy, int iz) {
        return values[(static_cast<size_t>(iz) * ny + iy) * nx + ix];
    };

    island.mesh = polygonize(nx, ny, nx, density_at);
    // Координаты решётки -> мировые.
    for (math::Vec3 &p : island.mesh.positions) {
        p.x = -half_extent + p.x * voxel;
        p.y = y_bottom + p.y * voxel;
        p.z = -half_extent + p.z * voxel;
    }

    compute_normals_and_uvs(island, field, params);
    build_grid(island.grid, field, params, y_top, y_bottom);
    place_poi(island.grid, params, rng);
    return island;
}

GridData generate_grid(uint64_t seed, const GeneratorParams &params) {
    // Тот же сид, тот же порядок использования RNG, что в generate_island:
    // сетка и POI обязаны совпадать с клиентской визуализацией байт-в-байт.
    const math::SimplexNoise noise(seed);
    math::Rng rng(math::mix_seed(seed, 0xD1CE));
    const DensityField field{params, noise};
    const float y_top = params.top_height + params.top_amplitude + 2.0f;
    const float y_bottom = -(params.bottom_depth + params.bottom_amplitude + 2.0f);

    GridData grid;
    build_grid(grid, field, params, y_top, y_bottom);
    place_poi(grid, params, rng);
    return grid;
}

} // namespace dicecore::gen
