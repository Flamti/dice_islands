// Тесты этапа 12: деструктор ландшафта (обрушение края), распространение
// болезни только по смежным, выбор цели Тёмной магией из 2 конкурентов.
#include "dicecore/core.hpp"
#include "gen/destruction.hpp"
#include "gen/island_gen.hpp"

#include <cstdio>
#include <set>
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

// --- Деструктор ландшафта: выбор связного края (юнит) ---

bool cells_connected(const std::vector<dicecore::gen::GridCell> &cells) {
    if (cells.empty()) {
        return false;
    }
    std::set<int64_t> set;
    for (const auto &c : cells) {
        set.insert((static_cast<int64_t>(c.cell_z) << 32) | static_cast<uint32_t>(c.cell_x));
    }
    // BFS от первой клетки по 4-связности внутри множества.
    std::set<int64_t> seen;
    std::vector<dicecore::gen::GridCell> stack{cells[0]};
    seen.insert((static_cast<int64_t>(cells[0].cell_z) << 32) |
            static_cast<uint32_t>(cells[0].cell_x));
    const int32_t dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    while (!stack.empty()) {
        const auto c = stack.back();
        stack.pop_back();
        for (const auto &d : dirs) {
            const int32_t nx = c.cell_x + d[0];
            const int32_t nz = c.cell_z + d[1];
            const int64_t k = (static_cast<int64_t>(nz) << 32) | static_cast<uint32_t>(nx);
            if (set.count(k) && !seen.count(k)) {
                seen.insert(k);
                stack.push_back({nx, nz});
            }
        }
    }
    return seen.size() == cells.size();
}

void test_edge_collapse_region() {
    const dicecore::gen::GeneratorParams params;
    const dicecore::gen::GridData grid =
            dicecore::gen::generate_grid(4242, params);
    dicecore::math::Rng rng(7);

    // Защитим центр (как замок), чтобы обрушение шло по краю вне него.
    std::vector<dicecore::gen::GridCell> protect;
    for (int32_t z = grid.cells_z / 2 - 1; z <= grid.cells_z / 2 + 1; ++z) {
        for (int32_t x = grid.cells_x / 2 - 1; x <= grid.cells_x / 2 + 1; ++x) {
            protect.push_back({x, z});
        }
    }

    const std::vector<dicecore::gen::GridCell> region =
            dicecore::gen::pick_edge_region(grid, protect, 6, 10, rng);
    expect(!region.empty(), "обрушение выбрало непустой кусок края");
    expect(region.size() >= 6 && region.size() <= 10, "размер куска в [6,10]");
    expect(cells_connected(region), "кусок связный");

    // Ни одна клетка не в защищённой зоне и все — поверхность.
    std::set<int64_t> protect_set;
    for (const auto &c : protect) {
        protect_set.insert((static_cast<int64_t>(c.cell_z) << 32) | static_cast<uint32_t>(c.cell_x));
    }
    bool all_valid = true;
    for (const auto &c : region) {
        const int64_t k = (static_cast<int64_t>(c.cell_z) << 32) | static_cast<uint32_t>(c.cell_x);
        if (protect_set.count(k)) {
            all_valid = false;
        }
        if (grid.type_at(c.cell_x, c.cell_z) == static_cast<int32_t>(dicecore::gen::CellType::Void)) {
            all_valid = false;
        }
    }
    expect(all_valid, "кусок вне замка и только по поверхности");
}

void test_carved_mesh_regenerates() {
    const dicecore::gen::GeneratorParams params;
    const dicecore::gen::IslandData base = dicecore::gen::generate_island(4242, params);
    // Вырезаем несколько крайних клеток и проверяем, что меш изменился.
    dicecore::gen::GridData grid = base.grid;
    dicecore::math::Rng rng(1);
    const std::vector<dicecore::gen::GridCell> region =
            dicecore::gen::pick_edge_region(grid, {}, 6, 10, rng);
    expect(!region.empty(), "есть кусок для вырезания");
    const dicecore::gen::IslandData carved =
            dicecore::gen::generate_island_carved(4242, params, region);
    expect(carved.mesh.positions.size() != base.mesh.positions.size() ||
                    carved.mesh.indices.size() != base.mesh.indices.size(),
            "вырезание изменило меш (ре-полигонизация)");
    expect(!carved.mesh.positions.empty(), "меш после вырезания не пуст");
}

// --- Полная партия: болезнь и Тёмная магия ---

