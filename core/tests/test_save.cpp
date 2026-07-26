// Тесты сохранений и реконнекта (этап 14): сейв -> загрузка -> идентичное
// продолжение по тем же намерениям; передача слота ИИ (дисконнект).
#include "dicecore/core.hpp"
#include "gen/island_gen.hpp"

#include <algorithm>
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

const char *kBuildings = R"({
  "starting_resources": { "wood": 20, "stone": 20, "gold": 20, "food": 20, "hammers": 8, "swords": 4, "culture": 5 },
  "buildings": {
    "castle": { "name": "Замок", "size_x": 3, "size_z": 3, "cost": {}, "hp": 60, "preplaced": true, "dice": "mix", "spawns_garrison": true },
    "farm": { "name": "Ферма", "size_x": 2, "size_z": 2, "cost": { "wood": 4 }, "hp": 12, "dice": "food" }
  }
})";

// Кубик со смешанными гранями (включая кресты) — RNG влияет на исход.
const char *kDice = R"({ "caps": { "food": 20, "wood": 20, "stone": 20 }, "dice": {
  "mix": [ {"wood":1},{"food":2},{"stone":1},{"food":1,"cross":1},{"gold":1},{"cross":1} ],
  "food": [ {"food":2},{"food":1},{"food":1},{"food":1,"cross":1},{"wood":1},{"cross":1} ]
} })";

const char *kDisasters = R"({
  "danger_max": 12, "thresholds": [ {"value":4,"tier":1} ],
  "disasters": { "thieves": { "name": "Воры", "tier": 1, "target": "self", "effect": "steal_resource", "steal_percent": 25, "steal_from": ["food","wood","stone","gold"] } }
})";

dicecore::MatchConfig make_config(uint64_t seed) {
    dicecore::MatchConfig config;
    config.buildings_json = kBuildings;
    config.dice_json = kDice;
    config.disasters_json = kDisasters;
    config.match_seed = seed;
    for (int i = 0; i < 2; ++i) {
        dicecore::PlayerConfig p;
        p.id = i;
        p.team = i + 1;
        p.island_seed = 7000 + i;
        config.players.push_back(p);
    }
    return config;
}

dicecore::Intent ready_intent() {
    dicecore::Intent intent;
    intent.type = dicecore::kIntentPhaseReady;
    return intent;
}

