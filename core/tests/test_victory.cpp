// Тесты победы/поражения/Чуда (этап 13): военная, культурная, чудо-победа,
// приоритет одновременных, выбывание игрока, наблюдение выбывшего.
#include "dicecore/core.hpp"
#include "gen/island_gen.hpp"

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

// Замок открывает исследования/рейды; много ресурсов и мечей для сценариев.
const char *kBuildings = R"({
  "starting_resources": { "wood": 200, "stone": 200, "gold": 200, "food": 200, "hammers": 200, "swords": 20, "culture": 0 },
  "buildings": {
    "castle": { "name": "Замок", "size_x": 3, "size_z": 3, "cost": {}, "hp": 30, "preplaced": true, "dice": "gold", "unlocks_raids": true, "spawns_garrison": false },
    "wonder": { "name": "Чудо", "size_x": 4, "size_z": 4, "cost": { "wood": 5, "stone": 5, "gold": 5 }, "hammers": 1, "hp": 80, "wonder_stages": 3 }
  }
})";

const char *kDice = R"({ "caps": {}, "dice": { "gold": [ {"gold":1},{"gold":1},{"gold":1},{"gold":1},{"gold":1},{"gold":1} ] } })";

// Решающая атака, чтобы рейд гарантированно разрушал замок.
const char *kCombat = R"({
  "tick_rate": 20, "timeout_sec": 20, "reward_percent": 30, "frame_interval_ticks": 4,
  "wall_defense_armor_bonus": 0, "wall_detour_factor": 1.0, "raid_capacity": 20, "raids_per_turn": 1,
  "fighter": { "hp": 10, "strength": 50, "attack_rate": 2.0, "armor": 0, "dodge": 0.0, "range": 1, "move_speed": 6.0 },
  "tower": { "radius": 4, "damage": 3, "rate": 0.8 }, "pirate": { "count": 6 }
})";

dicecore::MatchConfig base_config(int players, bool separate_teams) {
    dicecore::MatchConfig config;
    config.buildings_json = kBuildings;
    config.dice_json = kDice;
    config.combat_json = kCombat;
    config.match_seed = 77;
    for (int i = 0; i < players; ++i) {
        dicecore::PlayerConfig p;
        p.id = i;
        p.team = separate_teams ? (i + 1) : 1;
        p.island_seed = 6000 + i;
        config.players.push_back(p);
    }
    return config;
}

dicecore::Intent ready_intent() {
    dicecore::Intent intent;
    intent.type = dicecore::kIntentPhaseReady;
    return intent;
}

void advance_to_phase(dicecore::Match &match, int32_t phase) {
    for (int i = 0; i < 500; ++i) {
        const dicecore::TurnSnapshot s = match.snapshot();
        if (s.finished || s.phase == phase) {
            return;
        }
        if (s.is_decision) {
            for (const dicecore::PlayerSnapshot &p : s.players) {
                if (!p.ready) {
                    match.submit_intent(p.id, ready_intent());
                }
            }
        }
        match.tick(0.25);
    }
}

// Прокрутка партии до завершения (или предела ходов).
void run_until_finished(dicecore::Match &match, int max_ticks = 4000) {
    for (int i = 0; i < max_ticks && !match.snapshot().finished; ++i) {
        const dicecore::TurnSnapshot s = match.snapshot();
        if (s.is_decision) {
            for (const dicecore::PlayerSnapshot &p : s.players) {
                if (!p.ready) {
                    match.submit_intent(p.id, ready_intent());
                }
            }
        }
        match.tick(0.5);
    }
}

int32_t castle_of(const dicecore::TurnSnapshot &s, int32_t player) {
    for (const dicecore::BuildingSnapshot &b : s.buildings) {
        if (b.player_id == player && b.type == "castle") {
            return static_cast<int32_t>(b.id);
        }
    }
    return -1;
}

const dicecore::PlayerSnapshot *player_of(const dicecore::TurnSnapshot &s, int32_t id) {
    for (const dicecore::PlayerSnapshot &p : s.players) {
        if (p.id == id) {
            return &p;
        }
    }
    return nullptr;
}

// --- Военная победа: рейд разрушает единственный вражеский замок ---

void test_military_victory_and_elimination() {
    dicecore::Match match;
    std::string error;
    expect(match.start(base_config(2, true), error), "партия 2 команд стартует");

    // Ход 1 Набеги: игрок 0 отправляет решающий рейд на замок игрока 1.
    advance_to_phase(match, static_cast<int32_t>(dicecore::Phase::Raids));
    const int32_t target = castle_of(match.snapshot(), 1);
    dicecore::Intent r;
    r.type = dicecore::kIntentRaid;
    r.payload[dicecore::kPayloadTargetPlayer] = "1";
    r.payload[dicecore::kPayloadTargetBuilding] = std::to_string(target);
    r.payload[dicecore::kPayloadCount] = "20";
    r.payload[dicecore::kPayloadSide] = "0";
    expect(match.submit_intent(0, r).accepted, "рейд на замок принят");

    run_until_finished(match);
    const dicecore::TurnSnapshot s = match.snapshot();
    expect(s.finished, "партия завершилась");
    expect(s.winner_team == 1, "победила команда игрока 0 (военная)");
    // Игрок 1 выбыл (замок разрушен), наблюдает.
    const dicecore::PlayerSnapshot *loser = player_of(s, 1);
    expect(loser != nullptr && !loser->alive, "проигравший выбыл (наблюдатель)");
}

