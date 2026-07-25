// Тесты генератора острова: JSON-парсер, шум, Marching Cubes (замкнутость),
// детерминизм по сиду, строительная сетка, POI, GLB. Без Godot.
#include "gen/glb_export.hpp"
#include "gen/island_gen.hpp"
#include "gen/marching_cubes.hpp"
#include "math/noise.hpp"
#include "math/rng.hpp"
#include "save/json.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void expect(bool condition, const char *message) {
    if (!condition) {
        ++g_failures;
        std::fprintf(stderr, "FAIL: %s\n", message);
    }
}

// --- JSON ---

void test_json_parser() {
    dicecore::save::JsonValue root;
    std::string error;
    const std::string text = R"({"a": 1.5, "b": {"c": [1, 2, true]}, "s": "текст", "n": null})";
    expect(dicecore::save::parse_json(text, root, error), "валидный JSON разбирается");
    expect(root.number_at("a", 0.0) == 1.5, "число читается");
    expect(root.as_object().at("s").as_string() == "текст", "строка читается");
    const auto &inner = root.as_object().at("b");
    expect(inner.as_object().at("c").as_array().size() == 3, "вложенный массив читается");
    expect(inner.as_object().at("c").as_array()[2].as_bool(), "bool в массиве читается");
    expect(root.number_at("missing", -7.0) == -7.0, "отсутствующий ключ даёт fallback");

    expect(!dicecore::save::parse_json("{\"a\": }", root, error), "обрыв значения — ошибка");
    expect(!dicecore::save::parse_json("{\"a\": 1} мусор", root, error), "хвостовой мусор — ошибка");
    expect(!dicecore::save::parse_json("", root, error), "пустой текст — ошибка");
}

// --- Шум и RNG ---

void test_noise_and_rng_determinism() {
    dicecore::math::SimplexNoise a(42);
    dicecore::math::SimplexNoise b(42);
    dicecore::math::SimplexNoise c(43);
    bool identical = true;
    bool differs = false;
    float min_v = 1e9f;
    float max_v = -1e9f;
    for (int i = 0; i < 500; ++i) {
        const float x = i * 0.37f;
        const float y = i * 0.21f;
        const float z = i * 0.11f;
        const float va = a.sample(x, y, z);
        identical = identical && va == b.sample(x, y, z);
        differs = differs || std::fabs(va - c.sample(x, y, z)) > 1e-6f;
        min_v = std::min(min_v, va);
        max_v = std::max(max_v, va);
    }
    expect(identical, "шум с одним сидом идентичен");
    expect(differs, "шум с разными сидами различается");
    expect(min_v < -0.3f && max_v > 0.3f, "шум покрывает оба знака");
    expect(min_v >= -1.5f && max_v <= 1.5f, "амплитуда шума в разумных пределах");

    dicecore::math::Rng r1(7);
    dicecore::math::Rng r2(7);
    bool rng_same = true;
    for (int i = 0; i < 100; ++i) {
        rng_same = rng_same && r1.next_u64() == r2.next_u64();
    }
    expect(rng_same, "RNG детерминирован по сиду");
    dicecore::math::Rng r3(8);
    int in_range = 0;
    for (int i = 0; i < 1000; ++i) {
        const int v = r3.range_int(2, 4);
        in_range += (v >= 2 && v <= 4) ? 1 : 0;
    }
    expect(in_range == 1000, "range_int держит границы включительно");
}

// --- Marching Cubes: замкнутость на сфере ---

