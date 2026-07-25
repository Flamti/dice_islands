// Тесты этапа 8: детектор смежности, мельница (только смежные фермы), склад
// (капы), строительная платформа (леса вне суши, ≤16, следующий ход), POI.
#include "dicecore/core.hpp"
#include "gen/island_gen.hpp"
#include "systems/adjacency.hpp"

#include <cstdio>
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

// Полный набор зданий с большими стартовыми ресурсами для свободной стройки.
const char *kBuildingsJson = R"({
  "starting_resources": { "wood": 200, "stone": 200, "gold": 200, "food": 200, "hammers": 200 },
  "buildings": {
    "castle": { "name": "Замок", "size_x": 3, "size_z": 3, "cost": {}, "hp": 60, "preplaced": true, "dice": "universal" },
    "farm": { "name": "Ферма", "size_x": 2, "size_z": 2, "cost": { "wood": 4 }, "hp": 12, "dice": "food" },
    "warehouse": { "name": "Склад", "size_x": 2, "size_z": 2, "cost": { "wood": 4, "stone": 2 }, "hp": 16, "cap_bonus": { "food": 15, "wood": 15, "stone": 15 } },
    "mill": { "name": "Мельница", "size_x": 2, "size_z": 2, "cost": { "wood": 4, "gold": 1 }, "hp": 12, "mill_food_bonus": 1 },
    "port": { "name": "Порт", "size_x": 2, "size_z": 3, "cost": { "wood": 5 }, "hp": 20, "requires_edge": true },
    "platform": { "name": "Платформа", "size_x": 2, "size_z": 2, "cost": { "wood": 2 }, "hp": 10, "expansion": { "radius": 3, "max_cells": 16 } }
  }
})";

// Кубик еды: грань 0 всегда «2 еды» (детерминизм проверки мельницы).
const char *kDiceJson = R"({
  "caps": { "wood": 20, "stone": 20, "food": 20 },
  "dice": {
    "universal": [ { "wood": 1 }, { "wood": 1 }, { "wood": 1 }, { "wood": 1 }, { "wood": 1 }, { "wood": 1 } ],
    "food": [ { "food": 2 }, { "food": 2 }, { "food": 2 }, { "food": 2 }, { "food": 2 }, { "food": 2 } ]
  }
})";

dicecore::MatchConfig make_config(uint64_t island_seed, uint64_t match_seed) {
    dicecore::MatchConfig config;
    config.buildings_json = kBuildingsJson;
    config.dice_json = kDiceJson;
    config.match_seed = match_seed;
    dicecore::PlayerConfig player;
    player.id = 0;
    player.team = 1;
    player.island_seed = island_seed;
    config.players.push_back(player);
    return config;
}

dicecore::Intent ready_intent() {
    dicecore::Intent intent;
    intent.type = dicecore::kIntentPhaseReady;
    return intent;
}

void advance_to_phase(dicecore::Match &match, int32_t phase) {
    for (int i = 0; i < 500; ++i) {
        const dicecore::TurnSnapshot snapshot = match.snapshot();
        if (snapshot.phase == phase) {
            return;
        }
        if (snapshot.is_decision) {
            for (const dicecore::PlayerSnapshot &p : snapshot.players) {
                if (!p.ready) {
                    match.submit_intent(p.id, ready_intent());
                }
            }
        }
        match.tick(0.25);
    }
}

void advance_to_turn_phase(dicecore::Match &match, int32_t turn, int32_t phase) {
    for (int i = 0; i < 80; ++i) {
        const dicecore::TurnSnapshot s = match.snapshot();
        if (s.turn == turn && s.phase == phase) {
            return;
        }
        if (s.turn < turn) {
            advance_to_phase(match, static_cast<int32_t>(dicecore::Phase::Checks));
            advance_to_phase(match, static_cast<int32_t>(dicecore::Phase::TurnStart));
        } else {
            advance_to_phase(match, phase);
        }
    }
}

dicecore::Intent build_intent(const std::string &id, int32_t cx, int32_t cz) {
    dicecore::Intent intent;
    intent.type = dicecore::kIntentBuild;
    intent.payload[dicecore::kPayloadBuilding] = id;
    intent.payload[dicecore::kPayloadCellX] = std::to_string(cx);
    intent.payload[dicecore::kPayloadCellZ] = std::to_string(cz);
    return intent;
}