// --- Культурная победа: >=2x и >=20 ---

void test_cultural_victory() {
    // Три игрока: команда 1 = {0, 1} (2 игрока), команда 2 = {2}. Замок даёт
    // кубик культуры -> команда 1 копит вдвое быстрее и первой берёт 2x и >=20.
    dicecore::MatchConfig config;
    config.dice_json = R"({ "caps": {}, "dice": { "culture": [ {"culture":1},{"culture":1},{"culture":1},{"culture":1},{"culture":1},{"culture":1} ] } })";
    config.combat_json = kCombat;
    config.match_seed = 9;
    config.buildings_json = R"({
      "starting_resources": { "wood": 20, "stone": 20, "gold": 20, "food": 20, "hammers": 8, "swords": 0, "culture": 0 },
      "buildings": {
        "castle": { "name": "Замок", "size_x": 3, "size_z": 3, "cost": {}, "hp": 30, "preplaced": true, "dice": "culture", "spawns_garrison": false }
      }
    })";
    const int32_t teams[3] = {1, 1, 2};
    for (int i = 0; i < 3; ++i) {
        dicecore::PlayerConfig p;
        p.id = i;
        p.team = teams[i];
        p.island_seed = 6200 + i;
        config.players.push_back(p);
    }
    dicecore::Match match;
    std::string error;
    expect(match.start(config, error), "партия для культурной победы стартует");

    run_until_finished(match);
    const dicecore::TurnSnapshot s = match.snapshot();
    expect(s.finished, "культурная партия завершилась");
    expect(s.winner_team == 1, "победила команда с большей культурой (2 игрока)");
    // Победа по культуре: у команды 1 суммарно >= 20 и >= 2x команды 2.
    int64_t c1 = 0, c2 = 0;
    for (const dicecore::PlayerSnapshot &p : s.players) {
        if (p.team == 1) c1 += p.culture;
        else c2 += p.culture;
    }
    expect(c1 >= 20 && c1 >= 2 * c2, "условие культурной победы выполнено");
}

// --- Чудо: 3 этапа + удержание 1 ход ---

void test_wonder_victory() {
    dicecore::Match match;
    std::string error;
    // Одна команда из 1 игрока — некому мешать: строим Чудо и держим ход.
    dicecore::MatchConfig config = base_config(1, true);
    expect(match.start(config, error), "партия для Чуда стартует");

    // Строим Чудо (этап 1) и достраиваем этапы 2, 3 за один ход Развития.
    advance_to_phase(match, static_cast<int32_t>(dicecore::Phase::Development));
    const dicecore::gen::GridData grid =
            dicecore::gen::generate_grid(6000, dicecore::gen::GeneratorParams{});
    int32_t wx = -1, wz = -1;
    for (int32_t cz = 0; cz + 4 <= grid.cells_z && wx < 0; ++cz) {
        for (int32_t cx = 0; cx + 4 <= grid.cells_x && wx < 0; ++cx) {
            bool ok = true;
            for (int32_t dz = 0; dz < 4 && ok; ++dz) {
                for (int32_t dx = 0; dx < 4 && ok; ++dx) {
                    if (grid.type_at(cx + dx, cz + dz) !=
                            static_cast<int32_t>(dicecore::gen::CellType::Buildable)) {
                        ok = false;
                    }
                }
            }
            for (const dicecore::BuildingSnapshot &b : match.snapshot().buildings) {
                if (cx < b.cell_x + b.size_x && b.cell_x < cx + 4 && cz < b.cell_z + b.size_z &&
                        b.cell_z < cz + 4) {
                    ok = false;
                }
            }
            if (ok) {
                wx = cx;
                wz = cz;
            }
        }
    }
    expect(wx >= 0, "нашлось место 4x4 под Чудо");
    dicecore::Intent build;
    build.type = dicecore::kIntentBuild;
    build.payload[dicecore::kPayloadBuilding] = "wonder";
    build.payload[dicecore::kPayloadCellX] = std::to_string(wx);
    build.payload[dicecore::kPayloadCellZ] = std::to_string(wz);
    expect(match.submit_intent(0, build).accepted, "этап 1 Чуда заложен");
    // Этапы 2 и 3 — повторная постройка «wonder» продвигает этап.
    dicecore::Intent advance;
    advance.type = dicecore::kIntentBuild;
    advance.payload[dicecore::kPayloadBuilding] = "wonder";
    advance.payload[dicecore::kPayloadCellX] = std::to_string(wx);
    advance.payload[dicecore::kPayloadCellZ] = std::to_string(wz);
    const dicecore::IntentResult r2 = match.submit_intent(0, advance);
    expect(r2.accepted && r2.payload.at("stage") == "2", "этап 2 достроен");
    const dicecore::IntentResult r3 = match.submit_intent(0, advance);
    expect(r3.accepted && r3.payload.at("stage") == "3", "этап 3 достроен (Чудо готово)");

    // В этот же ход победы ещё нет (нужно выстоять 1 ход).
    advance_to_phase(match, static_cast<int32_t>(dicecore::Phase::Checks));
    expect(!match.snapshot().finished, "в ход завершения Чуда победы ещё нет");

    // Следующий ход: Чудо выстояло -> чудо-победа.
    run_until_finished(match);
    const dicecore::TurnSnapshot s = match.snapshot();
    expect(s.finished && s.winner_team == 1, "Чудо простояло ход -> победа");
}

