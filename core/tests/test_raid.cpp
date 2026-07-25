// Тесты рейдов игроков (этап 10): валидация (порт, вместимость, союзники,
// лимит), разрушение цели с наградой 30%, возврат выживших, общий бой двух
// враждебных рейдов с наградой за последний удар.
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

// Замок открывает рейды и спавнит гарнизон; стартовый гарнизон 8 мечей.
// combat.json переопределён: атака решающая (высокая сила, без уворота).
const char *kBuildings = R"({
  "starting_resources": { "wood": 20, "stone": 20, "gold": 20, "food": 20, "hammers": 4, "swords": 8 },
  "buildings": {
    "castle": { "name": "Замок", "size_x": 3, "size_z": 3, "cost": {}, "hp": 30, "preplaced": true, "dice": "gold", "unlocks_raids": true },
    "farm": { "name": "Ферма", "size_x": 2, "size_z": 2, "cost": { "wood": 4 }, "hp": 12, "dice": "food" }
  }
})";

const char *kBuildingsGarrison = R"({
  "starting_resources": { "wood": 20, "stone": 20, "gold": 20, "food": 20, "hammers": 4, "swords": 8 },
  "buildings": {
    "castle": { "name": "Замок", "size_x": 3, "size_z": 3, "cost": {}, "hp": 30, "preplaced": true, "dice": "gold", "unlocks_raids": true, "spawns_garrison": true },
    "farm": { "name": "Ферма", "size_x": 2, "size_z": 2, "cost": { "wood": 4 }, "hp": 12, "dice": "food" }
  }
})";

const char *kDice = R"({ "caps": {}, "dice": { "gold": [ {"gold":1},{"gold":1},{"gold":1},{"gold":1},{"gold":1},{"gold":1} ], "food": [ {"food":1},{"food":1},{"food":1},{"food":1},{"food":1},{"food":1} ] } })";

// Решающая атака: высокая сила, без уворота, короткий таймаут.
const char *kCombat = R"({
  "tick_rate": 20, "timeout_sec": 30, "reward_percent": 30, "frame_interval_ticks": 4,
  "wall_defense_armor_bonus": 1, "wall_detour_factor": 1.0, "raid_capacity": 8, "raids_per_turn": 1,
  "fighter": { "hp": 10, "strength": 50, "attack_rate": 2.0, "armor": 0, "dodge": 0.0, "range": 1, "move_speed": 4.0 },
  "tower": { "radius": 4, "damage": 3, "rate": 0.8 }, "pirate": { "count": 6 }
})";

dicecore::MatchConfig make_config(const char *buildings, int players, bool same_team) {
    dicecore::MatchConfig config;
    config.buildings_json = buildings;
    config.dice_json = kDice;
    config.combat_json = kCombat;
    config.match_seed = 555;
    for (int i = 0; i < players; ++i) {
        dicecore::PlayerConfig p;
        p.id = i;
        p.team = same_team ? 1 : (i + 1);
        p.island_seed = 1000 + i;
        config.players.push_back(p);
    }
    return config;
}

dicecore::Intent ready_intent() {
    dicecore::Intent intent;
    intent.type = dicecore::kIntentPhaseReady;
    return intent;
}

