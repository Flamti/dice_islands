#include "systems/combat/pathfind.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <set>

namespace dicecore::systems::combat {

namespace {

// 4-связность в детерминированном порядке (N, E, S, W).
constexpr int32_t kDir[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};

int32_t manhattan_to_nearest(int32_t x, int32_t z,
        const std::vector<std::pair<int32_t, int32_t>> &goals) {
    int32_t best = 1 << 30;
    for (const auto &g : goals) {
        best = std::min(best, std::abs(x - g.first) + std::abs(z - g.second));
    }
    return best;
}

} // namespace

std::vector<std::pair<int32_t, int32_t>> find_path(const PathGrid &grid, int32_t sx, int32_t sz,
        const std::vector<std::pair<int32_t, int32_t>> &goals) {
    std::vector<std::pair<int32_t, int32_t>> path;
    if (goals.empty() || !grid.in_bounds(sx, sz)) {
        return path;
    }
    std::set<std::pair<int32_t, int32_t>> goal_set(goals.begin(), goals.end());

    const size_t count = static_cast<size_t>(grid.cells_x) * grid.cells_z;
    std::vector<float> g_score(count, std::numeric_limits<float>::infinity());
    std::vector<int32_t> came_from(count, -1);
    std::vector<uint8_t> closed(count, 0);

    // Очередь: (f, tie=g уже учтён, index). Тай-брейк по индексу для детерминизма.
    struct Node {
        float f;
        int32_t index;
        bool operator>(const Node &o) const {
            return f != o.f ? f > o.f : index > o.index;
        }
    };
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;

    const auto index_of = [&](int32_t x, int32_t z) {
        return static_cast<size_t>(z) * grid.cells_x + x;
    };

    const size_t start = index_of(sx, sz);
    g_score[start] = 0.0f;
    open.push({static_cast<float>(manhattan_to_nearest(sx, sz, goals)), static_cast<int32_t>(start)});

    while (!open.empty()) {
        const int32_t current = open.top().index;
        open.pop();
        if (closed[current]) {
            continue;
        }
        closed[current] = 1;
        const int32_t cx = current % grid.cells_x;
        const int32_t cz = current / grid.cells_x;

        if (goal_set.count({cx, cz})) {
            // Восстановление пути.
            int32_t node = current;
            while (node != static_cast<int32_t>(start)) {
                path.push_back({node % grid.cells_x, node / grid.cells_x});
                node = came_from[node];
            }
            std::reverse(path.begin(), path.end());
            return path;
        }

        for (const auto &d : kDir) {
            const int32_t nx = cx + d[0];
            const int32_t nz = cz + d[1];
            if (!grid.in_bounds(nx, nz)) {
                continue;
            }
            const float extra = grid.cost_at(nx, nz);
            if (extra < 0.0f) {
                continue; // непроходимо (здание, вода)
            }
            const size_t ni = index_of(nx, nz);
            if (closed[ni]) {
                continue;
            }
            const float tentative = g_score[current] + 1.0f + extra;
            if (tentative < g_score[ni]) {
                g_score[ni] = tentative;
                came_from[ni] = current;
                const float h = static_cast<float>(manhattan_to_nearest(nx, nz, goals));
                open.push({tentative + h, static_cast<int32_t>(ni)});
            }
        }
    }
    return path; // пусто — цель недостижима
}

} // namespace dicecore::systems::combat