// --- Приоритет: Чудо -> Военная -> Культурная ---

void test_priority_wonder_over_others() {
    // Команда 1 (игрок 0) держит выстоявшее Чудо; команда 2 (игрок 1) имеет
    // огромную культуру. Одновременно в фазе Проверок должна победить Чудо.
    // Настроим стартовую культуру игроку 1 через отдельный каталог.
    dicecore::MatchConfig config;
    config.dice_json = kDice;
    config.combat_json = kCombat;
    config.match_seed = 5;
    config.buildings_json = R"({
      "starting_resources": { "wood": 200, "stone": 200, "gold": 200, "food": 200, "hammers": 200, "swords": 0, "culture": 100 },
      "buildings": {
        "castle": { "name": "Замок", "size_x": 3, "size_z": 3, "cost": {}, "hp": 30, "preplaced": true, "dice": "gold", "spawns_garrison": false },
        "wonder": { "name": "Чудо", "size_x": 4, "size_z": 4, "cost": { "wood": 5, "stone": 5, "gold": 5 }, "hammers": 1, "hp": 80, "wonder_stages": 3 }
      }
    })";
    for (int i = 0; i < 2; ++i) {
        dicecore::PlayerConfig p;
        p.id = i;
        p.team = i + 1;
        p.island_seed = 6100 + i;
        config.players.push_back(p);
    }
    dicecore::Match match;
    std::string error;
    expect(match.start(config, error), "партия приоритета стартует");
    // Обе команды стартуют со 100 культуры -> ни у кого нет 2x, культурной
    // победы нет сразу. Игрок 0 строит Чудо и достраивает за ход.
    advance_to_phase(match, static_cast<int32_t>(dicecore::Phase::Development));
    const dicecore::gen::GridData grid =
            dicecore::gen::generate_grid(6100, dicecore::gen::GeneratorParams{});
    int32_t wx = -1, wz = -1;
    for (int32_t cz = 0; cz + 4 <= grid.cells_z && wx < 0; ++cz) {
        for (int32_t cx = 0; cx + 4 <= grid.cells_x && wx < 0; ++cx) {
            bool ok = true;
            for (int32_t dz = 0; dz < 4 && ok; ++dz) {
                for (int32_t dx = 0; dx < 4 && ok; ++dx) {
                    if (grid.type_at(cx + dx, cz + dz) !=
                            static_cast<int32_t>(dicecore::gen::CellType::Buildable)) {
                        ok = false;
                    }
                }
            }
            for (const dicecore::BuildingSnapshot &b : match.snapshot().buildings) {
                if (b.player_id == 0 && cx < b.cell_x + b.size_x && b.cell_x < cx + 4 &&
                        cz < b.cell_z + b.size_z && b.cell_z < cz + 4) {
                    ok = false;
                }
            }
            if (ok) {
                wx = cx;
                wz = cz;
            }
        }
    }
    expect(wx >= 0, "место под Чудо у игрока 0");
    for (int stage = 0; stage < 3; ++stage) {
        dicecore::Intent b;
        b.type = dicecore::kIntentBuild;
        b.payload[dicecore::kPayloadBuilding] = "wonder";
        b.payload[dicecore::kPayloadCellX] = std::to_string(wx);
        b.payload[dicecore::kPayloadCellZ] = std::to_string(wz);
        expect(match.submit_intent(0, b).accepted, "этап Чуда достроен");
    }

    run_until_finished(match);
    const dicecore::TurnSnapshot s = match.snapshot();
    expect(s.finished && s.winner_team == 1, "Чудо приоритетнее культуры -> команда 1");
}

} // namespace

int main() {
    test_military_victory_and_elimination();
    test_cultural_victory();
    test_wonder_victory();
    test_priority_wonder_over_others();

    if (g_failures == 0) {
        std::printf("OK: все тесты победы пройдены\n");
        return 0;
    }
    std::fprintf(stderr, "ПРОВАЛЕНО ПРОВЕРОК: %d\n", g_failures);
    return 1;
}