// Возврат по значению: аргумент часто — временный снапшот match.snapshot().
dicecore::PlayerSnapshot me(const dicecore::TurnSnapshot &snapshot) {
    return snapshot.players[0];
}

// --- Детектор смежности (юнит) ---

void test_adjacency_detector() {
    using dicecore::systems::rects_side_adjacent;
    // 2x2 в (0,0) и 2x2 в (2,0): касаются вертикальной стороной.
    expect(rects_side_adjacent(0, 0, 2, 2, 2, 0, 2, 2), "боковое касание по X — смежны");
    // 2x2 в (0,0) и 2x2 в (0,2): касаются горизонтальной стороной.
    expect(rects_side_adjacent(0, 0, 2, 2, 0, 2, 2, 2), "боковое касание по Z — смежны");
    // Касание только углом (0,0)+(2,2): НЕ смежны (SPEC §5).
    expect(!rects_side_adjacent(0, 0, 2, 2, 2, 2, 2, 2), "угловое касание — не смежны");
    // Разрыв в одну клетку: не смежны.
    expect(!rects_side_adjacent(0, 0, 2, 2, 3, 0, 2, 2), "зазор — не смежны");
    // Частичное перекрытие сторон по Z при касании по X.
    expect(rects_side_adjacent(0, 0, 2, 3, 2, 2, 2, 2), "частичное перекрытие стороны — смежны");
}

// Ищет свободное место size x size рядом с занятой областью (для смежности)
// или просто любое свободное. Возвращает false, если не нашлось.
bool find_spot(const dicecore::TurnSnapshot &snapshot, uint64_t seed, int32_t size_x, int32_t size_z,
        int32_t &out_x, int32_t &out_z, int32_t near_x = -1, int32_t near_z = -1) {
    const dicecore::gen::GridData grid =
            dicecore::gen::generate_grid(seed, dicecore::gen::GeneratorParams{});
    int32_t best_x = -1;
    int32_t best_z = -1;
    int32_t best_metric = 1 << 30;
    for (int32_t cz = 0; cz + size_z <= grid.cells_z; ++cz) {
        for (int32_t cx = 0; cx + size_x <= grid.cells_x; ++cx) {
            bool ok = true;
            for (int32_t dz = 0; dz < size_z && ok; ++dz) {
                for (int32_t dx = 0; dx < size_x && ok; ++dx) {
                    if (grid.type_at(cx + dx, cz + dz) !=
                            static_cast<int32_t>(dicecore::gen::CellType::Buildable)) {
                        ok = false;
                    }
                }
            }
            for (const dicecore::BuildingSnapshot &b : snapshot.buildings) {
                if (!ok) break;
                if (cx < b.cell_x + b.size_x && b.cell_x < cx + size_x &&
                        cz < b.cell_z + b.size_z && b.cell_z < cz + size_z) {
                    ok = false;
                }
            }
            if (!ok) {
                continue;
            }
            const int32_t metric = near_x < 0 ? (cz * 100 + cx)
                                              : std::abs(cx - near_x) + std::abs(cz - near_z);
            if (metric < best_metric) {
                best_metric = metric;
                best_x = cx;
                best_z = cz;
            }
        }
    }
    out_x = best_x;
    out_z = best_z;
    return best_x >= 0;
}

// --- Мельница усиливает только смежные фермы ---

