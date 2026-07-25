#include "systems/combat/battle.hpp"
#include "systems/combat/pathfind.hpp"

#include "math/rng.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace dicecore::systems::combat {

namespace {

// Дальность ближнего боя с запасом на диагональ (клетка = 1.0, диагональ 1.41).
constexpr float kRangeSlack = 0.5f;
// Как часто пересчитывать путь бойца (тиков): компромисс цена/адаптивность.
constexpr int32_t kRepathTicks = 5;

struct SimFighter {
    int32_t id = 0;
    int32_t team = 0;
    int32_t owner_player = -1;
    bool attacker = false;
    int32_t group = -1; // индекс в setup.attackers (для атакующих)
    float x = 0.0f;
    float z = 0.0f;
    int32_t hp = 0;
    float attack_cd = 0.0f;
    bool alive = true; // hp > 0
    bool retreated = false; // покинул поле живым (цель разрушена)
    std::vector<std::pair<int32_t, int32_t>> path;
    size_t path_idx = 0;
    int32_t repath_timer = 0;
};

struct SimBuilding {
    BattleBuilding def;
    int32_t hp = 0;
    float tower_cd = 0.0f;
    bool alive = true;
};

float dist(const SimFighter &a, const SimFighter &b) {
    const float dx = a.x - b.x;
    const float dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz);
}

float dist_to(float ax, float az, float bx, float bz) {
    const float dx = ax - bx;
    const float dz = az - bz;
    return std::sqrt(dx * dx + dz * dz);
}

int32_t cell_of(float v) {
    return static_cast<int32_t>(std::floor(v));
}

struct Battle {
    const BattleSetup &setup;
    const CombatConfig &cfg;
    math::Rng rng;
    std::vector<SimFighter> fighters;
    std::vector<SimBuilding> buildings;
    std::vector<int32_t> occupancy; // cell -> building index или -1
    std::vector<int32_t> last_hit_group; // building index -> группа последнего удара
    int32_t next_id = 0;

    explicit Battle(const BattleSetup &s, uint64_t seed) : setup(s), cfg(s.config), rng(seed) {
        occupancy.assign(static_cast<size_t>(setup.cells_x) * setup.cells_z, -1);
        last_hit_group.assign(setup.buildings.size(), -1);
        buildings.reserve(setup.buildings.size());
        for (const BattleBuilding &b : setup.buildings) {
            SimBuilding sb;
            sb.def = b;
            sb.hp = b.hp;
            const int32_t index = static_cast<int32_t>(buildings.size());
            for (int32_t dz = 0; dz < b.size_z; ++dz) {
                for (int32_t dx = 0; dx < b.size_x; ++dx) {
                    const int32_t cx = b.cell_x + dx;
                    const int32_t cz = b.cell_z + dz;
                    if (in_bounds(cx, cz)) {
                        occupancy[idx(cx, cz)] = index;
                    }
                }
            }
            buildings.push_back(sb);
        }
    }

    bool in_bounds(int32_t x, int32_t z) const {
        return x >= 0 && x < setup.cells_x && z >= 0 && z < setup.cells_z;
    }
    size_t idx(int32_t x, int32_t z) const {
        return static_cast<size_t>(z) * setup.cells_x + x;
    }
    bool walkable(int32_t x, int32_t z) const {
        return in_bounds(x, z) && setup.walkable[idx(x, z)] != 0;
    }

    // Свободна ли клетка для стояния бойца (поверхность без здания).
    bool free_cell(int32_t x, int32_t z) const {
        return walkable(x, z) && occupancy[idx(x, z)] < 0;
    }

    // Крайние свободные клетки заданной стороны для высадки (SPEC §9.3).
    std::vector<std::pair<int32_t, int32_t>> landing_cells(int32_t side) const {
        std::vector<std::pair<int32_t, int32_t>> cells;
        // Свободная клетка считается краевой, если рядом (по нужной оси) — вода.
        for (int32_t z = 0; z < setup.cells_z; ++z) {
            for (int32_t x = 0; x < setup.cells_x; ++x) {
                if (!free_cell(x, z)) {
                    continue;
                }
                bool edge = false;
                switch (side) {
                    case 0: edge = !walkable(x, z - 1); break; // N
                    case 1: edge = !walkable(x + 1, z); break; // E
                    case 2: edge = !walkable(x, z + 1); break; // S
                    default: edge = !walkable(x - 1, z); break; // W
                }
                if (edge) {
                    cells.push_back({x, z});
                }
            }
        }
        return cells;
    }