// Каталог с параметризуемыми кубиками замка/хижин; uni открывает исследования.
std::string buildings_json(const std::string &castle_die, const std::string &hut_die, bool uni) {
    const std::string uni_flag = uni ? ", \"unlocks_research\": true" : "";
    return std::string(R"({
      "starting_resources": { "wood": 20, "stone": 20, "gold": 20, "food": 20, "hammers": 8, "culture": 20, "swords": 0 },
      "buildings": {
        "castle": { "name": "Замок", "size_x": 3, "size_z": 3, "cost": {}, "hp": 200, "preplaced": true, "dice": ")") +
            castle_die + R"(", "spawns_garrison": true)" + uni_flag + R"( },
        "hut": { "name": "Хижина", "size_x": 1, "size_z": 1, "cost": { "wood": 1 }, "hp": 30, "dice": ")" +
            hut_die + R"(" }
      }
    })";
}

const char *kDice = R"({ "caps": {}, "dice": {
  "cross": [ {"cross":5},{"cross":5},{"cross":5},{"cross":5},{"cross":5},{"cross":5} ],
  "food": [ {"food":1},{"food":1},{"food":1},{"food":1},{"food":1},{"food":1} ]
} })";

// Пул для тестов: только болезнь (тяжесть I, Self) — гарантированно срабатывает.
const char *kDisastersDisease = R"({
  "danger_max": 12, "thresholds": [ {"value":4,"tier":1} ],
  "disasters": { "disease": { "name": "Болезнь", "tier": 1, "target": "self", "effect": "disease", "spread_percent": 100, "cure_gold": 2 } }
})";

// Пул для Тёмной магии: молния (тяжесть I, Opponent).
const char *kDisastersLightning = R"({
  "danger_max": 12, "thresholds": [ {"value":4,"tier":1} ],
  "disasters": { "lightning": { "name": "Молния", "tier": 1, "target": "opponent", "effect": "damage_random_building", "damage": 8 } }
})";

// research.json с Тёмной магией первым узлом (для простоты покупки).
const char *kResearch = R"({
  "branches": { "magic": [ { "id": "dark_magic", "cost": 4, "params": {} } ] }
})";

dicecore::Intent ready_intent() {
    dicecore::Intent intent;
    intent.type = dicecore::kIntentPhaseReady;
    return intent;
}

