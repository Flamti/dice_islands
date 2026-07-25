#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Боевая система (SPEC §9): детерминированная симуляция боя на сетке острова
// защитника. Самодостаточна — принимает BattleSetup, возвращает BattleResult
// (исход + записанные кадры для проигрывания клиентами). Работает без ECS и
// без Godot; вызывается фазой Боя, а также напрямую тестами.

namespace dicecore::systems::combat {

// Спецтеам нейтральных пиратов (катастрофа «Нашествие», SPEC §7.2, §9.3).
inline constexpr int32_t kPirateTeam = -1;

// Статы бойца (SPEC §9.1) — из data/combat.json, база + апгрейды.
struct FighterStats {
    int32_t hp = 10;
    int32_t strength = 2;
    float attack_rate = 1.0f; // ударов/с
    int32_t armor = 0;
    float dodge = 0.05f; // шанс избежать удар
    int32_t range = 1; // дальность в клетках
    float move_speed = 2.0f; // клеток/с
};

struct CombatConfig {
    int32_t tick_rate = 20;
    float timeout_sec = 90.0f;
    int32_t reward_percent = 30;
    int32_t frame_interval_ticks = 4; // как часто писать кадр проигрывания
    int32_t wall_defense_armor_bonus = 1; // +броня защитнику у стены (SPEC §9.3)
    float wall_detour_factor = 1.0f; // порог «сломать vs обойти»
    FighterStats fighter;
    int32_t tower_radius = 4;
    int32_t tower_damage = 3;
    float tower_rate = 0.8f; // выстрелов/с
    int32_t pirate_count = 6;
    int32_t raid_capacity = 8; // бойцов в рейде игрока (SPEC §9.4)
    int32_t raids_per_turn = 1; // рейдов за ход (узел «Стратегия» — 2)
};

// Здание на поле боя.
struct BattleBuilding {
    int32_t id = 0; // ID сущности ECS (для применения исхода)
    int32_t cell_x = 0;
    int32_t cell_z = 0;
    int32_t size_x = 1;
    int32_t size_z = 1;
    int32_t hp = 1;
    bool is_wall = false;
    bool is_tower = false;
    bool spawns_garrison = false;
};

// Отряд атакующих (рейд или пираты).
struct AttackerGroup {
    int32_t team = kPirateTeam;
    int32_t owner_player = -1; // для возврата выживших; -1 — пираты
    int32_t count = 0;
    int32_t landing_side = 0; // 0=N(-z),1=E(+x),2=S(+z),3=W(-x)
    int32_t target_building = -1; // индекс в buildings; -1 — ближайшее
};

struct BattleSetup {
    int32_t cells_x = 0;
    int32_t cells_z = 0;
    std::vector<uint8_t> walkable; // 1 — поверхность острова (боец может встать)
    std::vector<BattleBuilding> buildings;
    int32_t defender_player = 0;
    int32_t defender_team = 0;
    int32_t defender_garrison = 0; // бойцов защиты (= мечи игрока)
    std::vector<AttackerGroup> attackers;
    CombatConfig config;
};

// Кадр проигрывания: положение живых бойцов в момент tick.
struct BattleUnitFrame {
    int32_t id = 0;
    int32_t team = 0;
    bool attacker = false;
    float x = 0.0f;
    float z = 0.0f;
    int32_t hp = 0;
};

struct BattleFrame {
    int32_t tick = 0;
    std::vector<BattleUnitFrame> units;
};

// Исход боя для одной атакующей группы.
struct GroupOutcome {
    int32_t team = kPirateTeam;
    int32_t owner_player = -1;
    int32_t survivors = 0;
    bool target_destroyed = false;
    bool got_reward = false; // эта группа нанесла последний удар по своей цели
};

struct BattleResult {
    int32_t ticks = 0;
    int32_t defender_survivors = 0; // выжившие защитники (новый гарнизон)
    std::vector<int32_t> destroyed_buildings; // ID зданий, ставших Destroyed
    std::vector<GroupOutcome> groups;
    std::vector<BattleFrame> frames;
};

// Детерминированная симуляция боя. Один сид -> один исход и один лог.
BattleResult simulate_battle(const BattleSetup &setup, uint64_t seed);

// Разбор data/combat.json в конфиг. При ошибке — false и текст в error.
bool parse_combat_json(const std::string &json_text, CombatConfig &config, std::string &error);

} // namespace dicecore::systems::combat