void advance_to_turn_start(dicecore::Match &match, int32_t turn) {
    for (int i = 0; i < 4000; ++i) {
        const dicecore::TurnSnapshot s = match.snapshot();
        if (s.turn == turn && s.phase == 0) {
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

// Играть один полный ход, не трогая барьеры вручную (RNG хоста решает всё).
void play_auto_turn(dicecore::Match &match) {
    const int32_t start_turn = match.snapshot().turn;
    for (int i = 0; i < 2000 && match.snapshot().turn == start_turn; ++i) {
        const dicecore::TurnSnapshot s = match.snapshot();
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

// Отпечаток стейта для сравнения; порядко-независим по зданиям (после
// загрузки entity-id другие, поэтому сортируем строки зданий по клетке).
std::string fingerprint(const dicecore::TurnSnapshot &s) {
    std::string fp = "t" + std::to_string(s.turn) + "p" + std::to_string(s.phase);
    for (const dicecore::PlayerSnapshot &p : s.players) {
        fp += "|P" + std::to_string(p.id) + ":" + std::to_string(p.wood) + "," +
                std::to_string(p.stone) + "," + std::to_string(p.food) + "," +
                std::to_string(p.gold) + "," + std::to_string(p.hammers) + "," +
                std::to_string(p.swords) + "," + std::to_string(p.culture) + ",d" +
                std::to_string(p.danger) + ",a" + (p.alive ? "1" : "0");
    }
    std::vector<std::string> bs;
    for (const dicecore::BuildingSnapshot &b : s.buildings) {
        bs.push_back(b.type + ":" + std::to_string(b.player_id) + "," +
                std::to_string(b.cell_x) + "," + std::to_string(b.cell_z) + ",h" +
                std::to_string(b.hp) + ",s" + std::to_string(b.status) + ",w" +
                std::to_string(b.wonder_stage));
    }
    std::sort(bs.begin(), bs.end());
    for (const std::string &b : bs) {
        fp += "|B" + b;
    }
    return fp;
}

void test_save_load_identical_continuation() {
    // Оригинал: играем несколько ходов до границы хода 3, строим ферму по пути.
    dicecore::Match original;
    std::string error;
    expect(original.start(make_config(4242), error), "оригинал стартует");

    // Ход 1: строим ферму (найдём место).
    for (int i = 0; i < 500; ++i) {
        const dicecore::TurnSnapshot s = original.snapshot();
        if (s.phase == static_cast<int32_t>(dicecore::Phase::Development)) {
            break;
        }
        if (s.is_decision) {
            for (const dicecore::PlayerSnapshot &p : s.players) {
                if (!p.ready) original.submit_intent(p.id, ready_intent());
            }
        }
        original.tick(0.25);
    }
    const dicecore::gen::GridData grid =
            dicecore::gen::generate_grid(7000, dicecore::gen::GeneratorParams{});
    for (int32_t cz = 0; cz + 2 <= grid.cells_z; ++cz) {
        bool built = false;
        for (int32_t cx = 0; cx + 2 <= grid.cells_x && !built; ++cx) {
            dicecore::Intent b;
            b.type = dicecore::kIntentBuild;
            b.payload[dicecore::kPayloadBuilding] = "farm";
            b.payload[dicecore::kPayloadCellX] = std::to_string(cx);
            b.payload[dicecore::kPayloadCellZ] = std::to_string(cz);
            built = original.submit_intent(0, b).accepted;
        }
        if (built) break;
    }

    advance_to_turn_start(original, 3);

    // Сохраняем на границе хода 3.
    const std::string blob = original.save();
    expect(!blob.empty() && blob.front() == '{', "сейв — непустой JSON");

    // Загружаем в свежую партию и сравниваем стейт на границе хода.
    dicecore::Match loaded;
    expect(loaded.load(blob, error), "загрузка сейва успешна");
    expect(loaded.snapshot().turn == 3 && loaded.snapshot().phase == 0,
            "загрузка встала на границу хода 3");
    expect(fingerprint(original.snapshot()) == fingerprint(loaded.snapshot()),
            "стейт после загрузки совпадает с сохранённым");

    // Продолжаем оба одинаково (авто-ход) -> идентичный стейт (RNG сохранён).
    play_auto_turn(original);
    play_auto_turn(loaded);
    expect(fingerprint(original.snapshot()) == fingerprint(loaded.snapshot()),
            "повтор хода из сейва даёт идентичный стейт");

    // И ещё ход — детерминизм держится.
    play_auto_turn(original);
    play_auto_turn(loaded);
    expect(fingerprint(original.snapshot()) == fingerprint(loaded.snapshot()),
            "детерминизм сохраняется на следующий ход");
}

void test_ai_takeover_on_disconnect() {
    // Партия 2 людей без лимита таймеров: фаза-решение НЕ завершится, пока оба
    // не готовы. Игрок 1 «отваливается» -> становится ИИ -> авто-готовность.
    dicecore::MatchConfig config = make_config(7);
    dicecore::Match match;
    std::string error;
    expect(match.start(config, error), "партия стартует");

    // Доходим до фазы Бросков (решение).
    for (int i = 0; i < 500; ++i) {
        const dicecore::TurnSnapshot s = match.snapshot();
        if (s.phase == static_cast<int32_t>(dicecore::Phase::Rolls)) {
            break;
        }
        match.tick(0.25);
    }
    expect(match.snapshot().phase == static_cast<int32_t>(dicecore::Phase::Rolls),
            "достигнута фаза Бросков");

    // Только игрок 0 готов; игрок 1 молчит -> фаза держится (без таймера).
    match.submit_intent(0, ready_intent());
    for (int i = 0; i < 20; ++i) {
        match.tick(0.25);
    }
    expect(match.snapshot().phase == static_cast<int32_t>(dicecore::Phase::Rolls),
            "фаза держится, пока игрок 1 не готов");

    // Игрок 1 отвалился -> слот передан ИИ: сразу готов -> барьер снят, фаза идёт.
    match.set_player_ai(1, true);
    bool ai_now = false;
    for (const dicecore::PlayerSnapshot &p : match.snapshot().players) {
        if (p.id == 1) ai_now = p.is_ai;
    }
    expect(ai_now, "слот отвалившегося игрока помечен ИИ");
    match.tick(0.0); // барьер снят готовностью ИИ — фаза завершается
    expect(match.snapshot().phase != static_cast<int32_t>(dicecore::Phase::Rolls),
            "ИИ доигрывает: зависшая фаза завершилась после передачи слота");
}

void test_ai_auto_ready_next_phase() {
    // Чистая проверка: ИИ-игрок авто-готов при входе в фазу-решение, поэтому
    // фаза завершается силами одного человека + ИИ.
    dicecore::MatchConfig config = make_config(3);
    config.players[1].is_ai = true; // игрок 1 — ИИ с самого начала
    dicecore::Match match;
    std::string error;
    expect(match.start(config, error), "партия человек+ИИ стартует");

    // Доходим до Бросков; человек (0) готов -> фаза завершается (ИИ авто-готов).
    for (int i = 0; i < 500; ++i) {
        const dicecore::TurnSnapshot s = match.snapshot();
        if (s.phase == static_cast<int32_t>(dicecore::Phase::Rolls)) {
            break;
        }
        match.tick(0.25);
    }
    match.submit_intent(0, ready_intent());
    match.tick(0.0);
    expect(match.snapshot().phase != static_cast<int32_t>(dicecore::Phase::Rolls),
            "человек готов + ИИ авто-готов -> фаза Бросков завершилась");
}

void test_bad_save() {
    dicecore::Match match;
    std::string error;
    expect(!match.load("{ мусор", error) && error == dicecore::kErrorBadSave,
            "битый сейв отклонён");
    expect(!match.load(R"({"version":999})", error) && error == dicecore::kErrorBadSave,
            "чужая версия сейва отклонена");
}

} // namespace

int main() {
    test_save_load_identical_continuation();
    test_ai_takeover_on_disconnect();
    test_ai_auto_ready_next_phase();
    test_bad_save();

    if (g_failures == 0) {
        std::printf("OK: все тесты сохранений пройдены\n");
        return 0;
    }
    std::fprintf(stderr, "ПРОВАЛЕНО ПРОВЕРОК: %d\n", g_failures);
    return 1;
}
