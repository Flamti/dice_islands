#include "gen/destruction.hpp"

#include <algorithm>
#include <set>

namespace dicecore::gen {

namespace {

constexpr int32_t kNeighbors[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

bool is_surface(const GridData &grid, int32_t cx, int32_t cz) {
    if (cx < 0 || cx >= grid.cells_x || cz < 0 || cz >= grid.cells_z) {
        return false;
    }
    return grid.type_at(cx, cz) != static_cast<int32_t>(CellType::Void);
}

// Крайняя клетка: поверхность, у которой хотя бы один сосед — не поверхность.
bool is_edge(const GridData &grid, int32_t cx, int32_t cz) {
    if (!is_surface(grid, cx, cz)) {
        return false;
    }
    for (const auto &n : kNeighbors) {
        if (!is_surface(grid, cx + n[0], cz + n[1])) {
            return true;
        }
    }
    return false;
}

int64_t key_of(int32_t cx, int32_t cz) {
    return (static_cast<int64_t>(cz) << 32) | static_cast<uint32_t>(cx);
}

} // namespace

std::vector<GridCell> pick_edge_region(const GridData &grid,
        const std::vector<GridCell> &protected_cells, int32_t min_cells, int32_t max_cells,
        math::Rng &rng) {
    std::set<int64_t> protect;
    for (const GridCell &c : protected_cells) {
        protect.insert(key_of(c.cell_x, c.cell_z));
    }

    // Крайние клетки-кандидаты (не защищённые), в детерминированном порядке.
    std::vector<GridCell> edges;
    for (int32_t cz = 0; cz < grid.cells_z; ++cz) {
        for (int32_t cx = 0; cx < grid.cells_x; ++cx) {
            if (is_edge(grid, cx, cz) && protect.count(key_of(cx, cz)) == 0) {
                edges.push_back({cx, cz});
            }
        }
    }
    if (static_cast<int32_t>(edges.size()) < min_cells) {
        return {};
    }

    // Рост связного куска от случайной крайней клетки: BFS по поверхности,
    // предпочитая крайние клетки, обходя защищённые. Целевой размер в [min,max].
    const int32_t target =
            max_cells > min_cells ? rng.range_int(min_cells, max_cells) : min_cells;
    const GridCell start = edges[rng.range_int(0, static_cast<int32_t>(edges.size()) - 1)];

    std::set<int64_t> chosen;
    std::vector<GridCell> region;
    std::vector<GridCell> frontier{start};
    chosen.insert(key_of(start.cell_x, start.cell_z));
    while (!frontier.empty() && static_cast<int32_t>(region.size()) < target) {
        // Берём фронтирную клетку (детерминированно — первую).
        const GridCell cell = frontier.front();
        frontier.erase(frontier.begin());
        region.push_back(cell);
        // Соседи-поверхность, не защищённые, ещё не выбранные — во фронтир.
        for (const auto &n : kNeighbors) {
            const int32_t nx = cell.cell_x + n[0];
            const int32_t nz = cell.cell_z + n[1];
            if (!is_surface(grid, nx, nz)) {
                continue;
            }
            const int64_t k = key_of(nx, nz);
            if (protect.count(k) != 0 || chosen.count(k) != 0) {
                continue;
            }
            chosen.insert(k);
            frontier.push_back({nx, nz});
        }
    }
    if (static_cast<int32_t>(region.size()) < min_cells) {
        return {}; // не удалось набрать связный кусок нужного размера
    }
    return region;
}

} // namespace dicecore::gen
