// Тесты исследований (этап 11): недоступность без университета, покупка узла
// с тратой культуры (= уменьшение счёта победы), последовательность узлов,
// эффект «Фортификация» в бою, эффекты экономики (Логистика).
#include "dicecore/core.hpp"
#include "gen/island_gen.hpp"
#include "systems/combat/battle.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace combat = dicecore::systems::combat;

namespace {

int g_failures = 0;

void expect(bool condition, const char *message) {
    if (!condition) {
        ++g_failures;
        std::fprintf(stderr, "FAIL: %s\n", message);
    }
}

// Каталог с университетом; замок несёт кубик культуры для дохода.
// Вариант «no_uni»: замок без unlocks_research (экран недоступен).
const char *kBuildingsUni = R"({
  "starting_resources": { "wood": 20, "stone": 20, "gold": 20, "food": 20, "hammers": 4, "culture": 20 },
  "buildings": {
    "castle": { "name": "Замок", "size_x": 3, "size_z": 3, "cost": {}, "hp": 60, "preplaced": true, "dice": "culture", "unlocks_research": true, "spawns_garrison": true },
    "warehouse": { "name": "Склад", "size_x": 2, "size_z": 2, "cost": { "wood": 4, "stone": 2 }, "hp": 16, "cap_bonus": { "food": 15, "wood": 15, "stone": 15 } }
  }
})";

const char *kBuildingsNoUni = R"({
  "starting_resources": { "wood": 20, "stone": 20, "gold": 20, "food": 20, "hammers": 4, "culture": 20 },
  "buildings": {
    "castle": { "name": "Замок", "size_x": 3, "size_z": 3, "cost": {}, "hp": 60, "preplaced": true, "dice": "culture", "spawns_garrison": true }
  }
})";

const char *kDice = R"({ "caps": { "food": 20, "wood": 20, "stone": 20 }, "dice": { "culture": [ {"culture":1},{"culture":1},{"culture":1},{"culture":1},{"culture":1},{"culture":1} ] } })";

const char *kResearch = R"({
  "branches": {
    "economy": [
      { "id": "craft", "cost": 4, "params": { "hammers_per_turn": 1 } },
      { "id": "logistics", "cost": 7, "params": { "cap_bonus": 10 } },
      { "id": "engineering", "cost": 12, "params": { "expansion_percent": 50 } }
    ],
    "war": [
      { "id": "tactics", "cost": 4, "params": { "raid_capacity_bonus": 2 } },
      { "id": "fortification", "cost": 7, "params": { "wall_hp_bonus": 15, "tower_damage_percent": 25 } },
      { "id": "strategy", "cost": 12, "params": { "raids_per_turn": 2 } }
    ]
  }
})";

dicecore::MatchConfig make_config(const char *buildings) {
    dicecore::MatchConfig config;
    config.buildings_json = buildings;
    config.dice_json = kDice;
    config.research_json = kResearch;
    config.match_seed = 42;
    dicecore::PlayerConfig p;
    p.id = 0;
    p.team = 1;
    p.island_seed = 1500;
    config.players.push_back(p);
    return config;
}

dicecore::Intent ready_intent() {
    dicecore::Intent intent;
    intent.type = dicecore::kIntentPhaseReady;
    return intent;
}

