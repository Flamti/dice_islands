// Тесты еды и голода (этап 6): апкип 1 еда/здание, Starving в случайном
// детерминированном порядке, Starving не даёт кубик, снятие после подвоза.
#include "dicecore/core.hpp"

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

// Замок (кубик задаётся сценарием) + дешёвая хижина (кубик задаётся).
// Стартовая еда подставляется сценарием.
const char *kBuildingsTemplate = R"({
  "starting_resources": { "wood": 6, "stone": 4, "gold": 2, "food": %FOOD%, "hammers": 4 },
  "buildings": {
    "castle": { "size_x": 3, "size_z": 3, "cost": {}, "hp": 60, "preplaced": true, "dice": "%CASTLE%" },
    "hut": { "size_x": 1, "size_z": 1, "cost": { "wood": 1 }, "hp": 5, "dice": "%HUT%" }
  }
})";

const char *kDiceJson = R"({
  "caps": { "food": 20 },
  "dice": {
    "bigfood": [
      { "food": 10 }, { "food": 10 }, { "food": 10 },
      { "food": 10 }, { "food": 10 }, { "food": 10 }
    ],
    "nofood": [
      { "wood": 1 }, { "wood": 1 }, { "wood": 1 },
      { "wood": 1 }, { "wood": 1 }, { "wood": 1 }
    ]
  }
})";

std::string make_buildings(int food, const std::string &castle_die, const std::string &hut_die) {
    std::string text = kBuildingsTemplate;
    auto replace = [&text](const std::string &marker, const std::string &value) {
        text.replace(text.find(marker), marker.size(), value);
    };
    replace("%FOOD%", std::to_string(food));
    replace("%CASTLE%", castle_die);
    replace("%HUT%", hut_die);
    return text;
}