dicecore::Intent raid_intent(int32_t target_player, int32_t target_building, int32_t count,
        int32_t side) {
    dicecore::Intent intent;
    intent.type = dicecore::kIntentRaid;
    intent.payload[dicecore::kPayloadTargetPlayer] = std::to_string(target_player);
    intent.payload[dicecore::kPayloadTargetBuilding] = std::to_string(target_building);
    intent.payload[dicecore::kPayloadCount] = std::to_string(count);
    intent.payload[dicecore::kPayloadSide] = std::to_string(side);
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

const dicecore::PlayerSnapshot *player_by_id(const dicecore::TurnSnapshot &snapshot, int32_t id) {
    for (const dicecore::PlayerSnapshot &p : snapshot.players) {
        if (p.id == id) {
            return &p;
        }
    }
    return nullptr;
}

// ID замка игрока в снапшоте (или -1).
int32_t castle_id(const dicecore::TurnSnapshot &snapshot, int32_t player_id) {
    for (const dicecore::BuildingSnapshot &b : snapshot.buildings) {
        if (b.player_id == player_id && b.type == "castle") {
            return static_cast<int32_t>(b.id);
        }
    }
    return -1;
}

void test_raid_validation() {
    dicecore::Match match;
    std::string error;
    expect(match.start(make_config(kBuildings, 2, false), error), "партия 2 команд стартует");

    const int32_t host_castle = castle_id(match.snapshot(), 0);

    // Вне фазы Набегов — отказ по фазе.
    dicecore::IntentResult r = match.submit_intent(1, raid_intent(0, host_castle, 4, 0));
    expect(!r.accepted && r.reason == dicecore::kRejectWrongPhase, "рейд вне фазы Набегов отклонён");

    advance_to_phase(match, static_cast<int32_t>(dicecore::Phase::Raids));

    // Слишком большой отряд.
    r = match.submit_intent(1, raid_intent(0, host_castle, 99, 0));
    expect(!r.accepted && r.reason == dicecore::kRejectBadRaidCount, "рейд сверх вместимости отклонён");
    // По себе.
    r = match.submit_intent(1, raid_intent(1, castle_id(match.snapshot(), 1), 4, 0));
    expect(!r.accepted && r.reason == dicecore::kRejectBadRaidTarget, "рейд по себе отклонён");
    // Неизвестное здание.
    r = match.submit_intent(1, raid_intent(0, 999999, 4, 0));
    expect(!r.accepted && r.reason == dicecore::kRejectBadRaidTarget, "рейд по несущест. зданию отклонён");

    // Валидный рейд.
    r = match.submit_intent(1, raid_intent(0, host_castle, 4, 0));
    expect(r.accepted, "валидный рейд принят");
    expect(player_by_id(match.snapshot(), 1)->swords == 4, "гарнизон списан на отправленных (8-4)");
    // Второй рейд за ход — лимит 1.
    r = match.submit_intent(1, raid_intent(0, host_castle, 2, 0));
    expect(!r.accepted && r.reason == dicecore::kRejectRaidLimit, "второй рейд за ход отклонён (лимит)");
}

void test_raid_ally_rejected() {
    dicecore::Match match;
    std::string error;
    expect(match.start(make_config(kBuildings, 2, true), error), "партия одной команды стартует");
    advance_to_phase(match, static_cast<int32_t>(dicecore::Phase::Raids));
    const int32_t ally_castle = castle_id(match.snapshot(), 0);
    const dicecore::IntentResult r = match.submit_intent(1, raid_intent(0, ally_castle, 4, 0));
    expect(!r.accepted && r.reason == dicecore::kRejectAllyTarget, "рейд по союзнику невозможен");
}

void test_raid_destroys_and_rewards() {
    dicecore::Match match;
    std::string error;
    // Защитник (0) без гарнизон-спавна: рейд гостя (1) беспрепятственно бьёт.
    expect(match.start(make_config(kBuildings, 2, false), error), "партия стартует");

    // Ход 1 Развитие: защитник строит ферму (цель рейда).
    advance_to_phase(match, static_cast<int32_t>(dicecore::Phase::Development));
    const dicecore::gen::GridData grid =
            dicecore::gen::generate_grid(1000, dicecore::gen::GeneratorParams{});
    int32_t fx = -1, fz = -1;
    for (int32_t cz = 0; cz + 2 <= grid.cells_z && fx < 0; ++cz) {
        for (int32_t cx = 0; cx + 2 <= grid.cells_x && fx < 0; ++cx) {
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
                fx = cx;
                fz = cz;
            }
        }
    }
    expect(fx >= 0, "нашлось место под ферму защитника");
    dicecore::Intent build;
    build.type = dicecore::kIntentBuild;
    build.payload[dicecore::kPayloadBuilding] = "farm";
    build.payload[dicecore::kPayloadCellX] = std::to_string(fx);
    build.payload[dicecore::kPayloadCellZ] = std::to_string(fz);
    expect(match.submit_intent(0, build).accepted, "ферма защитника построена");

    // Ферма — цель рейда (уже на сетке, хоть и UnderConstruction).
    int32_t farm_id = -1;
    for (const dicecore::BuildingSnapshot &b : match.snapshot().buildings) {
        if (b.player_id == 0 && b.type == "farm") {
            farm_id = static_cast<int32_t>(b.id);
        }
    }
    expect(farm_id >= 0, "ферма есть в снапшоте");

    // Фаза Набегов: гость (1) отправляет 8 бойцов на ферму.
    advance_to_phase(match, static_cast<int32_t>(dicecore::Phase::Raids));
    const int32_t gold_before = player_by_id(match.snapshot(), 1)->gold;
    const int32_t defender_gold = player_by_id(match.snapshot(), 0)->gold;
    const dicecore::IntentResult r = match.submit_intent(1, raid_intent(0, farm_id, 8, 0));
    expect(r.accepted, "рейд гостя на ферму принят");

    // Фаза Боя: ферма разрушается, награда 30% золота уходит гостю.
    advance_to_turn_phase(match, 2, static_cast<int32_t>(dicecore::Phase::Food));
    const std::vector<dicecore::BattleLog> logs = match.take_battle_logs();
    expect(!logs.empty(), "бой на острове защитника состоялся");

    bool farm_destroyed = false;
    const dicecore::TurnSnapshot after = match.snapshot();
    for (const dicecore::BuildingSnapshot &b : after.buildings) {
        if (static_cast<int32_t>(b.id) == farm_id &&
                b.status == static_cast<int32_t>(dicecore::BuildingStatus::Destroyed)) {
            farm_destroyed = true;
        }
    }
    expect(farm_destroyed, "рейд разрушил ферму защитника");
    // Награда: 30% золота защитника (окр. вниз) перешло гостю; выжившие вернулись.
    const int32_t expected_reward = defender_gold * 30 / 100;
    const dicecore::PlayerSnapshot *guest = player_by_id(after, 1);
    expect(guest->gold >= gold_before + expected_reward, "гость получил ~30% золота защитника");
    expect(guest->swords == 8, "выжившие вернулись в гарнизон (защиты не было)");
}