dicecore::Intent research_intent(const std::string &node) {
    dicecore::Intent intent;
    intent.type = dicecore::kIntentResearch;
    intent.payload[dicecore::kPayloadNode] = node;
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

// Возврат по значению: аргумент часто — временный снапшот match.snapshot().
dicecore::PlayerSnapshot me(const dicecore::TurnSnapshot &s) {
    return s.players[0];
}

bool has_effect(const dicecore::PlayerSnapshot &p, const std::string &effect) {
    for (const std::string &e : p.research) {
        if (e == effect) {
            return true;
        }
    }
    return false;
}

void test_no_university_blocks() {
    dicecore::Match match;
    std::string error;
    expect(match.start(make_config(kBuildingsNoUni), error), "партия без университета стартует");
    advance_to_phase(match, static_cast<int32_t>(dicecore::Phase::Development));
    expect(!me(match.snapshot()).research_available, "без университета экран недоступен");
    const dicecore::IntentResult r = match.submit_intent(0, research_intent("craft"));
    expect(!r.accepted && r.reason == dicecore::kRejectNoUniversity,
            "исследование без активного университета отклонено");
}

void test_buy_and_culture_cost() {
    dicecore::Match match;
    std::string error;
    expect(match.start(make_config(kBuildingsUni), error), "партия с университетом стартует");
    advance_to_phase(match, static_cast<int32_t>(dicecore::Phase::Development));
    expect(me(match.snapshot()).research_available, "с активным университетом экран доступен");

    const int32_t culture_before = me(match.snapshot()).culture;
    // Узел 2 ветки заблокирован, пока не куплен узел 1.
    dicecore::IntentResult r = match.submit_intent(0, research_intent("logistics"));
    expect(!r.accepted && r.reason == dicecore::kRejectNodeLocked,
            "второй узел ветки заблокирован без первого");

    // Покупка «Ремесло» (4 культуры): культура и счёт победы падают на 4.
    r = match.submit_intent(0, research_intent("craft"));
    expect(r.accepted, "покупка Ремесла принята");
    expect(me(match.snapshot()).culture == culture_before - 4,
            "культура (и счёт победы) уменьшились на стоимость узла");
    expect(has_effect(me(match.snapshot()), "craft"), "Ремесло изучено");

    // Повторная покупка отклонена.
    r = match.submit_intent(0, research_intent("craft"));
    expect(!r.accepted && r.reason == dicecore::kRejectAlreadyResearched, "повторная покупка отклонена");

    // Теперь Логистика открыта.
    r = match.submit_intent(0, research_intent("logistics"));
    expect(r.accepted, "Логистика открыта после Ремесла");

    // Нехватка культуры на дорогой узел (Инженерия 12; осталось 20-4-7=9).
    r = match.submit_intent(0, research_intent("engineering"));
    expect(!r.accepted && r.reason == dicecore::kRejectNotEnoughCulture,
            "нет культуры на дорогой узел");
}

void test_logistics_raises_caps() {
    dicecore::Match match;
    std::string error;
    expect(match.start(make_config(kBuildingsUni), error), "партия стартует");
    advance_to_phase(match, static_cast<int32_t>(dicecore::Phase::Development));
    expect(me(match.snapshot()).cap_food == 20, "базовый кап еды 20");

    // Строим склад (+15 к капам) и исследуем Ремесло -> Логистика (+10 к складам).
    const dicecore::gen::GridData grid =
            dicecore::gen::generate_grid(1500, dicecore::gen::GeneratorParams{});
    int32_t wx = -1, wz = -1;
    for (int32_t cz = 0; cz + 2 <= grid.cells_z && wx < 0; ++cz) {
        for (int32_t cx = 0; cx + 2 <= grid.cells_x && wx < 0; ++cx) {
            bool ok = true;
            for (int32_t dz = 0; dz < 2 && ok; ++dz) {
                for (int32_t dx = 0; dx < 2 && ok; ++dx) {
                    if (grid.type_at(cx + dx, cz + dz) !=
                            static_cast<int32_t>(dicecore::gen::CellType::Buildable)) {
                        ok = false;
                    }
                }
            }
            for (const dicecore::BuildingSnapshot &b : match.snapshot().buildings) {
                if (b.player_id == 0 && cx < b.cell_x + b.size_x && b.cell_x < cx + 2 &&
                        cz < b.cell_z + b.size_z && b.cell_z < cz + 2) {
                    ok = false;
                }
            }
            if (ok) {
                wx = cx;
                wz = cz;
            }
        }
    }
    expect(wx >= 0, "нашлось место под склад");
    dicecore::Intent build;
    build.type = dicecore::kIntentBuild;
    build.payload[dicecore::kPayloadBuilding] = "warehouse";
    build.payload[dicecore::kPayloadCellX] = std::to_string(wx);
    build.payload[dicecore::kPayloadCellZ] = std::to_string(wz);
    expect(match.submit_intent(0, build).accepted, "склад построен");
    expect(match.submit_intent(0, research_intent("craft")).accepted, "Ремесло куплено");
    expect(match.submit_intent(0, research_intent("logistics")).accepted, "Логистика куплена");

    // Ход 2: склад активен. Кап = 20 (база) + 15 (склад) + 10 (Логистика) = 45.
    advance_to_phase(match, static_cast<int32_t>(dicecore::Phase::Checks));
    advance_to_phase(match, static_cast<int32_t>(dicecore::Phase::Development));
    expect(me(match.snapshot()).cap_food == 45,
            "активный склад + Логистика подняли кап (20+15+10=45)");
}

// Фортификация: сравниваем два боя — стена с бонусом HP выживает там, где
// базовая ломается при фиксированном бюджете атакующих.
void test_fortification_in_combat() {
    combat::CombatConfig cfg;
    std::string err;
    // Малый бюджет атакующих: 1 боец, короткий таймаут — стену едва хватает.
    const char *combat_json = R"({
      "tick_rate": 20, "timeout_sec": 8, "reward_percent": 30, "frame_interval_ticks": 4,
      "wall_defense_armor_bonus": 1, "wall_detour_factor": 1.0, "raid_capacity": 8, "raids_per_turn": 1,
      "fighter": { "hp": 10, "strength": 2, "attack_rate": 1.0, "armor": 0, "dodge": 0.0, "range": 1, "move_speed": 4.0 },
      "tower": { "radius": 4, "damage": 3, "rate": 0.8 }, "pirate": { "count": 6 }
    })";
    expect(combat::parse_combat_json(combat_json, cfg, err), "combat.json разобран");

    // Коридор: суша по средней строке, стена и цель на пути.
    auto make_setup = [&](int32_t wall_hp) {
        combat::BattleSetup setup;
        setup.cells_x = 10;
        setup.cells_z = 3;
        setup.walkable.assign(30, 0);
        for (int32_t x = 0; x < 10; ++x) {
            setup.walkable[1 * 10 + x] = 1;
        }
        setup.config = cfg;
        combat::BattleBuilding target;
        target.id = 1;
        target.cell_x = 8;
        target.cell_z = 1;
        target.hp = 12;
        setup.buildings.push_back(target);
        combat::BattleBuilding wall;
        wall.id = 2;
        wall.cell_x = 4;
        wall.cell_z = 1;
        wall.hp = wall_hp;
        wall.is_wall = true;
        setup.buildings.push_back(wall);
        combat::AttackerGroup g;
        g.team = combat::kPirateTeam;
        g.count = 1;
        g.landing_side = 3;
        g.target_building = 0;
        setup.attackers.push_back(g);
        return setup;
    };

    // Базовая стена HP 30: за 8 с один боец (2 урона/с) успевает сломать
    // (30/2=15с? нет — не успевает). Подберём так, чтобы разница проявилась:
    // база 12 HP -> ломается за ~6с, доходит до цели; +15 = 27 HP -> не успеть.
    const combat::BattleResult base = combat::simulate_battle(make_setup(12), 1);
    const combat::BattleResult fort = combat::simulate_battle(make_setup(12 + 15), 1);

    bool base_wall_broken = false;
    bool fort_wall_broken = false;
    for (int32_t id : base.destroyed_buildings) {
        if (id == 2) base_wall_broken = true;
    }
    for (int32_t id : fort.destroyed_buildings) {
        if (id == 2) fort_wall_broken = true;
    }
    expect(base_wall_broken, "базовую стену боец успевает сломать");
    expect(!fort_wall_broken, "укреплённая стена (+15 HP) выдерживает тот же натиск");
}