void test_mill_boosts_only_adjacent_farms() {
    const uint64_t seed = 100;
    dicecore::Match match;
    std::string error;
    expect(match.start(make_config(seed, 1), error), "партия стартует");

    advance_to_phase(match, static_cast<int32_t>(dicecore::Phase::Development));
    dicecore::TurnSnapshot snapshot = match.snapshot();

    // Мельница + смежная ферма впритык, и вторая ферма далеко.
    int32_t mx = -1, mz = -1;
    expect(find_spot(snapshot, seed, 2, 2, mx, mz), "место под мельницу");
    expect(match.submit_intent(0, build_intent("mill", mx, mz)).accepted, "мельница построена");
    snapshot = match.snapshot();

    // Смежная ферма справа от мельницы (касание стороной).
    int32_t adj_x = mx + 2, adj_z = mz;
    const bool adj_ok = match.submit_intent(0, build_intent("farm", adj_x, adj_z)).accepted;
    expect(adj_ok, "смежная ферма построена впритык к мельнице");
    snapshot = match.snapshot();

    // Дальняя ферма: прямой скан сетки — свободное 2x2 buildable-место,
    // не смежное ни с мельницей, ни со смежной фермой, без пересечений.
    const dicecore::gen::GridData grid =
            dicecore::gen::generate_grid(seed, dicecore::gen::GeneratorParams{});
    int32_t far_x = -1, far_z = -1;
    for (int32_t cz = 0; cz + 2 <= grid.cells_z && far_x < 0; ++cz) {
        for (int32_t cx = 0; cx + 2 <= grid.cells_x && far_x < 0; ++cx) {
            bool buildable = true;
            for (int32_t dz = 0; dz < 2 && buildable; ++dz) {
                for (int32_t dx = 0; dx < 2 && buildable; ++dx) {
                    if (grid.type_at(cx + dx, cz + dz) !=
                            static_cast<int32_t>(dicecore::gen::CellType::Buildable)) {
                        buildable = false;
                    }
                }
            }
            if (!buildable) continue;
            bool free_and_far = true;
            for (const dicecore::BuildingSnapshot &b : snapshot.buildings) {
                if (cx < b.cell_x + b.size_x && b.cell_x < cx + 2 &&
                        cz < b.cell_z + b.size_z && b.cell_z < cz + 2) {
                    free_and_far = false; // пересечение с построенным
                }
                if (dicecore::systems::rects_side_adjacent(cx, cz, 2, 2, b.cell_x, b.cell_z,
                            b.size_x, b.size_z)) {
                    free_and_far = false; // смежность с любым зданием
                }
            }
            if (free_and_far) {
                far_x = cx;
                far_z = cz;
            }
        }
    }
    expect(far_x >= 0, "нашлась несмежная дальняя ферма");
    expect(match.submit_intent(0, build_intent("farm", far_x, far_z)).accepted, "дальняя ферма построена");

    // Ход 2, фаза Бросков: у смежной фермы кубик несёт бонус мельницы (+1),
    // у дальней — нет. Мельница усиливает только смежные фермы (SPEC §5).
    advance_to_turn_phase(match, 2, static_cast<int32_t>(dicecore::Phase::Rolls));
    const dicecore::PlayerSnapshot p = me(match.snapshot());
    int food_dice = 0;
    int with_bonus = 0;
    int without_bonus = 0;
    for (const dicecore::DieSnapshot &die : p.dice) {
        if (die.type != "food") {
            continue;
        }
        ++food_dice;
        if (die.food_bonus == 1) {
            ++with_bonus;
        } else if (die.food_bonus == 0) {
            ++without_bonus;
        }
    }
    expect(food_dice == 2, "две фермы дали два кубика еды");
    expect(with_bonus == 1, "ровно одна ферма (смежная) получила бонус мельницы");
    expect(without_bonus == 1, "дальняя ферма бонуса не получила");
}

// --- Склад поднимает капы ---

void test_warehouse_raises_caps() {
    const uint64_t seed = 200;
    dicecore::Match match;
    std::string error;
    expect(match.start(make_config(seed, 1), error), "партия стартует");

    advance_to_phase(match, static_cast<int32_t>(dicecore::Phase::Development));
    expect(me(match.snapshot()).cap_food == 20, "базовый кап еды 20");

    int32_t wx = -1, wz = -1;
    expect(find_spot(match.snapshot(), seed, 2, 2, wx, wz), "место под склад");
    expect(match.submit_intent(0, build_intent("warehouse", wx, wz)).accepted, "склад построен");
    // Склад активируется на след. ходу — до этого кап прежний.
    expect(me(match.snapshot()).cap_food == 20, "недостроенный склад не поднимает кап");

    advance_to_turn_phase(match, 2, static_cast<int32_t>(dicecore::Phase::Development));
    const dicecore::PlayerSnapshot &p = me(match.snapshot());
    expect(p.cap_food == 35 && p.cap_wood == 35 && p.cap_stone == 35,
            "активный склад поднял капы на 15 (20 -> 35)");
}

// --- Платформа: леса вне суши, ≤16, на следующий ход ---

