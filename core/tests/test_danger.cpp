// Тесты шкалы опасности и катастрофы «Воры» (этап 7): накопление крестов,
// пороги, сброс шкалы, кража 25% случайного ресурса, детерминизм, событие.
#include "dicecore/core.hpp"

#include <cmath>
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

// Замок с кубиком, каждая грань которого — 5 крестов: +5 к шкале за ход
// (один кубик, любая выпавшая грань). Порог I (4) достигается за 1 ход.
const char *kBuildingsJson = R"({
  "starting_resources": { "wood": 8, "stone": 8, "gold": 8, "food": 8, "hammers": 2 },
  "buildings": {
    "castle": { "size_x": 3, "size_z": 3, "cost": {}, "hp": 60, "preplaced": true, "dice": "cross" }
  }
})";

const char *kDiceJson = R"({
  "caps": {},
  "dice": {
    "cross": [
      { "cross": 5 }, { "cross": 5 }, { "cross": 5 },
      { "cross": 5 }, { "cross": 5 }, { "cross": 5 }
    ],
    "nocross": [
      { "gold": 1 }, { "gold": 1 }, { "gold": 1 },
      { "gold": 1 }, { "gold": 1 }, { "gold": 1 }
    ]
  }
})";

const char *kDisastersJson = R"({
  "danger_max": 12,
  "thresholds": [
    { "value": 4, "tier": 1 }, { "value": 7, "tier": 2 },
    { "value": 10, "tier": 3 }, { "value": 12, "tier": 4 }
  ],
  "disasters": {
    "thieves": {
      "name": "Воры", "tier": 1, "target": "self",
      "effect": "steal_resource", "steal_percent": 25,
      "steal_from": ["food", "wood", "stone", "gold"]
    }
  }
})";

dicecore::MatchConfig make_config(const std::string &castle_die, uint64_t match_seed) {
    dicecore::MatchConfig config;
    std::string buildings = kBuildingsJson;
    const std::string marker = "\"dice\": \"cross\"";
    buildings.replace(buildings.find(marker), marker.size(), "\"dice\": \"" + castle_die + "\"");
    config.buildings_json = buildings;
    config.dice_json = kDiceJson;
    config.disasters_json = kDisastersJson;
    config.match_seed = match_seed;
    dicecore::PlayerConfig player;
    player.id = 0;
    player.team = 1;
    player.island_seed = 700;
    config.players.push_back(player);
    return config;
}

dicecore::Intent ready_intent() {
    dicecore::Intent intent;
    intent.type = dicecore::kIntentPhaseReady;
    return intent;
}

void advance_to_phase(dicecore::Match &match, int32_t phase) {
    constexpr int kMaxTicks = 500;
    for (int i = 0; i < kMaxTicks; ++i) {
        const dicecore::TurnSnapshot snapshot = match.snapshot();
        if (snapshot.phase == phase) {
            return;
        }
        if (snapshot.is_decision) {
            for (const dicecore::PlayerSnapshot &player : snapshot.players) {
                if (!player.ready) {
                    match.submit_intent(player.id, ready_intent());
                }
            }
        }
        match.tick(0.25);
    }
}

void advance_to_turn_phase(dicecore::Match &match, int32_t turn, int32_t phase) {
    constexpr int kMaxHops = 60;
    for (int i = 0; i < kMaxHops; ++i) {
        const dicecore::TurnSnapshot snapshot = match.snapshot();
        if (snapshot.turn == turn && snapshot.phase == phase) {
            return;
        }
        if (snapshot.turn < turn) {
            advance_to_phase(match, static_cast<int32_t>(dicecore::Phase::Checks));
            advance_to_phase(match, static_cast<int32_t>(dicecore::Phase::TurnStart));
        } else {
            advance_to_phase(match, phase);
        }
    }
}

// Все события катастроф из тика перехода в фазу Катастроф на текущем ходу.
std::vector<dicecore::Event> disaster_events_of_turn(dicecore::Match &match, int32_t turn) {
    advance_to_turn_phase(match, turn, static_cast<int32_t>(dicecore::Phase::Resources));
    // Следующий тик заведёт машину в фазу Катастроф и вернёт её события.
    std::vector<dicecore::Event> disasters;
    constexpr int kMaxTicks = 40;
    for (int i = 0; i < kMaxTicks; ++i) {
        if (match.snapshot().phase == static_cast<int32_t>(dicecore::Phase::Development)) {
            break;
        }
        for (const dicecore::Event &event : match.tick(0.25)) {
            if (event.type == dicecore::kEventDisaster) {
                disasters.push_back(event);
            }
        }
    }
    return disasters;
}

int player_danger(const dicecore::Match &match, int32_t id) {
    for (const dicecore::PlayerSnapshot &player : match.snapshot().players) {
        if (player.id == id) {
            return player.danger;
        }
    }
    return -1;
}

void test_danger_max_in_snapshot() {
    dicecore::Match match;
    std::string error;
    expect(match.start(make_config("nocross", 1), error), "партия с катастрофами стартует");
    expect(match.snapshot().danger_max == 12, "предел шкалы в снапшоте");
    expect(player_danger(match, 0) == 0, "шкала стартует с нуля");
}