// Башня с бонусом урона убивает быстрее (Фортификация, +25% урона).
void test_fortified_tower_damage() {
    combat::CombatConfig cfg;
    std::string err;
    const char *combat_json = R"({
      "tick_rate": 20, "timeout_sec": 30, "reward_percent": 30, "frame_interval_ticks": 4,
      "wall_defense_armor_bonus": 0, "wall_detour_factor": 1.0, "raid_capacity": 8, "raids_per_turn": 1,
      "fighter": { "hp": 10, "strength": 1, "attack_rate": 1.0, "armor": 0, "dodge": 0.0, "range": 1, "move_speed": 1.0 },
      "tower": { "radius": 6, "damage": 4, "rate": 1.0 }, "pirate": { "count": 3 }
    })";
    expect(combat::parse_combat_json(combat_json, cfg, err), "combat.json разобран");

    auto make_setup = [&](int32_t tower_dmg) {
        combat::BattleSetup setup;
        setup.cells_x = 12;
        setup.cells_z = 12;
        setup.walkable.assign(144, 1);
        setup.config = cfg;
        combat::BattleBuilding castle;
        castle.id = 1;
        castle.cell_x = 5;
        castle.cell_z = 5;
        castle.size_x = 2;
        castle.size_z = 2;
        castle.hp = 200; // прочный: важно, сколько атакующих убьёт башня
        setup.buildings.push_back(castle);
        combat::BattleBuilding tower;
        tower.id = 2;
        tower.cell_x = 5;
        tower.cell_z = 7;
        tower.hp = 24;
        tower.is_tower = true;
        tower.tower_damage = tower_dmg; // 0 — базовый урон
        setup.buildings.push_back(tower);
        combat::AttackerGroup g;
        g.team = combat::kPirateTeam;
        g.count = 3;
        g.landing_side = 0;
        g.target_building = 0;
        setup.attackers.push_back(g);
        return setup;
    };

    const combat::BattleResult base = combat::simulate_battle(make_setup(0), 5);
    const combat::BattleResult fort = combat::simulate_battle(make_setup(5), 5); // +25% от 4
    expect(fort.groups[0].survivors <= base.groups[0].survivors,
            "усиленная башня выбивает не меньше атакующих");
    expect(fort.groups[0].survivors < 3 || base.groups[0].survivors < 3,
            "башня вообще наносит потери");
}

void test_bad_config() {
    dicecore::Match match;
    std::string error;
    dicecore::MatchConfig config = make_config(kBuildingsUni);
    config.research_json = "{ мусор";
    expect(!match.start(config, error) && error == dicecore::kErrorBadResearchConfig,
            "битый research.json отклонён");
}

} // namespace

int main() {
    test_no_university_blocks();
    test_buy_and_culture_cost();
    test_logistics_raises_caps();
    test_fortification_in_combat();
    test_fortified_tower_damage();
    test_bad_config();

    if (g_failures == 0) {
        std::printf("OK: все тесты исследований пройдены\n");
        return 0;
    }
    std::fprintf(stderr, "ПРОВАЛЕНО ПРОВЕРОК: %d\n", g_failures);
    return 1;
}