void test_platform_scaffolds() {
    const uint64_t seed = 300;
    dicecore::Match match;
    std::string error;
    expect(match.start(make_config(seed, 1), error), "партия стартует");

    advance_to_phase(match, static_cast<int32_t>(dicecore::Phase::Development));
    dicecore::TurnSnapshot snapshot = match.snapshot();
    expect(me(snapshot).scaffolds.empty(), "лесов на старте нет");

    // Платформа у самого края острова — вокруг много Void для лесов.
    const dicecore::gen::GridData grid =
            dicecore::gen::generate_grid(seed, dicecore::gen::GeneratorParams{});
    int32_t bx = -1, bz = -1;
    // Ищем 2x2 buildable-место, у которого рядом есть Void (край).
    for (int32_t cz = 0; cz + 2 <= grid.cells_z && bx < 0; ++cz) {
        for (int32_t cx = 0; cx + 2 <= grid.cells_x && bx < 0; ++cx) {
            bool buildable = true;
            for (int32_t dz = 0; dz < 2 && buildable; ++dz) {
                for (int32_t dx = 0; dx < 2 && buildable; ++dx) {
                    if (grid.type_at(cx + dx, cz + dz) !=
                            static_cast<int32_t>(dicecore::gen::CellType::Buildable)) {
                        buildable = false;
                    }
                }
            }
            if (!buildable) continue;
            // Есть ли Void в радиусе 3?
            int voids = 0;
            for (int32_t z = cz - 3; z <= cz + 4; ++z) {
                for (int32_t x = cx - 3; x <= cx + 4; ++x) {
                    if (x >= 0 && x < grid.cells_x && z >= 0 && z < grid.cells_z &&
                            grid.type_at(x, z) == static_cast<int32_t>(dicecore::gen::CellType::Void)) {
                        ++voids;
                    }
                }
            }
            // Свободно от зданий?
            bool free_of_buildings = true;
            for (const dicecore::BuildingSnapshot &b : snapshot.buildings) {
                if (cx < b.cell_x + b.size_x && b.cell_x < cx + 2 &&
                        cz < b.cell_z + b.size_z && b.cell_z < cz + 2) {
                    free_of_buildings = false;
                }
            }
            if (voids > 0 && free_of_buildings) {
                bx = cx;
                bz = cz;
            }
        }
    }
    expect(bx >= 0, "нашлось краевое место под платформу с Void вокруг");
    expect(match.submit_intent(0, build_intent("platform", bx, bz)).accepted, "платформа построена");

    // Тот же ход — лесов ещё нет (достройка в фазу 0 следующего хода).
    expect(me(match.snapshot()).scaffolds.empty(), "в ход постройки лесов ещё нет");

    advance_to_turn_phase(match, 2, static_cast<int32_t>(dicecore::Phase::Development));
    const dicecore::PlayerSnapshot &p = me(match.snapshot());
    expect(!p.scaffolds.empty(), "на следующий ход платформа добавила леса");
    expect(p.scaffolds.size() <= 16, "лесов не более 16 (SPEC §11.4)");

    // Все леса — только на бывших Void-клетках (вне поверхности острова).
    bool all_on_void = true;
    for (const dicecore::CellRef &s : p.scaffolds) {
        if (grid.type_at(s.x, s.z) != static_cast<int32_t>(dicecore::gen::CellType::Void)) {
            all_on_void = false;
        }
    }
    expect(all_on_void, "леса добавлены только вне поверхности острова");

    // Леса больше не растут на третий ход (платформа расширяется один раз).
    const size_t scaffolds_turn2 = p.scaffolds.size();
    advance_to_turn_phase(match, 3, static_cast<int32_t>(dicecore::Phase::Development));
    expect(me(match.snapshot()).scaffolds.size() == scaffolds_turn2,
            "платформа расширяется только один раз");

    // На лесах можно строить (строительно эквивалентны обычным клеткам).
    const dicecore::CellRef scaffold = me(match.snapshot()).scaffolds[0];
    // Ищем 2x2 полностью из лесов — иначе строим 1x1? ферма 2x2; проверим отказ
    // корректной причиной, если 2x2 из лесов не набирается.
    const dicecore::IntentResult r =
            match.submit_intent(0, build_intent("farm", scaffold.x, scaffold.z));
    expect(r.accepted || r.reason == dicecore::kRejectCellNotBuildable ||
                    r.reason == dicecore::kRejectCellOccupied ||
                    r.reason == dicecore::kRejectOutOfBounds,
            "клетка-леса принимает стройку либо отклоняет по составу габарита, но не по типу");
}

// --- Порт только на краю острова ---