dicecore::MatchConfig make_config(int food, const std::string &castle_die,
        const std::string &hut_die, uint64_t match_seed) {
    dicecore::MatchConfig config;
    config.buildings_json = make_buildings(food, castle_die, hut_die);
    config.dice_json = kDiceJson;
    config.match_seed = match_seed;
    dicecore::PlayerConfig player;
    player.id = 0;
    player.team = 1;
    player.island_seed = 500;
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

// Прогон к фазе phase хода turn (фазы нумеруются внутри хода).
void advance_to_turn_phase(dicecore::Match &match, int32_t turn, int32_t phase) {
    constexpr int kMaxHops = 40;
    for (int i = 0; i < kMaxHops; ++i) {
        const dicecore::TurnSnapshot snapshot = match.snapshot();
        if (snapshot.turn == turn && snapshot.phase == phase) {
            return;
        }
        // Шаг к следующей интересной точке: конец хода либо целевая фаза.
        if (snapshot.turn < turn) {
            advance_to_phase(match, static_cast<int32_t>(dicecore::Phase::Checks));
            advance_to_phase(match, static_cast<int32_t>(dicecore::Phase::TurnStart));
        } else {
            advance_to_phase(match, phase);
        }
    }
}

// Строит count хижин в текущую фазу Развития (место ищет перебором клеток).
int build_huts(dicecore::Match &match, int count) {
    int built = 0;
    for (int32_t z = 0; z < 24 && built < count; ++z) {
        for (int32_t x = 0; x < 24 && built < count; ++x) {
            dicecore::Intent intent;
            intent.type = dicecore::kIntentBuild;
            intent.payload[dicecore::kPayloadBuilding] = "hut";
            intent.payload[dicecore::kPayloadCellX] = std::to_string(x);
            intent.payload[dicecore::kPayloadCellZ] = std::to_string(z);
            if (match.submit_intent(0, intent).accepted) {
                ++built;
            }
        }
    }
    return built;
}

// Множество голодающих зданий в виде отпечатков "тип:клетка".
std::set<std::string> starving_set(const dicecore::TurnSnapshot &snapshot) {
    std::set<std::string> result;
    for (const dicecore::BuildingSnapshot &b : snapshot.buildings) {
        if (b.status == static_cast<int32_t>(dicecore::BuildingStatus::Starving)) {
            result.insert(b.type + ":" + std::to_string(b.cell_x) + "," + std::to_string(b.cell_z));
        }
    }
    return result;
}

int count_starving(const dicecore::TurnSnapshot &snapshot) {
    return static_cast<int>(starving_set(snapshot).size());
}

void test_enough_food() {
    dicecore::Match match;
    std::string error;
    // Еды 5, замок без еды с кубика: t1 апкип 1 здание -> еда 4.
    expect(match.start(make_config(5, "nofood", "nofood", 1), error), "партия стартует");
    advance_to_phase(match, static_cast<int32_t>(dicecore::Phase::Food));
    dicecore::TurnSnapshot snapshot = match.snapshot();
    expect(snapshot.players[0].food == 4, "апкип списал 1 еду за замок");
    expect(count_starving(snapshot) == 0, "при достатке никто не голодает");

    // Строим 2 хижины; ход 2: еды 4 на 3 здания -> все сыты, еда 1.
    advance_to_phase(match, static_cast<int32_t>(dicecore::Phase::Development));
    expect(build_huts(match, 2) == 2, "построены 2 хижины");
    advance_to_turn_phase(match, 2, static_cast<int32_t>(dicecore::Phase::Food));
    snapshot = match.snapshot();
    expect(snapshot.players[0].food == 1, "апкип хода 2 списал 3 еды");
    expect(count_starving(snapshot) == 0, "трое сытых при еде 4");
}

void test_shortage_random_deterministic() {
    // Еды 2: t1 замок ест (еда 1); t1 строим 2 хижины; t2: 3 здания на 1 еду
    // -> ровно 2 Starving, выбор случаен, но детерминирован по сиду.
    std::set<std::string> first_run;
    {
        dicecore::Match match;
        std::string error;
        expect(match.start(make_config(2, "nofood", "nofood", 77), error), "партия A стартует");
        advance_to_phase(match, static_cast<int32_t>(dicecore::Phase::Development));
        expect(build_huts(match, 2) == 2, "хижины A построены");
        advance_to_turn_phase(match, 2, static_cast<int32_t>(dicecore::Phase::Food));
        const dicecore::TurnSnapshot snapshot = match.snapshot();
        expect(snapshot.players[0].food == 0, "еда кончилась");
        first_run = starving_set(snapshot);
        expect(first_run.size() == 2, "голодают ровно 2 здания из 3");
    }
    {
        dicecore::Match match;
        std::string error;
        expect(match.start(make_config(2, "nofood", "nofood", 77), error), "партия B стартует");
        advance_to_phase(match, static_cast<int32_t>(dicecore::Phase::Development));
        build_huts(match, 2);
        advance_to_turn_phase(match, 2, static_cast<int32_t>(dicecore::Phase::Food));
        expect(starving_set(match.snapshot()) == first_run,
                "тот же сид — то же множество голодающих");
    }

    // Разные сиды дают разные множества хотя бы иногда — порядок случайный.
    std::set<std::set<std::string>> variants;
    for (uint64_t seed = 1; seed <= 12; ++seed) {
        dicecore::Match match;
        std::string error;
        match.start(make_config(2, "nofood", "nofood", seed), error);
        advance_to_phase(match, static_cast<int32_t>(dicecore::Phase::Development));
        build_huts(match, 2);
        advance_to_turn_phase(match, 2, static_cast<int32_t>(dicecore::Phase::Food));
        variants.insert(starving_set(match.snapshot()));
    }
    expect(variants.size() >= 2, "разные сиды дают разные множества голодающих");
}

void test_starving_gives_no_dice() {
    // Еды 1: t1 замок ест последнюю еду; кубиков еды нет ни у кого ->
    // t2: замок и хижины голодают, пул кубиков в Бросках пуст.
    dicecore::Match match;
    std::string error;
    expect(match.start(make_config(1, "nofood", "nofood", 5), error), "партия стартует");
    advance_to_phase(match, static_cast<int32_t>(dicecore::Phase::Development));
    expect(build_huts(match, 1) == 1, "хижина построена");
    advance_to_turn_phase(match, 2, static_cast<int32_t>(dicecore::Phase::Rolls));
    const dicecore::TurnSnapshot snapshot = match.snapshot();
    expect(count_starving(snapshot) == 2, "оба здания голодают при нуле еды");
    expect(snapshot.players[0].dice.empty(), "голодающие здания не дают кубиков");
}

void test_recovery_after_food_arrives() {
    // Замок без кубика, хижина с кубиком +10 еды. Еды 2: t1 замок ест (1);
    // t1 строим хижину; t2: 2 здания на 1 еду -> голодает одно (случайно).
    // Если сытой осталась хижина — её кубик привозит еду, и на ходу 3 статус
    // голодавшего снимается. Ищем сид с таким раскладом (и попутно другой).
    bool recovery_checked = false;
    bool castle_fed_seen = false;
    bool hut_fed_seen = false;
    for (uint64_t seed = 1; seed <= 40 && !(recovery_checked && castle_fed_seen); ++seed) {
        dicecore::Match match;
        std::string error;
        match.start(make_config(2, "nofood", "bigfood", seed), error);
        advance_to_phase(match, static_cast<int32_t>(dicecore::Phase::Development));
        if (build_huts(match, 1) != 1) {
            continue;
        }
        advance_to_turn_phase(match, 2, static_cast<int32_t>(dicecore::Phase::Food));
        const std::set<std::string> starving = starving_set(match.snapshot());
        if (starving.size() != 1) {
            continue;
        }
        const bool hut_starving = starving.begin()->rfind("hut:", 0) == 0;
        if (hut_starving) {
            castle_fed_seen = true; // сытым остался замок — еды не прибудет
            continue;
        }
        hut_fed_seen = true;
        // Сытая хижина бросает +10 еды в ход 2 -> к фазе Еды хода 3 замок сыт.
        advance_to_turn_phase(match, 3, static_cast<int32_t>(dicecore::Phase::Food));
        const dicecore::TurnSnapshot snapshot = match.snapshot();
        expect(count_starving(snapshot) == 0, "после подвоза еды голод снят");
        expect(snapshot.players[0].food > 0, "еда с кубика дошла до склада");
        recovery_checked = true;
    }
    expect(hut_fed_seen && recovery_checked, "найден сид со снятием голода");
    expect(castle_fed_seen, "случайность выбирает и замок, и хижину (разные сиды)");
}

} // namespace

int main() {
    test_enough_food();
    test_shortage_random_deterministic();
    test_starving_gives_no_dice();
    test_recovery_after_food_arrives();

    if (g_failures == 0) {
        std::printf("OK: все тесты еды и голода пройдены\n");
        return 0;
    }
    std::fprintf(stderr, "ПРОВАЛЕНО ПРОВЕРОК: %d\n", g_failures);
    return 1;
}
