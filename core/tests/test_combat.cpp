// Тесты боевого ядра (этап 9): детерминизм (один сид — один лог), слом стены
// при дорогом обходе, гибель атакующих под плотной обороной башен, парсер,
// интеграция «Нашествие пиратов» через полную партию.
#include "dicecore/core.hpp"
#include "systems/combat/battle.hpp"

#include <cstdio>
#include <set>
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

const char *kCombatJson = R"({
  "tick_rate": 20, "timeout_sec": 90, "reward_percent": 30, "frame_interval_ticks": 4,
  "wall_defense_armor_bonus": 1, "wall_detour_factor": 1.0,
  "fighter": { "hp": 10, "strength": 2, "attack_rate": 1.0, "armor": 0, "dodge": 0.05, "range": 1, "move_speed": 2.0 },
  "tower": { "radius": 4, "damage": 3, "rate": 0.8 },
  "pirate": { "count": 6 }
})";

combat::CombatConfig load_config() {
    combat::CombatConfig config;
    std::string error;
    if (!combat::parse_combat_json(kCombatJson, config, error)) {
        std::fprintf(stderr, "не удалось разобрать combat.json: %s\n", error.c_str());
    }
    return config;
}

// Пустое поле cells_x*cells_z, вся поверхность проходима.
combat::BattleSetup make_open_field(int32_t cells, const combat::CombatConfig &config) {
    combat::BattleSetup setup;
    setup.cells_x = cells;
    setup.cells_z = cells;
    setup.walkable.assign(static_cast<size_t>(cells) * cells, 1);
    setup.config = config;
    setup.defender_team = 1;
    setup.defender_player = 0;
    return setup;
}

// Сравнение логов боёв на идентичность (детерминизм).
bool frames_identical(const combat::BattleResult &a, const combat::BattleResult &b) {
    if (a.ticks != b.ticks || a.frames.size() != b.frames.size() ||
            a.defender_survivors != b.defender_survivors) {
        return false;
    }
    for (size_t i = 0; i < a.frames.size(); ++i) {
        if (a.frames[i].tick != b.frames[i].tick ||
                a.frames[i].units.size() != b.frames[i].units.size()) {
            return false;
        }
        for (size_t j = 0; j < a.frames[i].units.size(); ++j) {
            const auto &ua = a.frames[i].units[j];
            const auto &ub = b.frames[i].units[j];
            if (ua.id != ub.id || ua.hp != ub.hp || ua.x != ub.x || ua.z != ub.z) {
                return false;
            }
        }
    }
    return true;
}

// --- Детерминизм ---

void test_determinism() {
    const combat::CombatConfig config = load_config();
    combat::BattleSetup setup = make_open_field(12, config);
    // Цель — «ферма» 2x2 в центре, защитник с гарнизоном.
    combat::BattleBuilding farm;
    farm.id = 1;
    farm.cell_x = 5;
    farm.cell_z = 5;
    farm.size_x = 2;
    farm.size_z = 2;
    farm.hp = 40; // прочнее, чтобы бой шёл дольше и уворот успел проявиться
    farm.spawns_garrison = true; // защитники спавнятся и вступают в бой
    setup.buildings.push_back(farm);
    setup.defender_garrison = 4;
    combat::AttackerGroup group;
    group.team = combat::kPirateTeam;
    group.count = 6;
    group.landing_side = 0;
    group.target_building = 0;
    setup.attackers.push_back(group);

    const combat::BattleResult a = combat::simulate_battle(setup, 12345);
    const combat::BattleResult b = combat::simulate_battle(setup, 12345);
    expect(frames_identical(a, b), "один сид — идентичный лог боя");
    expect(!a.frames.empty(), "лог боя не пуст");

    // RNG влияет на бой: по набору сидов встречаются разные исходы.
    std::set<int32_t> outcomes;
    for (uint64_t seed = 1; seed <= 16; ++seed) {
        const combat::BattleResult r = combat::simulate_battle(setup, seed);
        outcomes.insert(r.ticks * 100 + r.defender_survivors);
    }
    expect(outcomes.size() >= 2, "уворот (RNG) даёт разные исходы по сидам");
}

// --- Гибель атакующих под плотной обороной башен ---