    // Клетки-цели у здания: свободные клетки, ортогонально смежные с габаритом.
    std::vector<std::pair<int32_t, int32_t>> adjacent_free(const BattleBuilding &b) const {
        std::vector<std::pair<int32_t, int32_t>> cells;
        for (int32_t z = b.cell_z - 1; z <= b.cell_z + b.size_z; ++z) {
            for (int32_t x = b.cell_x - 1; x <= b.cell_x + b.size_x; ++x) {
                const bool inside = x >= b.cell_x && x < b.cell_x + b.size_x && z >= b.cell_z &&
                        z < b.cell_z + b.size_z;
                if (inside) {
                    continue;
                }
                // Только ортогональные соседи.
                const bool ortho = (x >= b.cell_x && x < b.cell_x + b.size_x) ||
                        (z >= b.cell_z && z < b.cell_z + b.size_z);
                if (ortho && free_cell(x, z)) {
                    cells.push_back({x, z});
                }
            }
        }
        return cells;
    }

    float building_center_x(const SimBuilding &b) const {
        return b.def.cell_x + b.def.size_x * 0.5f;
    }
    float building_center_z(const SimBuilding &b) const {
        return b.def.cell_z + b.def.size_z * 0.5f;
    }

    // Суммарный DPS живых атакующих команды (для оценки цены слома стены).
    float team_dps(int32_t team) const {
        float dps = 0.0f;
        for (const SimFighter &f : fighters) {
            if (f.alive && f.attacker && f.team == team) {
                dps += cfg.fighter.strength * cfg.fighter.attack_rate;
            }
        }
        return std::max(dps, 0.001f);
    }

    // Сетка A* для атакующей команды: стены — проходимы с ценой слома.
    PathGrid attacker_grid(int32_t team) const {
        PathGrid grid;
        grid.cells_x = setup.cells_x;
        grid.cells_z = setup.cells_z;
        grid.extra_cost.assign(occupancy.size(), -1.0f);
        const float dps = team_dps(team);
        for (int32_t z = 0; z < setup.cells_z; ++z) {
            for (int32_t x = 0; x < setup.cells_x; ++x) {
                if (!walkable(x, z)) {
                    continue;
                }
                const int32_t b = occupancy[idx(x, z)];
                if (b < 0) {
                    grid.extra_cost[idx(x, z)] = 0.0f;
                } else if (buildings[b].alive && buildings[b].def.is_wall) {
                    // Цена входа в стену ≈ время слома в клетках хода (SPEC §9.3).
                    const float break_time = buildings[b].hp / dps;
                    grid.extra_cost[idx(x, z)] =
                            break_time * cfg.fighter.move_speed * cfg.wall_detour_factor;
                }
                // прочие здания остаются непроходимыми (-1)
            }
        }
        return grid;
    }

    // Сетка A* для защитника: стены и здания непроходимы.
    PathGrid defender_grid() const {
        PathGrid grid;
        grid.cells_x = setup.cells_x;
        grid.cells_z = setup.cells_z;
        grid.extra_cost.assign(occupancy.size(), -1.0f);
        for (int32_t z = 0; z < setup.cells_z; ++z) {
            for (int32_t x = 0; x < setup.cells_x; ++x) {
                if (walkable(x, z) && occupancy[idx(x, z)] < 0) {
                    grid.extra_cost[idx(x, z)] = 0.0f;
                }
            }
        }
        return grid;
    }

    bool on_field(const SimFighter &f) const {
        return f.alive && !f.retreated;
    }

    bool hostile(const SimFighter &a, const SimFighter &b) const {
        if (!on_field(a) || !on_field(b)) {
            return false;
        }
        if (a.attacker == b.attacker) {
            // Обе — атакующие: враждебны, если разные команды (SPEC §9.3).
            // Обе — защитники: союзники.
            return a.attacker && a.team != b.team;
        }
        return true; // атакующий против защитника
    }

    // Ближайший враждебный живой боец; тай-брейк по id.
    int32_t nearest_enemy(const SimFighter &self) const {
        int32_t best = -1;
        float best_d = std::numeric_limits<float>::infinity();
        for (size_t i = 0; i < fighters.size(); ++i) {
            const SimFighter &other = fighters[i];
            if (!hostile(self, other)) {
                continue;
            }
            const float d = dist(self, other);
            if (d < best_d - 1e-6f) {
                best_d = d;
                best = static_cast<int32_t>(i);
            }
        }
        return best;
    }

