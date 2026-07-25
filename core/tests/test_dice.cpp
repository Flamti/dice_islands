// Тесты кубиков (этап 5): пул от активных зданий, рероллы, блокиратор
// крестов, конвертация граней в ресурсы, капы со сжиганием, детерминизм.
#include "dicecore/core.hpp"

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

// Замок и ферма; ферма дешёвая, чтобы строить со старта.
const char *kBuildingsJson = R"({
  "starting_resources": { "wood": 6, "stone": 4, "gold": 2, "food": 4, "hammers": 2 },
  "buildings": {
    "castle": { "size_x": 3, "size_z": 3, "cost": {}, "hp": 60, "preplaced": true, "dice": "%CASTLE%" },
    "farm": { "size_x": 2, "size_z": 2, "cost": { "wood": 4 }, "hp": 12, "dice": "food" }
  }
})";

// Кубики со спроектированными гранями для детерминированных проверок.
const char *kDiceJson = R"({
  "caps": { "wood": 20, "stone": 20, "food": 20 },
  "dice": {
    "nocross": [
      { "wood": 1 }, { "wood": 1 }, { "wood": 1 },
      { "wood": 1 }, { "wood": 1 }, { "wood": 1 }
    ],
    "allcross": [
      { "cross": 1 }, { "cross": 1 }, { "cross": 1 },
      { "cross": 1 }, { "cross": 1 }, { "cross": 1 }
    ],
    "bigwood": [
      { "wood": 30 }, { "wood": 30 }, { "wood": 30 },
      { "wood": 30 }, { "wood": 30 }, { "wood": 30 }
    ],
    "food": [
      { "food": 2 }, { "food": 1 }, { "food": 1 },
      { "food": 1, "cross": 1 }, { "wood": 1 }, { "cross": 1 }
    ]
  }
})";

std::string buildings_with_castle_die(const std::string &die_id) {
    std::string text = kBuildingsJson;
    const std::string marker = "%CASTLE%";
    text.replace(text.find(marker), marker.size(), die_id);
    return text;
}

dicecore::MatchConfig make_config(const std::string &castle_die, uint64_t match_seed) {
    dicecore::MatchConfig config;
    config.buildings_json = buildings_with_castle_die(castle_die);
    config.dice_json = kDiceJson;
    config.match_seed = match_seed;
    for (int i = 0; i < 2; ++i) {
        dicecore::PlayerConfig player;
        player.id = i;
        player.team = i + 1;
        player.island_seed = 100 + i;
        config.players.push_back(player);
    }
    return config;
}

dicecore::Intent ready_intent() {
    dicecore::Intent intent;
    intent.type = dicecore::kIntentPhaseReady;
    return intent;
}