void test_marching_cubes_sphere() {
    // Сфера радиуса 6 в решётке 17^3: плотность = r - |p - центр|.
    constexpr int kN = 17;
    constexpr float kRadius = 6.0f;
    constexpr float kCenter = (kN - 1) / 2.0f;
    const dicecore::gen::DensityAt sphere = [](int x, int y, int z) {
        const float dx = x - kCenter;
        const float dy = y - kCenter;
        const float dz = z - kCenter;
        return kRadius - std::sqrt(dx * dx + dy * dy + dz * dz);
    };
    const dicecore::gen::Mesh mesh = dicecore::gen::polygonize(kN, kN, kN, sphere);

    expect(!mesh.positions.empty() && !mesh.indices.empty(), "сфера полигонизуется");
    expect(mesh.indices.size() % 3 == 0, "индексы кратны трём");

    // Замкнутое 2-многообразие: каждое ребро — ровно в двух треугольниках
    // с противоположной ориентацией; эйлерова характеристика сферы = 2.
    std::map<std::pair<uint32_t, uint32_t>, int> edge_use;
    int degenerate = 0;
    for (size_t i = 0; i < mesh.indices.size(); i += 3) {
        const uint32_t v[3] = {mesh.indices[i], mesh.indices[i + 1], mesh.indices[i + 2]};
        if (v[0] == v[1] || v[1] == v[2] || v[0] == v[2]) {
            ++degenerate;
            continue;
        }
        for (int e = 0; e < 3; ++e) {
            edge_use[{v[e], v[(e + 1) % 3]}] += 1;
        }
    }
    expect(degenerate == 0, "нет вырожденных треугольников");
    bool manifold = true;
    for (const auto &[edge, count] : edge_use) {
        const auto reversed = edge_use.find({edge.second, edge.first});
        if (count != 1 || reversed == edge_use.end() || reversed->second != 1) {
            manifold = false;
            break;
        }
    }
    expect(manifold, "каждое ребро в двух треугольниках с противоположной ориентацией");

    const long long verts = static_cast<long long>(mesh.positions.size());
    const long long faces = static_cast<long long>(mesh.indices.size() / 3 - degenerate);
    const long long edges = static_cast<long long>(edge_use.size() / 2);
    expect(verts - edges + faces == 2, "эйлерова характеристика сферы равна 2");

    // Ориентация: нормаль треугольника смотрит наружу от центра сферы
    // (по центроиду; строго внутрь — ни одного, нули допустимы на
    // симметричных решётке треугольниках, где нормаль перпендикулярна радиусу).
    int outward = 0;
    int inward = 0;
    for (size_t i = 0; i < mesh.indices.size(); i += 3) {
        const dicecore::math::Vec3 p0 = mesh.positions[mesh.indices[i]];
        const dicecore::math::Vec3 p1 = mesh.positions[mesh.indices[i + 1]];
        const dicecore::math::Vec3 p2 = mesh.positions[mesh.indices[i + 2]];
        const dicecore::math::Vec3 face_normal = cross(p1 - p0, p2 - p0);
        const dicecore::math::Vec3 centroid = (p0 + p1 + p2) * (1.0f / 3.0f);
        const dicecore::math::Vec3 center_out{centroid.x - kCenter, centroid.y - kCenter,
                centroid.z - kCenter};
        const float side = dot(face_normal, center_out);
        outward += side > 0.0f ? 1 : 0;
        inward += side < 0.0f ? 1 : 0;
    }
    expect(inward == 0 && outward > 0, "обход треугольников CCW при взгляде снаружи");
}

// --- Генератор острова ---

dicecore::gen::GeneratorParams test_params() {
    dicecore::gen::GeneratorParams params;
    return params; // дефолты соответствуют data/generator.json
}

bool meshes_identical(const dicecore::gen::IslandData &a, const dicecore::gen::IslandData &b) {
    if (a.mesh.positions.size() != b.mesh.positions.size() ||
            a.mesh.indices != b.mesh.indices || a.uvs != b.uvs) {
        return false;
    }
    for (size_t i = 0; i < a.mesh.positions.size(); ++i) {
        if (std::memcmp(&a.mesh.positions[i], &b.mesh.positions[i], sizeof(float) * 3) != 0 ||
                std::memcmp(&a.mesh.normals[i], &b.mesh.normals[i], sizeof(float) * 3) != 0) {
            return false;
        }
    }
    return true;
}

void test_island_determinism() {
    const dicecore::gen::GeneratorParams params = test_params();
    const dicecore::gen::IslandData first = dicecore::gen::generate_island(1234, params);
    const dicecore::gen::IslandData second = dicecore::gen::generate_island(1234, params);
    const dicecore::gen::IslandData other = dicecore::gen::generate_island(1235, params);

    expect(meshes_identical(first, second), "один сид — идентичный меш");
    expect(first.grid.types == second.grid.types, "один сид — идентичная сетка");
    expect(first.grid.poi.size() == second.grid.poi.size(), "один сид — идентичные POI");
    for (size_t i = 0; i < first.grid.poi.size(); ++i) {
        expect(first.grid.poi[i].cell_x == second.grid.poi[i].cell_x &&
                        first.grid.poi[i].kind == second.grid.poi[i].kind &&
                        first.grid.poi[i].amount == second.grid.poi[i].amount,
                "POI повторяются по сиду");
    }
    expect(!meshes_identical(first, other), "другой сид — другой меш");

    const std::vector<uint8_t> glb1 = dicecore::gen::export_glb(first);
    const std::vector<uint8_t> glb2 = dicecore::gen::export_glb(second);
    expect(glb1 == glb2, "GLB повторяем байт-в-байт");
}