    int32_t defender_armor_at(float x, float z) const {
        // +броня защитнику рядом со своей живой стеной (SPEC §9.3).
        const int32_t cx = cell_of(x);
        const int32_t cz = cell_of(z);
        for (int32_t dz = -1; dz <= 1; ++dz) {
            for (int32_t dx = -1; dx <= 1; ++dx) {
                const int32_t nx = cx + dx;
                const int32_t nz = cz + dz;
                if (!in_bounds(nx, nz)) {
                    continue;
                }
                const int32_t b = occupancy[idx(nx, nz)];
                if (b >= 0 && buildings[b].alive && buildings[b].def.is_wall) {
                    return cfg.wall_defense_armor_bonus;
                }
            }
        }
        return 0;
    }

    // Удар по бойцу с учётом брони и уворота. Возвращает true при попадании.
    bool strike_fighter(SimFighter &target) {
        const float roll = static_cast<float>(rng.range_double(0.0, 1.0));
        if (roll < cfg.fighter.dodge) {
            return false; // уворот
        }
        int32_t armor = cfg.fighter.armor;
        if (!target.attacker) {
            armor += defender_armor_at(target.x, target.z);
        }
        const int32_t damage = std::max(cfg.fighter.strength - armor, 1);
        target.hp -= damage;
        if (target.hp <= 0) {
            target.alive = false;
        }
        return true;
    }

    void damage_building(int32_t index, int32_t amount, int32_t attacker_group,
            std::vector<int32_t> &destroyed) {
        SimBuilding &b = buildings[index];
        if (!b.alive) {
            return;
        }
        b.hp -= amount;
        // Кто нанёс последний удар (для награды за цель, SPEC §9.3).
        if (attacker_group >= 0) {
            last_hit_group[index] = attacker_group;
        }
        if (b.hp <= 0) {
            b.alive = false;
            destroyed.push_back(b.def.id);
            // Стена рухнула — клетки снова проходимы (occupancy зачищаем).
            if (b.def.is_wall) {
                for (int32_t dz = 0; dz < b.def.size_z; ++dz) {
                    for (int32_t dx = 0; dx < b.def.size_x; ++dx) {
                        const int32_t cx = b.def.cell_x + dx;
                        const int32_t cz = b.def.cell_z + dz;
                        if (in_bounds(cx, cz)) {
                            occupancy[idx(cx, cz)] = -1;
                        }
                    }
                }
            }
        }
    }
};

// Индекс стены на клетке (или -1).
int32_t wall_at(const Battle &battle, int32_t x, int32_t z) {
    if (!battle.in_bounds(x, z)) {
        return -1;
    }
    const int32_t b = battle.occupancy[battle.idx(x, z)];
    if (b >= 0 && battle.buildings[b].alive && battle.buildings[b].def.is_wall) {
        return b;
    }
    return -1;
}

} // namespace