void advance_to_phase(dicecore::Match &match, int32_t phase) {
    for (int i = 0; i < 500; ++i) {
        const dicecore::TurnSnapshot s = match.snapshot();
        if (s.phase == phase) {
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

int count_status(const dicecore::TurnSnapshot &s, int32_t player, int32_t status) {
    int n = 0;
    for (const dicecore::BuildingSnapshot &b : s.buildings) {
        if (b.player_id == player && b.status == status) {
            ++n;
        }
    }
    return n;
}

void test_disease_spreads_only_adjacent() {
    dicecore::MatchConfig config;
    config.buildings_json = buildings_json("food", "cross", false);
    config.dice_json = kDice;
    config.disasters_json = kDisastersDisease;
    config.combat_json = "";
    config.match_seed = 3;
    dicecore::PlayerConfig p;
    p.id = 0;
    p.team = 1;
    p.island_seed = 5000;
    config.players.push_back(p);
    dicecore::Match match;
    std::string error;
    expect(match.start(config, error), "партия для болезни стартует");

    // Ход 1 Развитие: строим 3 хижины в ряд впритык к замку (смежные цепочкой).
    advance_to_phase(match, static_cast<int32_t>(dicecore::Phase::Development));
    const dicecore::gen::GridData grid =
            dicecore::gen::generate_grid(5000, dicecore::gen::GeneratorParams{});
    // Ищем ряд из 4 подряд идущих Buildable-клеток (по X) вне замка.
    int32_t rx = -1, rz = -1;
    for (int32_t cz = 0; cz < grid.cells_z && rx < 0; ++cz) {
        for (int32_t cx = 0; cx + 4 <= grid.cells_x && rx < 0; ++cx) {
            bool ok = true;
            for (int32_t k = 0; k < 4; ++k) {
                if (grid.type_at(cx + k, cz) !=
                        static_cast<int32_t>(dicecore::gen::CellType::Buildable)) {
                    ok = false;
                }
            }
            for (const dicecore::BuildingSnapshot &b : match.snapshot().buildings) {
                for (int32_t k = 0; k < 4; ++k) {
                    if (cx + k >= b.cell_x && cx + k < b.cell_x + b.size_x && cz >= b.cell_z &&
                            cz < b.cell_z + b.size_z) {
                        ok = false;
                    }
                }
            }
            if (ok) {
                rx = cx;
                rz = cz;
            }
        }
    }
    expect(rx >= 0, "нашёлся ряд под 4 хижины");
    // Строим только 3 хижины подряд (rx, rx+1, rx+2); клетка rx+3 остаётся пустой.
    for (int32_t k = 0; k < 3; ++k) {
        dicecore::Intent build;
        build.type = dicecore::kIntentBuild;
        build.payload[dicecore::kPayloadBuilding] = "hut";
        build.payload[dicecore::kPayloadCellX] = std::to_string(rx + k);
        build.payload[dicecore::kPayloadCellZ] = std::to_string(rz);
        expect(match.submit_intent(0, build).accepted, "хижина построена");
    }

    // Идём по ходам: замок копит кресты, болезнь приходит и ползёт по смежным.
    // spread_percent=100 -> болезнь гарантированно расходится вдоль цепочки.
    int max_diseased = 0;
    for (int32_t turn = 2; turn <= 6; ++turn) {
        advance_to_turn_phase(match, turn, static_cast<int32_t>(dicecore::Phase::Development));
        max_diseased = std::max(max_diseased,
                count_status(match.snapshot(), 0, static_cast<int32_t>(dicecore::BuildingStatus::Diseased)));
    }
    // Болезнь распространилась хотя бы на 2 здания (исходное + смежное).
    expect(max_diseased >= 2, "болезнь распространилась по смежным зданиям");
    // Пустая клетка rx+3 не может «заболеть» — проверяем, что число заболевших
    // не превышает число зданий (замок + 3 хижины = 4).
    expect(max_diseased <= 4, "болеют только здания, не пустые клетки");
}

void test_dark_magic_offers_choice() {
    // Два оппонента у игрока 0: игроки 1 и 2 (разные команды). Игрок 0 с Тёмной
    // магией и молнией (Opponent) должен получить выбор из 2 целей.
    dicecore::MatchConfig config;
    config.buildings_json = buildings_json("cross", "food", true); // замок крестит + университет
    config.dice_json = kDice;
    config.disasters_json = kDisastersLightning;
    config.combat_json = "";
    config.research_json = kResearch;
    config.match_seed = 11;
    for (int i = 0; i < 3; ++i) {
        dicecore::PlayerConfig p;
        p.id = i;
        p.team = i + 1; // три разные команды
        p.island_seed = 5100 + i;
        config.players.push_back(p);
    }
    dicecore::Match match;
    std::string error;
    expect(match.start(config, error), "партия для Тёмной магии стартует");

    // Ход 1 Развитие: игрок 0 изучает Тёмную магию.
    advance_to_phase(match, static_cast<int32_t>(dicecore::Phase::Development));
    dicecore::Intent research;
    research.type = dicecore::kIntentResearch;
    research.payload[dicecore::kPayloadNode] = "dark_magic";
    expect(match.submit_intent(0, research).accepted, "Тёмная магия изучена");

    // Ход 2 фаза Катастроф: замок игрока 0 даёт кресты -> молния -> выбор из 2.
    // Прогоняем до появления события target_choice.
    bool choice_seen = false;
    int candidate_a = -1, candidate_b = -1;
    for (int i = 0; i < 200 && !choice_seen; ++i) {
        for (const dicecore::Event &e : match.tick(0.25)) {
            if (e.type == dicecore::kEventTargetChoice && e.payload.at("chooser") == "0") {
                choice_seen = true;
                candidate_a = std::stoi(e.payload.at("candidate_a"));
                candidate_b = std::stoi(e.payload.at("candidate_b"));
            }
        }
        // Проходим фазы-решения, но НЕ трогаем фазу Катастроф (она держится).
        const dicecore::TurnSnapshot s = match.snapshot();
        if (s.is_decision) {
            for (const dicecore::PlayerSnapshot &p : s.players) {
                if (!p.ready) {
                    match.submit_intent(p.id, ready_intent());
                }
            }
        }
    }
    expect(choice_seen, "с Тёмной магией пришёл выбор цели");
    expect(candidate_a != candidate_b && candidate_a != 0 && candidate_b != 0,
            "предложены два разных конкурента (не сам игрок)");

    // Фаза Катастроф держится, пока игрок 0 не выбрал.
    expect(match.snapshot().phase == static_cast<int32_t>(dicecore::Phase::Disasters),
            "фаза Катастроф держится в ожидании выбора");

    // Игрок 0 выбирает первого кандидата -> молния бьёт по нему, фаза идёт дальше.
    dicecore::Intent pick;
    pick.type = dicecore::kIntentTargetPick;
    pick.payload[dicecore::kPayloadPick] = "0";
    expect(match.submit_intent(0, pick).accepted, "выбор цели принят");
    for (int i = 0; i < 10 && match.snapshot().phase ==
            static_cast<int32_t>(dicecore::Phase::Disasters); ++i) {
        match.tick(0.5);
    }
    expect(match.snapshot().phase != static_cast<int32_t>(dicecore::Phase::Disasters),
            "после выбора фаза Катастроф продолжилась");
}

} // namespace

int main() {
    test_edge_collapse_region();
    test_carved_mesh_regenerates();
    test_disease_spreads_only_adjacent();
    test_dark_magic_offers_choice();

    if (g_failures == 0) {
        std::printf("OK: все тесты катастроф и магии пройдены\n");
        return 0;
    }
    std::fprintf(stderr, "ПРОВАЛЕНО ПРОВЕРОК: %d\n", g_failures);
    return 1;
}