void test_no_crosses_no_disaster() {
    dicecore::Match match;
    std::string error;
    expect(match.start(make_config("nocross", 2), error), "партия без крестов стартует");
    // Несколько ходов без крестов: шкала стоит на нуле, катастроф нет.
    for (int32_t turn = 1; turn <= 3; ++turn) {
        const std::vector<dicecore::Event> disasters = disaster_events_of_turn(match, turn);
        expect(disasters.empty(), "без крестов катастроф нет");
    }
    expect(player_danger(match, 0) == 0, "шкала осталась на нуле");
}

void test_threshold_triggers_theft_and_reset() {
    dicecore::Match match;
    std::string error;
    expect(match.start(make_config("cross", 3), error), "партия с крестовым замком стартует");

    // Ход 1: замок даёт 6 крестов -> шкала 6 >= порог 4 -> «Воры», сброс в 0.
    const std::vector<dicecore::Event> disasters = disaster_events_of_turn(match, 1);
    expect(disasters.size() == 1, "сработала ровно одна катастрофа");
    if (!disasters.empty()) {
        const dicecore::Event &event = disasters[0];
        expect(event.payload.at("player") == "0", "катастрофа у игрока 0");
        expect(event.payload.at("disaster") == "thieves", "сработали именно Воры");
        expect(event.payload.at("tier") == "1", "тяжесть I");
        expect(event.payload.count("resource") == 1, "в событии указан украденный ресурс");
        expect(event.payload.count("amount") == 1, "в событии указан объём кражи");
    }
    expect(player_danger(match, 0) == 0, "шкала сброшена после катастрофы");
}

void test_theft_amount_25_percent() {
    // Стартовые запасы 8 каждого из еды/дерева/камня/золота -> кража 25% = 2.
    dicecore::Match match;
    std::string error;
    expect(match.start(make_config("cross", 3), error), "партия стартует");
    const std::vector<dicecore::Event> disasters = disaster_events_of_turn(match, 1);
    expect(disasters.size() == 1, "одна катастрофа");
    if (disasters.empty()) {
        return;
    }
    const std::string resource = disasters[0].payload.at("resource");
    const int stolen = std::stoi(disasters[0].payload.at("amount"));
    // Любой из четырёх ресурсов стартует с 8; ceil(8 * 25%) = 2.
    expect(stolen == 2, "украдено 25% от 8 с округлением вверх = 2");

    // Ресурс в снапшоте уменьшился ровно на украденное.
    int value = -1;
    for (const dicecore::PlayerSnapshot &player : match.snapshot().players) {
        if (player.id != 0) {
            continue;
        }
        if (resource == "food") value = player.food;
        else if (resource == "wood") value = player.wood;
        else if (resource == "stone") value = player.stone;
        else if (resource == "gold") value = player.gold;
    }
    // Замок без ресурсных граней: 8 - 2 = 6 (кубик «cross» ресурсов не даёт).
    expect(value == 6, "у игрока украденный ресурс уменьшился на объём кражи");
}

void test_determinism_same_seed() {
    dicecore::Match a;
    dicecore::Match b;
    std::string error;
    expect(a.start(make_config("cross", 99), error), "партия A стартует");
    expect(b.start(make_config("cross", 99), error), "партия B стартует");
    const std::vector<dicecore::Event> da = disaster_events_of_turn(a, 1);
    const std::vector<dicecore::Event> db = disaster_events_of_turn(b, 1);
    expect(da.size() == 1 && db.size() == 1, "у обеих по одной катастрофе");
    if (da.size() == 1 && db.size() == 1) {
        expect(da[0].payload.at("resource") == db[0].payload.at("resource"),
                "один сид — тот же украденный ресурс");
    }

    // Разные сиды выбирают разные ресурсы хотя бы иногда.
    std::vector<std::string> stolen_resources;
    for (uint64_t seed = 1; seed <= 12; ++seed) {
        dicecore::Match match;
        match.start(make_config("cross", seed), error);
        const std::vector<dicecore::Event> disasters = disaster_events_of_turn(match, 1);
        if (!disasters.empty()) {
            stolen_resources.push_back(disasters[0].payload.at("resource"));
        }
    }
    bool has_variety = false;
    for (const std::string &res : stolen_resources) {
        has_variety = has_variety || res != stolen_resources.front();
    }
    expect(has_variety, "разные сиды крадут разные ресурсы");
}

void test_bad_config() {
    dicecore::Match match;
    std::string error;
    dicecore::MatchConfig config = make_config("cross", 1);
    config.disasters_json = "{ мусор";
    expect(!match.start(config, error) && error == dicecore::kErrorBadDisastersConfig,
            "битый disasters.json отклонён");

    config = make_config("cross", 1);
    config.disasters_json = R"({"thresholds": [], "disasters": {}})";
    expect(!match.start(config, error) && error == dicecore::kErrorBadDisastersConfig,
            "пустые пороги отклонены");
}

} // namespace

int main() {
    test_danger_max_in_snapshot();
    test_no_crosses_no_disaster();
    test_threshold_triggers_theft_and_reset();
    test_theft_amount_25_percent();
    test_determinism_same_seed();
    test_bad_config();

    if (g_failures == 0) {
        std::printf("OK: все тесты опасности пройдены\n");
        return 0;
    }
    std::fprintf(stderr, "ПРОВАЛЕНО ПРОВЕРОК: %d\n", g_failures);
    return 1;
}