void test_towers_kill_attackers() {
    const combat::CombatConfig config = load_config();
    combat::BattleSetup setup = make_open_field(14, config);
    // Цель-замок в центре, окружённый кольцом башен: пираты не дойдут.
    combat::BattleBuilding castle;
    castle.id = 1;
    castle.cell_x = 6;
    castle.cell_z = 6;
    castle.size_x = 2;
    castle.size_z = 2;
    castle.hp = 60;
    castle.spawns_garrison = true;
    setup.buildings.push_back(castle);

    int32_t tower_id = 100;
    for (int32_t z = 3; z <= 11; z += 2) {
        for (int32_t x = 3; x <= 11; x += 2) {
            if (x >= 5 && x <= 8 && z >= 5 && z <= 8) {
                continue; // не ставим на замок
            }
            combat::BattleBuilding tower;
            tower.id = tower_id++;
            tower.cell_x = x;
            tower.cell_z = z;
            tower.size_x = 1;
            tower.size_z = 1;
            tower.hp = 24;
            tower.is_tower = true;
            setup.buildings.push_back(tower);
        }
    }
    setup.defender_garrison = 8;
    combat::AttackerGroup group;
    group.team = combat::kPirateTeam;
    group.count = 6;
    group.landing_side = 0;
    group.target_building = 0;
    setup.attackers.push_back(group);

    const combat::BattleResult result = combat::simulate_battle(setup, 7);
    expect(!result.groups.empty(), "есть исход группы");
    expect(result.groups[0].survivors < 6, "плотная оборона выбивает часть/всех пиратов");
    expect(!result.groups[0].target_destroyed, "пираты не разрушили замок сквозь башни");
    // Замок не в списке разрушенных.
    bool castle_destroyed = false;
    for (int32_t id : result.destroyed_buildings) {
        if (id == 1) {
            castle_destroyed = true;
        }
    }
    expect(!castle_destroyed, "замок уцелел под защитой башен");
}

// --- Слом стены, когда обход дороже ---

void test_wall_broken_when_detour_expensive() {
    const combat::CombatConfig config = load_config();
    // Узкий коридор: остров-полоса шириной 3 клетки, длинной 12.
    // Проходима только средняя строка (z=1); z=0 и z=2 — вода. Стена
    // посреди коридора: обойти нельзя вовсе, значит бойцы её ломают.
    combat::BattleSetup setup;
    setup.cells_x = 12;
    setup.cells_z = 3;
    setup.walkable.assign(static_cast<size_t>(setup.cells_x) * setup.cells_z, 0);
    for (int32_t x = 0; x < setup.cells_x; ++x) {
        setup.walkable[1 * setup.cells_x + x] = 1; // средняя строка — суша
    }
    setup.config = config;
    setup.defender_team = 1;
    setup.defender_player = 0;

    // Цель на дальнем конце коридора.
    combat::BattleBuilding target;
    target.id = 1;
    target.cell_x = 10;
    target.cell_z = 1;
    target.size_x = 1;
    target.size_z = 1;
    target.hp = 12;
    setup.buildings.push_back(target);
    // Стена посреди коридора.
    combat::BattleBuilding wall;
    wall.id = 2;
    wall.cell_x = 5;
    wall.cell_z = 1;
    wall.size_x = 1;
    wall.size_z = 1;
    wall.hp = 30;
    wall.is_wall = true;
    setup.buildings.push_back(wall);

    setup.defender_garrison = 0; // без защитников — чистая проверка слома стены
    combat::AttackerGroup group;
    group.team = combat::kPirateTeam;
    group.count = 6;
    group.landing_side = 3; // запад (левый край)
    group.target_building = 0;
    setup.attackers.push_back(group);

    const combat::BattleResult result = combat::simulate_battle(setup, 3);
    // Стена (id 2) должна быть разрушена по пути к цели.
    bool wall_broken = false;
    bool target_destroyed = false;
    for (int32_t id : result.destroyed_buildings) {
        if (id == 2) wall_broken = true;
        if (id == 1) target_destroyed = true;
    }
    expect(wall_broken, "пираты сломали стену на единственном пути");
    expect(target_destroyed, "после слома стены пираты добили цель");
    expect(result.groups[0].target_destroyed, "исход группы: цель разрушена");
}

// --- Обход дешевле слома: стена цела ---

void test_wall_avoided_when_detour_cheap() {
    const combat::CombatConfig config = load_config();
    // Открытое поле: короткая стена сбоку от прямого пути — дешевле обойти.
    combat::BattleSetup setup = make_open_field(12, config);
    combat::BattleBuilding target;
    target.id = 1;
    target.cell_x = 5;
    target.cell_z = 9;
    target.size_x = 2;
    target.size_z = 2;
    target.hp = 12;
    setup.buildings.push_back(target);
    // Одиночная стена, которую тривиально обойти.
    combat::BattleBuilding wall;
    wall.id = 2;
    wall.cell_x = 6;
    wall.cell_z = 4;
    wall.size_x = 1;
    wall.size_z = 1;
    wall.hp = 30;
    wall.is_wall = true;
    setup.buildings.push_back(wall);

    setup.defender_garrison = 0;
    combat::AttackerGroup group;
    group.team = combat::kPirateTeam;
    group.count = 6;
    group.landing_side = 0;
    group.target_building = 0;
    setup.attackers.push_back(group);

    const combat::BattleResult result = combat::simulate_battle(setup, 5);
    bool wall_broken = false;
    for (int32_t id : result.destroyed_buildings) {
        if (id == 2) wall_broken = true;
    }
    expect(!wall_broken, "одиночную стену обходят, а не ломают");
    expect(result.groups[0].target_destroyed, "цель всё равно разрушена (в обход)");
}