BattleResult simulate_battle(const BattleSetup &setup, uint64_t seed) {
    Battle battle(setup, seed);
    const CombatConfig &cfg = setup.config;
    BattleResult result;

    // --- Спавн защитников у казарм/замка (SPEC §9.3) ---
    std::vector<std::pair<int32_t, int32_t>> spawn_cells;
    for (const SimBuilding &b : battle.buildings) {
        if (!b.def.spawns_garrison) {
            continue;
        }
        for (const auto &c : battle.adjacent_free(b.def)) {
            spawn_cells.push_back(c);
        }
    }
    for (int32_t i = 0; i < setup.defender_garrison; ++i) {
        SimFighter f;
        f.id = battle.next_id++;
        f.team = setup.defender_team;
        f.owner_player = setup.defender_player;
        f.attacker = false;
        f.hp = cfg.fighter.hp;
        f.attack_cd = 0.0f;
        if (spawn_cells.empty()) {
            break;
        }
        const auto &c = spawn_cells[i % spawn_cells.size()];
        f.x = c.first + 0.5f;
        f.z = c.second + 0.5f;
        battle.fighters.push_back(f);
    }

    // --- Спавн атакующих на краю выбранной стороны ---
    std::vector<int32_t> group_target(setup.attackers.size(), -1);
    for (size_t gi = 0; gi < setup.attackers.size(); ++gi) {
        const AttackerGroup &group = setup.attackers[gi];
        // Цель группы: заданная либо ближайшее к высадке здание-невстена/небашня.
        int32_t target = group.target_building;
        const auto landings = battle.landing_cells(group.landing_side);
        if (target < 0 && !landings.empty()) {
            float best = std::numeric_limits<float>::infinity();
            const float lx = landings.front().first + 0.5f;
            const float lz = landings.front().second + 0.5f;
            for (size_t bi = 0; bi < battle.buildings.size(); ++bi) {
                const SimBuilding &b = battle.buildings[bi];
                if (b.def.is_wall) {
                    continue;
                }
                const float d = dist_to(lx, lz, battle.building_center_x(b), battle.building_center_z(b));
                if (d < best) {
                    best = d;
                    target = static_cast<int32_t>(bi);
                }
            }
        }
        group_target[gi] = target;

        for (int32_t i = 0; i < group.count; ++i) {
            SimFighter f;
            f.id = battle.next_id++;
            f.team = group.team;
            f.owner_player = group.owner_player;
            f.attacker = true;
            f.group = static_cast<int32_t>(gi);
            f.hp = cfg.fighter.hp;
            if (landings.empty()) {
                break;
            }
            const auto &c = landings[i % landings.size()];
            f.x = c.first + 0.5f;
            f.z = c.second + 0.5f;
            battle.fighters.push_back(f);
        }
    }

    const float dt = 1.0f / cfg.tick_rate;
    const int32_t max_ticks = static_cast<int32_t>(cfg.timeout_sec * cfg.tick_rate);
    const float within = cfg.fighter.range + kRangeSlack;

    std::vector<bool> group_target_destroyed(setup.attackers.size(), false);

    auto record_frame = [&](int32_t tick) {
        BattleFrame frame;
        frame.tick = tick;
        for (const SimFighter &f : battle.fighters) {
            if (battle.on_field(f)) {
                frame.units.push_back({f.id, f.team, f.attacker, f.x, f.z, f.hp});
            }
        }
        result.frames.push_back(std::move(frame));
    };

    int32_t tick = 0;
    for (; tick < max_ticks; ++tick) {
        // --- Башни бьют ближайшего атакующего в радиусе (SPEC §9.3) ---
        for (SimBuilding &b : battle.buildings) {
            if (!b.alive || !b.def.is_tower) {
                continue;
            }
            b.tower_cd = std::max(0.0f, b.tower_cd - dt);
            if (b.tower_cd > 0.0f) {
                continue;
            }
            const float tx = battle.building_center_x(b);
            const float tz = battle.building_center_z(b);
            int32_t best = -1;
            float best_d = static_cast<float>(cfg.tower_radius);
            for (size_t i = 0; i < battle.fighters.size(); ++i) {
                SimFighter &f = battle.fighters[i];
                if (!battle.on_field(f) || !f.attacker) {
                    continue;
                }
                const float d = dist_to(tx, tz, f.x, f.z);
                if (d <= best_d + 1e-6f && (best < 0 || d < best_d - 1e-6f)) {
                    best_d = d;
                    best = static_cast<int32_t>(i);
                }
            }
            if (best >= 0) {
                const float roll = static_cast<float>(battle.rng.range_double(0.0, 1.0));
                if (roll >= cfg.fighter.dodge) {
                    SimFighter &f = battle.fighters[best];
                    f.hp -= cfg.tower_damage;
                    if (f.hp <= 0) {
                        f.alive = false;
                    }
                }
                b.tower_cd = 1.0f / cfg.tower_rate;
            }
        }

        // --- Ход бойцов в порядке id ---
        for (SimFighter &f : battle.fighters) {
            if (!battle.on_field(f)) {
                continue;
            }
            f.attack_cd = std::max(0.0f, f.attack_cd - dt);

            // Ближайший враг в дальности удара -> атака.
            const int32_t enemy = battle.nearest_enemy(f);
            if (enemy >= 0 && dist(f, battle.fighters[enemy]) <= within) {
                if (f.attack_cd <= 0.0f) {
                    battle.strike_fighter(battle.fighters[enemy]);
                    f.attack_cd = 1.0f / cfg.fighter.attack_rate;
                }
                continue;
            }

            if (f.attacker) {
                const int32_t target = group_target[f.group];
                if (target >= 0 && battle.buildings[target].alive) {
                    const SimBuilding &tb = battle.buildings[target];
                    // Рядом с целью -> бьём здание.
                    if (dist_to(f.x, f.z, battle.building_center_x(tb), battle.building_center_z(tb)) <=
                            (tb.def.size_x + tb.def.size_z) * 0.5f + within) {
                        // Проверка ортогональной смежности к габариту цели.
                        bool adjacent = false;
                        const int32_t fcx = cell_of(f.x);
                        const int32_t fcz = cell_of(f.z);
                        for (const auto &c : battle.adjacent_free(tb.def)) {
                            if (c.first == fcx && c.second == fcz) {
                                adjacent = true;
                            }
                        }
                        if (adjacent) {
                            if (f.attack_cd <= 0.0f) {
                                battle.damage_building(target, cfg.fighter.strength, f.group,
                                        result.destroyed_buildings);
                                f.attack_cd = 1.0f / cfg.fighter.attack_rate;
                            }
                            continue;
                        }
                    }
                }
            }

            // Движение: строим/обновляем путь.
            if (f.path_idx >= f.path.size() || f.repath_timer <= 0) {
                std::vector<std::pair<int32_t, int32_t>> goals;
                if (f.attacker) {
                    const int32_t target = group_target[f.group];
                    if (target >= 0 && battle.buildings[target].alive) {
                        goals = battle.adjacent_free(battle.buildings[target].def);
                    }
                    if (goals.empty() && enemy >= 0) {
                        goals.push_back({cell_of(battle.fighters[enemy].x),
                                cell_of(battle.fighters[enemy].z)});
                    }
                    f.path = find_path(battle.attacker_grid(f.team), cell_of(f.x), cell_of(f.z), goals);
                } else {
                    if (enemy >= 0) {
                        goals.push_back({cell_of(battle.fighters[enemy].x),
                                cell_of(battle.fighters[enemy].z)});
                    }
                    f.path = find_path(battle.defender_grid(), cell_of(f.x), cell_of(f.z), goals);
                }
                f.path_idx = 0;
                f.repath_timer = kRepathTicks;
            }
            --f.repath_timer;

            if (f.path_idx < f.path.size()) {
                const auto next = f.path[f.path_idx];
                // Атакующий: если следующая клетка — стена, ломаем её.
                const int32_t wall = f.attacker ? wall_at(battle, next.first, next.second) : -1;
                if (wall >= 0) {
                    if (f.attack_cd <= 0.0f) {
                        battle.damage_building(wall, cfg.fighter.strength, f.group,
                                result.destroyed_buildings);
                        f.attack_cd = 1.0f / cfg.fighter.attack_rate;
                    }
                    continue;
                }
                const float tx = next.first + 0.5f;
                const float tz = next.second + 0.5f;
                const float d = dist_to(f.x, f.z, tx, tz);
                const float step = cfg.fighter.move_speed * dt;
                if (d <= step) {
                    f.x = tx;
                    f.z = tz;
                    ++f.path_idx;
                } else {
                    f.x += (tx - f.x) / d * step;
                    f.z += (tz - f.z) / d * step;
                }
            }
        }

        // --- Отступление групп, чьи цели разрушены (SPEC §9.3) ---
        for (size_t gi = 0; gi < setup.attackers.size(); ++gi) {
            const int32_t target = group_target[gi];
            if (target >= 0 && !battle.buildings[target].alive && !group_target_destroyed[gi]) {
                group_target_destroyed[gi] = true;
                // Выжившие этой группы немедленно отступают (SPEC §9.3).
                for (SimFighter &f : battle.fighters) {
                    if (battle.on_field(f) && f.attacker && f.group == static_cast<int32_t>(gi)) {
                        f.retreated = true;
                    }
                }
            }
        }

        if (tick % cfg.frame_interval_ticks == 0) {
            record_frame(tick);
        }

        // Конец боя: атакующих на поле не осталось.
        bool any_attacker = false;
        for (const SimFighter &f : battle.fighters) {
            if (battle.on_field(f) && f.attacker) {
                any_attacker = true;
                break;
            }
        }
        if (!any_attacker) {
            ++tick;
            break;
        }
    }
    record_frame(tick);
    result.ticks = tick;

    // --- Итоги: выжившие защитники и группы (отступившие живы) ---
    for (const SimFighter &f : battle.fighters) {
        if (f.alive && !f.attacker) {
            ++result.defender_survivors;
        }
    }
    for (size_t gi = 0; gi < setup.attackers.size(); ++gi) {
        GroupOutcome outcome;
        outcome.team = setup.attackers[gi].team;
        outcome.owner_player = setup.attackers[gi].owner_player;
        outcome.target_destroyed = group_target_destroyed[gi];
        // Награду получает лишь группа, нанёсшая последний удар по цели.
        const int32_t target = group_target[gi];
        outcome.got_reward = group_target_destroyed[gi] && target >= 0 &&
                battle.last_hit_group[target] == static_cast<int32_t>(gi);
        for (const SimFighter &f : battle.fighters) {
            if (f.attacker && f.group == static_cast<int32_t>(gi) && f.alive) {
                ++outcome.survivors; // f.alive охватывает и отступивших
            }
        }
        result.groups.push_back(outcome);
    }
    return result;
}

} // namespace dicecore::systems::combat
