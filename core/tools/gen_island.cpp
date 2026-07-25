// CLI-утилита генерации острова без Godot (PLAN, этап 3).
// Использование: gen_island --seed N [--config path.json] [--out island.glb]
#include "gen/glb_export.hpp"
#include "gen/island_gen.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

namespace {

bool read_file(const std::string &path, std::string &out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    out = buffer.str();
    return true;
}

int count_cells(const dicecore::gen::GridData &grid, dicecore::gen::CellType type) {
    int count = 0;
    for (const int32_t cell : grid.types) {
        if (cell == static_cast<int32_t>(type)) {
            ++count;
        }
    }
    return count;
}

} // namespace

int main(int argc, char **argv) {
    uint64_t seed = 1;
    std::string config_path;
    std::string out_path = "island.glb";

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            seed = std::strtoull(argv[++i], nullptr, 10);
        } else if (std::strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            config_path = argv[++i];
        } else if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            out_path = argv[++i];
        } else {
            std::fprintf(stderr,
                    "Использование: %s --seed N [--config generator.json] [--out island.glb]\n",
                    argv[0]);
            return 2;
        }
    }

    dicecore::gen::GeneratorParams params;
    if (!config_path.empty()) {
        std::string config_text;
        if (!read_file(config_path, config_text)) {
            std::fprintf(stderr, "ОШИБКА: не удалось прочитать конфиг %s\n", config_path.c_str());
            return 1;
        }
        std::string error;
        if (!dicecore::gen::params_from_json(config_text, params, error)) {
            std::fprintf(stderr, "ОШИБКА конфига: %s\n", error.c_str());
            return 1;
        }
    }

    const dicecore::gen::IslandData island = dicecore::gen::generate_island(seed, params);
    const std::vector<uint8_t> glb = dicecore::gen::export_glb(island);

    std::ofstream out(out_path, std::ios::binary);
    if (!out) {
        std::fprintf(stderr, "ОШИБКА: не удалось открыть %s для записи\n", out_path.c_str());
        return 1;
    }
    out.write(reinterpret_cast<const char *>(glb.data()),
            static_cast<std::streamsize>(glb.size()));
    out.close();

    std::printf("сид=%llu вершин=%zu треугольников=%zu glb=%zu байт\n",
            static_cast<unsigned long long>(seed), island.mesh.positions.size(),
            island.mesh.indices.size() / 3, glb.size());
    std::printf("сетка %dx%d: пригодных=%d занятых=%d POI=%zu\n", island.grid.cells_x,
            island.grid.cells_z, count_cells(island.grid, dicecore::gen::CellType::Buildable),
            count_cells(island.grid, dicecore::gen::CellType::Blocked), island.grid.poi.size());
    return 0;
}