void test_bad_config() {
    combat::CombatConfig config;
    std::string error;
    expect(!combat::parse_combat_json("{ мусор", config, error), "битый combat.json отклонён");
    expect(!combat::parse_combat_json(R"({"tick_rate": 0})", config, error),
            "нулевой tick_rate отклонён");
}

// --- Интеграция: «Нашествие пиратов» через полную партию ---

// Замок игрока 0 несёт кубик из 10 крестов -> порог III (10) за один ход ->
// пиратский рейд на игрока 1 (другая команда), резолв в фазе Боя.
const char *kIntBuildings = R"({
  "starting_resources": { "wood": 8, "stone": 8, "gold": 8, "food": 8, "hammers": 2 },
  "buildings": {
    "castle": { "size_x": 3, "size_z": 3, "cost": {}, "hp": 60, "preplaced": true, "dice": "%DIE%", "spawns_garrison": true }
  }
})";

const char *kIntDice = R"({
  "caps": {},
  "dice": {
    "cross10": [ {"cross":10},{"cross":10},{"cross":10},{"cross":10},{"cross":10},{"cross":10} ],
    "none": [ {"gold":1},{"gold":1},{"gold":1},{"gold":1},{"gold":1},{"gold":1} ]
  }
})";

const char *kIntDisasters = R"({
  "danger_max": 12,
  "thresholds": [ {"value":4,"tier":1},{"value":7,"tier":2},{"value":10,"tier":3},{"value":12,"tier":4} ],
  "disasters": {
    "pirates": { "name": "Пираты", "tier": 3, "target": "opponent", "effect": "pirate_raid", "pirate_count": 6 }
  }
})";

dicecore::Intent ready_intent() {
    dicecore::Intent intent;
    intent.type = dicecore::kIntentPhaseReady;
    return intent;
}

void advance_to_turn_phase(dicecore::Match &match, int32_t turn, int32_t phase) {
    for (int i = 0; i < 4000; ++i) {
        const dicecore::TurnSnapshot s = match.snapshot();
        if (s.turn == turn && s.phase == phase) {
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

void test_pirate_invasion_integration() {
    // Каталог общий: крестовый замок у обоих -> оба достигают порога III и
    // шлют пиратов на противника; проверяем, что бой возник и применился.
    dicecore::MatchConfig config;
    {
        std::string b = kIntBuildings;
        const std::string m = "%DIE%";
        b.replace(b.find(m), m.size(), "cross10");
        config.buildings_json = b;
    }
    config.dice_json = kIntDice;
    config.disasters_json = kIntDisasters;
    config.combat_json = kCombatJson;
    config.match_seed = 12345;
    for (int i = 0; i < 2; ++i) {
        dicecore::PlayerConfig p;
        p.id = i;
        p.team = i + 1; // разные команды
        p.island_seed = 900 + i;
        config.players.push_back(p);
    }

    dicecore::Match match;
    std::string error;
    expect(match.start(config, error), "партия для нашествия стартует");

    // Ход 1: оба замка дают 10 крестов -> оба достигают порога III -> каждый
    // шлёт пиратов на противника. В фазе Боя оба острова атакованы.
    advance_to_turn_phase(match, 2, static_cast<int32_t>(dicecore::Phase::Food));
    const std::vector<dicecore::BattleLog> logs = match.take_battle_logs();
    expect(!logs.empty(), "нашествие пиратов породило бой");

    bool has_frames = false;
    bool someone_hit = false;
    for (const dicecore::BattleLog &log : logs) {
        has_frames = has_frames || !log.frames.empty();
        someone_hit = someone_hit || !log.destroyed_buildings.empty();
    }
    expect(has_frames, "у боя есть лог кадров для проигрывания");
    // 6 пиратов на слабо защищённый остров (гарнизон 0) разрушают замок.
    expect(someone_hit, "пираты нанесли урон (разрушено здание)");
}

} // namespace

int main() {
    test_determinism();
    test_towers_kill_attackers();
    test_wall_broken_when_detour_expensive();
    test_wall_avoided_when_detour_cheap();
    test_bad_config();
    test_pirate_invasion_integration();

    if (g_failures == 0) {
        std::printf("OK: все тесты боевого ядра пройдены\n");
        return 0;
    }
    std::fprintf(stderr, "ПРОВАЛЕНО ПРОВЕРОК: %d\n", g_failures);
    return 1;
}