void test_shared_battle_last_hit() {
    // Три команды: 0 — защитник (со спавном гарнизона), 1 и 2 — враждебные
    // рейдеры на замок 0. Общий бой; награду получает лишь последний удар.
    dicecore::Match match;
    std::string error;
    expect(match.start(make_config(kBuildingsGarrison, 3, false), error), "партия 3 команд стартует");
    advance_to_phase(match, static_cast<int32_t>(dicecore::Phase::Raids));
    const int32_t target = castle_id(match.snapshot(), 0);
    const int32_t def_gold = player_by_id(match.snapshot(), 0)->gold;

    expect(match.submit_intent(1, raid_intent(0, target, 8, 0)).accepted, "рейд игрока 1 принят");
    expect(match.submit_intent(2, raid_intent(0, target, 8, 2)).accepted, "рейд игрока 2 принят");

    advance_to_turn_phase(match, 2, static_cast<int32_t>(dicecore::Phase::Food));
    expect(!match.take_battle_logs().empty(), "общий бой состоялся");

    const dicecore::TurnSnapshot after = match.snapshot();
    // Замок разрушен общим боем.
    bool castle_gone = false;
    for (const dicecore::BuildingSnapshot &b : after.buildings) {
        if (static_cast<int32_t>(b.id) == target &&
                b.status == static_cast<int32_t>(dicecore::BuildingStatus::Destroyed)) {
            castle_gone = true;
        }
    }
    expect(castle_gone, "общий бой разрушил замок защитника");

    // Награду получил ровно один из двух рейдеров (последний удар).
    const int32_t reward = def_gold * 30 / 100;
    const dicecore::PlayerSnapshot *a = player_by_id(after, 1);
    const dicecore::PlayerSnapshot *b = player_by_id(after, 2);
    const bool a_rewarded = a->gold >= 20 + reward - 1; // старт 20 золота
    const bool b_rewarded = b->gold >= 20 + reward - 1;
    expect(a_rewarded != b_rewarded, "награду за цель получил ровно один рейдер (последний удар)");
}

} // namespace

int main() {
    test_raid_validation();
    test_raid_ally_rejected();
    test_raid_destroys_and_rewards();
    test_shared_battle_last_hit();

    if (g_failures == 0) {
        std::printf("OK: все тесты рейдов пройдены\n");
        return 0;
    }
    std::fprintf(stderr, "ПРОВАЛЕНО ПРОВЕРОК: %d\n", g_failures);
    return 1;
}