void test_island_grid() {
    const dicecore::gen::GeneratorParams params = test_params();
    for (uint64_t seed = 1; seed <= 3; ++seed) {
        const dicecore::gen::IslandData island = dicecore::gen::generate_island(seed, params);
        int buildable = 0;
        int blocked = 0;
        for (size_t i = 0; i < island.grid.types.size(); ++i) {
            const int32_t type = island.grid.types[i];
            if (type == static_cast<int32_t>(dicecore::gen::CellType::Buildable)) {
                ++buildable;
                expect(std::isfinite(island.grid.heights[i]), "высота Buildable-клетки конечна");
            } else if (type == static_cast<int32_t>(dicecore::gen::CellType::Blocked)) {
                ++blocked;
            }
        }
        // SPEC §11.3: порядка 140–180 пригодных клеток; допуск теста шире,
        // точная подстройка — data/generator.json [баланс].
        expect(buildable >= 100 && buildable <= 220, "число Buildable в целевом диапазоне");
        expect(blocked > 0, "есть Blocked-клетки (POI или рельеф)");

        expect(island.grid.poi.size() == static_cast<size_t>(params.poi_count),
                "размещены все POI");
        for (const dicecore::gen::PoiSpot &poi : island.grid.poi) {
            expect(poi.cell_x >= 0 && poi.cell_x < island.grid.cells_x && poi.cell_z >= 0 &&
                            poi.cell_z < island.grid.cells_z,
                    "POI в границах сетки");
            expect(island.grid.type_at(poi.cell_x, poi.cell_z) ==
                            static_cast<int32_t>(dicecore::gen::CellType::Blocked),
                    "клетка POI помечена Blocked");
            expect(poi.amount >= params.poi_min_amount && poi.amount <= params.poi_max_amount,
                    "количество ресурса POI в диапазоне");
            expect(poi.kind == dicecore::gen::kPoiKindStone || poi.kind == dicecore::gen::kPoiKindWood,
                    "вид POI известен");
        }

        // Меш острова замкнут (нужно деструктору ландшафта и теням).
        expect(!island.mesh.positions.empty(), "меш острова не пуст");
        expect(island.mesh.normals.size() == island.mesh.positions.size(), "нормали на все вершины");
        expect(island.uvs.size() == island.mesh.positions.size() * 2, "UV на все вершины");
        for (size_t i = 0; i < island.uvs.size(); ++i) {
            expect(island.uvs[i] >= 0.0f && island.uvs[i] <= 1.0f, "UV в границах атласа");
        }
    }
}

void test_params_json() {
    dicecore::gen::GeneratorParams params;
    std::string error;
    expect(dicecore::gen::params_from_json(R"({"island_radius": 10.0, "poi_count": 3})", params, error),
            "частичный конфиг разбирается");
    expect(params.island_radius == 10.0f, "заданный параметр применён");
    expect(params.grid_cells == 24, "незаданный параметр остаётся дефолтным");

    expect(!dicecore::gen::params_from_json("[1,2]", params, error), "не-объект отклонён");
    expect(!dicecore::gen::params_from_json(R"({"cell_size": -1})", params, error),
            "отрицательный размер клетки отклонён");
}

void test_glb_structure() {
    const dicecore::gen::IslandData island = dicecore::gen::generate_island(5, test_params());
    const std::vector<uint8_t> glb = dicecore::gen::export_glb(island);
    expect(glb.size() > 100, "GLB не пуст");
    expect(glb.size() % 4 == 0, "GLB выровнен по 4 байтам");
    uint32_t magic = 0;
    uint32_t version = 0;
    uint32_t length = 0;
    std::memcpy(&magic, glb.data(), 4);
    std::memcpy(&version, glb.data() + 4, 4);
    std::memcpy(&length, glb.data() + 8, 4);
    expect(magic == 0x46546C67u, "магическое число glTF");
    expect(version == 2u, "версия glTF 2");
    expect(length == glb.size(), "длина в заголовке равна размеру файла");
}

} // namespace

int main() {
    test_json_parser();
    test_noise_and_rng_determinism();
    test_marching_cubes_sphere();
    test_island_determinism();
    test_island_grid();
    test_params_json();
    test_glb_structure();

    if (g_failures == 0) {
        std::printf("OK: все тесты генератора пройдены\n");
        return 0;
    }
    std::fprintf(stderr, "ПРОВАЛЕНО ПРОВЕРОК: %d\n", g_failures);
    return 1;
}