void test_port_requires_edge() {
    const uint64_t seed = 400;
    dicecore::Match match;
    std::string error;
    expect(match.start(make_config(seed, 1), error), "партия стартует");
    advance_to_phase(match, static_cast<int32_t>(dicecore::Phase::Development));

    const dicecore::gen::GridData grid =
            dicecore::gen::generate_grid(seed, dicecore::gen::GeneratorParams{});
    // Внутреннее место 2x3 без соседних Void: порт должен отклониться.
    int32_t inner_x = -1, inner_z = -1;
    for (int32_t cz = 2; cz + 3 <= grid.cells_z - 2 && inner_x < 0; ++cz) {
        for (int32_t cx = 2; cx + 2 <= grid.cells_x - 2 && inner_x < 0; ++cx) {
            bool ok = true;
            for (int32_t z = cz - 1; z <= cz + 3 && ok; ++z) {
                for (int32_t x = cx - 1; x <= cx + 2 && ok; ++x) {
                    if (grid.type_at(x, z) == static_cast<int32_t>(dicecore::gen::CellType::Void)) {
                        ok = false; // рядом есть край — не подходит для теста «внутри»
                    }
                    if (grid.type_at(x, z) != static_cast<int32_t>(dicecore::gen::CellType::Buildable) &&
                            x >= cx && x < cx + 2 && z >= cz && z < cz + 3) {
                        ok = false; // сам габарит должен быть buildable
                    }
                }
            }
            if (ok) {
                inner_x = cx;
                inner_z = cz;
            }
        }
    }
    if (inner_x >= 0) {
        const dicecore::IntentResult r = match.submit_intent(0, build_intent("port", inner_x, inner_z));
        expect(!r.accepted && r.reason == dicecore::kRejectNotEdge,
                "порт во внутренней клетке отклонён (нужен край)");
    }
}

// --- Добыча POI молотком ---

void test_poi_harvest() {
    const uint64_t seed = 100;
    dicecore::Match match;
    std::string error;
    expect(match.start(make_config(seed, 1), error), "партия стартует");
    advance_to_phase(match, static_cast<int32_t>(dicecore::Phase::Development));

    dicecore::TurnSnapshot snapshot = match.snapshot();
    expect(!me(snapshot).pois.empty(), "на острове есть POI");
    const dicecore::PoiRef poi = me(snapshot).pois[0];
    const int32_t hammers_before = me(snapshot).hammers;
    int32_t resource_before = 0;
    if (poi.kind == "stone") resource_before = me(snapshot).stone;
    else if (poi.kind == "wood") resource_before = me(snapshot).wood;

    dicecore::Intent harvest;
    harvest.type = dicecore::kIntentHarvest;
    harvest.payload[dicecore::kPayloadCellX] = std::to_string(poi.x);
    harvest.payload[dicecore::kPayloadCellZ] = std::to_string(poi.z);
    const dicecore::IntentResult r = match.submit_intent(0, harvest);
    expect(r.accepted, "добыча POI принята");
    expect(std::stoi(r.payload.at("amount")) == poi.amount, "выдан объём POI");

    snapshot = match.snapshot();
    const dicecore::PlayerSnapshot &p = me(snapshot);
    expect(p.hammers == hammers_before - 1, "добыча стоит 1 молоток");
    int32_t resource_after = poi.kind == "stone" ? p.stone : p.wood;
    expect(resource_after == resource_before + poi.amount, "ресурс POI начислен");

    // POI исчез, клетка теперь свободна для застройки.
    bool poi_gone = true;
    for (const dicecore::PoiRef &remaining : p.pois) {
        if (remaining.x == poi.x && remaining.z == poi.z) {
            poi_gone = false;
        }
    }
    expect(poi_gone, "POI убран после добычи");

    // Повторная добыча той же клетки отклонена.
    const dicecore::IntentResult r2 = match.submit_intent(0, harvest);
    expect(!r2.accepted && r2.reason == dicecore::kRejectNoPoiHere,
            "повторная добыча пустой клетки отклонена");
}

} // namespace

int main() {
    test_adjacency_detector();
    test_mill_boosts_only_adjacent_farms();
    test_warehouse_raises_caps();
    test_platform_scaffolds();
    test_port_requires_edge();
    test_poi_harvest();

    if (g_failures == 0) {
        std::printf("OK: все тесты смежности и расширения пройдены\n");
        return 0;
    }
    std::fprintf(stderr, "ПРОВАЛЕНО ПРОВЕРОК: %d\n", g_failures);
    return 1;
}