dicecore::Intent reroll_intent(const std::string &selection) {
    dicecore::Intent intent;
    intent.type = dicecore::kIntentReroll;
    intent.payload[dicecore::kPayloadDice] = selection;
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

const dicecore::PlayerSnapshot *player_by_id(const dicecore::TurnSnapshot &snapshot, int32_t id) {
    for (const dicecore::PlayerSnapshot &player : snapshot.players) {
        if (player.id == id) {
            return &player;
        }
    }
    return nullptr;
}

void test_income_pool_and_reroll() {
    dicecore::Match match;
    std::string error;
    expect(match.start(make_config("nocross", 7), error), "партия с кубиками стартует");

    // До фазы Бросков пул пуст, реролл отклоняется по фазе.
    dicecore::IntentResult result = match.submit_intent(0, reroll_intent("0"));
    expect(!result.accepted && result.reason == dicecore::kRejectWrongPhase,
            "реролл вне фазы Бросков отклонён");

    advance_to_phase(match, static_cast<int32_t>(dicecore::Phase::Rolls));
    dicecore::TurnSnapshot snapshot = match.snapshot();
    const dicecore::PlayerSnapshot *player = player_by_id(snapshot, 0);
    expect(player != nullptr && player->dice.size() == 1, "замок даёт один кубик");
    expect(player->dice[0].type == "nocross", "тип кубика — от здания");
    expect(player->dice[0].face >= 0 && player->dice[0].face <= 5, "первый бросок сделан");
    expect(player->dice[0].wood == 1, "выигрыш грани в снапшоте");
    expect(player->rerolls_left == dicecore::kMaxRerolls, "два реролла на старте");

    // Рероллы: 2 успешных, третий — отказ; мусорные выборки — отказ.
    result = match.submit_intent(0, reroll_intent("0"));
    expect(result.accepted, "первый реролл принят (грань без креста)");
    expect(player_by_id(match.snapshot(), 0)->rerolls_left == 1, "остался один реролл");
    result = match.submit_intent(0, reroll_intent("5"));
    expect(!result.accepted && result.reason == dicecore::kRejectBadDiceSelection,
            "индекс вне пула отклонён");
    result = match.submit_intent(0, reroll_intent("abc"));
    expect(!result.accepted && result.reason == dicecore::kRejectBadDiceSelection,
            "мусорная выборка отклонена");
    result = match.submit_intent(0, reroll_intent("0"));
    expect(result.accepted, "второй реролл принят");
    result = match.submit_intent(0, reroll_intent("0"));
    expect(!result.accepted && result.reason == dicecore::kRejectNoRerollsLeft,
            "третий реролл отклонён");
}

void test_cross_blocker() {
    dicecore::Match match;
    std::string error;
    expect(match.start(make_config("allcross", 8), error), "партия с крестовым кубиком стартует");

    advance_to_phase(match, static_cast<int32_t>(dicecore::Phase::Rolls));
    const dicecore::TurnSnapshot snapshot = match.snapshot();
    const dicecore::PlayerSnapshot *player = player_by_id(snapshot, 0);
    expect(player != nullptr && player->dice.size() == 1 && player->dice[0].crosses == 1,
            "выпала грань с крестом");

    const dicecore::IntentResult result = match.submit_intent(0, reroll_intent("0"));
    expect(!result.accepted && result.reason == dicecore::kRejectCrossLocked,
            "крест невозможно выделить для переброса");
    expect(player_by_id(match.snapshot(), 0)->rerolls_left == dicecore::kMaxRerolls,
            "неудачная попытка не тратит реролл");
}

void test_conversion_and_caps() {
    dicecore::Match match;
    std::string error;
    expect(match.start(make_config("bigwood", 9), error), "партия для капов стартует");

    advance_to_phase(match, static_cast<int32_t>(dicecore::Phase::Rolls));
    advance_to_phase(match, static_cast<int32_t>(dicecore::Phase::Disasters));
    const dicecore::TurnSnapshot snapshot = match.snapshot();
    const dicecore::PlayerSnapshot *player = player_by_id(snapshot, 0);
    // Старт 6Д + 30Д с грани = 36 -> кап 20, излишек сгорел.
    expect(player != nullptr && player->wood == 20, "излишек сверх капа сгорел (36 -> 20)");
    expect(player->dice.empty(), "пул очищен после конвертации");
    expect(player->rerolls_left == 0, "рероллы обнулены после конвертации");
}

void test_inactive_buildings_give_no_dice() {
    dicecore::Match match;
    std::string error;
    expect(match.start(make_config("nocross", 10), error), "партия для стройки стартует");

    // Ход 1: строим ферму в Развитии — кубик она даст только после активации.
    advance_to_phase(match, static_cast<int32_t>(dicecore::Phase::Development));
    dicecore::TurnSnapshot snapshot = match.snapshot();
    // Достаточно любого валидного места: перебор клеток, хост сам валидирует.
    bool built = false;
    for (int32_t z = 0; z < 24 && !built; ++z) {
        for (int32_t x = 0; x < 24 && !built; ++x) {
            dicecore::Intent intent;
            intent.type = dicecore::kIntentBuild;
            intent.payload[dicecore::kPayloadBuilding] = "farm";
            intent.payload[dicecore::kPayloadCellX] = std::to_string(x);
            intent.payload[dicecore::kPayloadCellZ] = std::to_string(z);
            built = match.submit_intent(0, intent).accepted;
        }
    }
    expect(built, "ферма построена в ход 1");

    // Ход 2, фаза Бросков: ферма активировалась и даёт кубик еды.
    advance_to_phase(match, static_cast<int32_t>(dicecore::Phase::Raids));
    advance_to_phase(match, static_cast<int32_t>(dicecore::Phase::Rolls));
    snapshot = match.snapshot();
    expect(snapshot.turn == 2, "наступил ход 2");
    const dicecore::PlayerSnapshot *host = player_by_id(snapshot, 0);
    expect(host != nullptr && host->dice.size() == 2, "активная ферма добавила кубик");
    bool has_food_die = false;
    for (const dicecore::DieSnapshot &die : host->dice) {
        has_food_die = has_food_die || die.type == "food";
    }
    expect(has_food_die, "кубик фермы — еда");
    const dicecore::PlayerSnapshot *guest = player_by_id(snapshot, 1);
    expect(guest != nullptr && guest->dice.size() == 1, "у гостя без фермы один кубик");
}

void test_determinism() {
    dicecore::Match a;
    dicecore::Match b;
    std::string error;
    expect(a.start(make_config("food", 42), error), "партия A стартует");
    expect(b.start(make_config("food", 42), error), "партия B стартует");
    advance_to_phase(a, static_cast<int32_t>(dicecore::Phase::Rolls));
    advance_to_phase(b, static_cast<int32_t>(dicecore::Phase::Rolls));

    const dicecore::TurnSnapshot sa = a.snapshot();
    const dicecore::TurnSnapshot sb = b.snapshot();
    for (int32_t id = 0; id < 2; ++id) {
        const dicecore::PlayerSnapshot *pa = player_by_id(sa, id);
        const dicecore::PlayerSnapshot *pb = player_by_id(sb, id);
        expect(pa->dice.size() == pb->dice.size() && pa->dice[0].face == pb->dice[0].face,
                "один match_seed — одинаковые грани");
    }

    dicecore::Match c;
    expect(c.start(make_config("food", 43), error), "партия C стартует");
    advance_to_phase(c, static_cast<int32_t>(dicecore::Phase::Rolls));
    // Разные сиды почти наверняка дают другую комбинацию граней двух кубиков;
    // проверяем мягко: хотя бы полное совпадение не обязательно.
    const dicecore::TurnSnapshot sc = c.snapshot();
    const bool all_same = player_by_id(sc, 0)->dice[0].face == player_by_id(sa, 0)->dice[0].face &&
            player_by_id(sc, 1)->dice[0].face == player_by_id(sa, 1)->dice[0].face;
    if (all_same) {
        std::fprintf(stderr, "ПРИМЕЧАНИЕ: сиды 42 и 43 совпали по граням (допустимо, 1/36)\n");
    }
}

void test_bad_dice_config() {
    dicecore::Match match;
    std::string error;
    dicecore::MatchConfig config = make_config("food", 1);
    config.dice_json = "{ мусор";
    expect(!match.start(config, error) && error == dicecore::kErrorBadDiceConfig,
            "битый dice.json отклонён");

    config = make_config("food", 1);
    config.dice_json = R"({"dice": {"bad": [{"wood": 1}]}})";
    expect(!match.start(config, error) && error == dicecore::kErrorBadDiceConfig,
            "кубик не с шестью гранями отклонён");
}

} // namespace

int main() {
    test_income_pool_and_reroll();
    test_cross_blocker();
    test_conversion_and_caps();
    test_inactive_buildings_give_no_dice();
    test_determinism();
    test_bad_dice_config();

    if (g_failures == 0) {
        std::printf("OK: все тесты кубиков пройдены\n");
        return 0;
    }
    std::fprintf(stderr, "ПРОВАЛЕНО ПРОВЕРОК: %d\n", g_failures);
    return 1;
}
