// Тесты размещения зданий (этап 4): предразмещённый замок, валидация
// BuildIntent/DemolishIntent, активация построенного, возврат при сносе.
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

// Компактный каталог этапа 4; числа соответствуют data/buildings.json.
const char *kBuildingsJson = R"({
  "starting_resources": { "wood": 6, "stone": 4, "gold": 2, "food": 4, "hammers": 2 },
  "buildings": {
    "castle": { "name": "Замок", "size_x": 3, "size_z": 3, "cost": {}, "hp": 60, "preplaced": true },
    "farm": { "name": "Ферма", "size_x": 2, "size_z": 2, "cost": { "wood": 4 }, "hp": 12 },
    "barracks": { "name": "Казарма", "size_x": 2, "size_z": 2, "cost": { "wood": 2, "stone": 4 }, "hp": 20 }
  }
})";

constexpr uint64_t kSeedHost = 101;
constexpr uint64_t kSeedGuest = 202;

dicecore::MatchConfig make_config() {
    dicecore::MatchConfig config;
    config.buildings_json = kBuildingsJson;
    for (int i = 0; i < 2; ++i) {
        dicecore::PlayerConfig player;
        player.id = i;
        player.team = i + 1;
        player.is_ai = false;
        player.island_seed = i == 0 ? kSeedHost : kSeedGuest;
        config.players.push_back(player);
    }
    return config;
}

dicecore::Intent ready_intent() {
    dicecore::Intent intent;
    intent.type = dicecore::kIntentPhaseReady;
    return intent;
}

dicecore::Intent build_intent(const std::string &building, int32_t cx, int32_t cz) {
    dicecore::Intent intent;
    intent.type = dicecore::kIntentBuild;
    intent.payload[dicecore::kPayloadBuilding] = building;
    intent.payload[dicecore::kPayloadCellX] = std::to_string(cx);
    intent.payload[dicecore::kPayloadCellZ] = std::to_string(cz);
    return intent;
}

dicecore::Intent demolish_intent(int32_t cx, int32_t cz) {
    dicecore::Intent intent;
    intent.type = dicecore::kIntentDemolish;
    intent.payload[dicecore::kPayloadCellX] = std::to_string(cx);
    intent.payload[dicecore::kPayloadCellZ] = std::to_string(cz);
    return intent;
}

// Прогон до нужной фазы: люди жмут «Готов» в фазах-решениях до целевой.
void advance_to_phase(dicecore::Match &match, int32_t phase) {
    constexpr int kMaxTicks = 500;
    constexpr double kStep = 0.25;
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
        match.tick(kStep);
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

int count_buildings(const dicecore::TurnSnapshot &snapshot, int32_t player_id,
        const std::string &type) {
    int count = 0;
    for (const dicecore::BuildingSnapshot &building : snapshot.buildings) {
        if (building.player_id == player_id && building.type == type) {
            ++count;
        }
    }
    return count;
}

// Свободное место под здание size x size на сетке игрока (вне замка).
bool find_free_spot(const dicecore::TurnSnapshot &snapshot, uint64_t island_seed, int32_t size,
        int32_t &out_x, int32_t &out_z) {
    const dicecore::gen::GridData grid =
            dicecore::gen::generate_grid(island_seed, dicecore::gen::GeneratorParams{});
    for (int32_t cz = 0; cz + size <= grid.cells_z; ++cz) {
        for (int32_t cx = 0; cx + size <= grid.cells_x; ++cx) {
            bool ok = true;
            for (int32_t dz = 0; dz < size && ok; ++dz) {
                for (int32_t dx = 0; dx < size && ok; ++dx) {
                    if (grid.type_at(cx + dx, cz + dz) !=
                            static_cast<int32_t>(dicecore::gen::CellType::Buildable)) {
                        ok = false;
                    }
                }
            }
            // Пересечение с уже стоящими зданиями (замком).
            for (const dicecore::BuildingSnapshot &b : snapshot.buildings) {
                if (!ok) {
                    break;
                }
                const bool overlap = cx < b.cell_x + b.size_x && b.cell_x < cx + size &&
                        cz < b.cell_z + b.size_z && b.cell_z < cz + size;
                ok = ok && !overlap;
            }
            if (ok) {
                out_x = cx;
                out_z = cz;
                return true;
            }
        }
    }
    return false;
}

void test_match_setup() {
    dicecore::Match match;
    std::string error;
    expect(match.start(make_config(), error), "партия с зданиями стартует");

    const dicecore::TurnSnapshot snapshot = match.snapshot();
    expect(count_buildings(snapshot, 0, "castle") == 1, "замок игрока 0 предразмещён");
    expect(count_buildings(snapshot, 1, "castle") == 1, "замок игрока 1 предразмещён");
    for (const dicecore::BuildingSnapshot &b : snapshot.buildings) {
        expect(b.status == static_cast<int32_t>(dicecore::BuildingStatus::Active),
                "замок активен со старта");
        expect(b.size_x == 3 && b.size_z == 3, "габарит замка 3x3");
    }
    const dicecore::PlayerSnapshot *host = player_by_id(snapshot, 0);
    expect(host != nullptr && host->wood == 6 && host->stone == 4 && host->gold == 2 &&
                    host->food == 4 && host->hammers == 2,
            "стартовые ресурсы по SPEC §2.2");
}

void test_build_validation_and_flow() {
    dicecore::Match match;
    std::string error;
    expect(match.start(make_config(), error), "партия для стройки стартует");

    // До фазы Развития строить нельзя.
    dicecore::IntentResult result = match.submit_intent(1, build_intent("farm", 5, 5));
    expect(!result.accepted && result.reason == dicecore::kRejectWrongPhase,
            "стройка вне фазы Развития отклонена");

    advance_to_phase(match, static_cast<int32_t>(dicecore::Phase::Development));

    int32_t cx = -1;
    int32_t cz = -1;
    expect(find_free_spot(match.snapshot(), kSeedGuest, 2, cx, cz), "нашлось место под ферму");

    // Гость (игрок 1) строит ферму.
    result = match.submit_intent(1, build_intent("farm", cx, cz));
    expect(result.accepted, "валидная стройка фермы принята");
    dicecore::TurnSnapshot snapshot = match.snapshot();
    expect(count_buildings(snapshot, 1, "farm") == 1, "ферма появилась в снапшоте");
    const dicecore::PlayerSnapshot *guest = player_by_id(snapshot, 1);
    expect(guest->wood == 2 && guest->hammers == 1, "стоимость фермы списана (4Д + молоток)");
    for (const dicecore::BuildingSnapshot &b : snapshot.buildings) {
        if (b.type == "farm") {
            expect(b.status == static_cast<int32_t>(dicecore::BuildingStatus::UnderConstruction),
                    "ферма строится (UnderConstruction)");
        }
    }

    // Отказы: занято, чужие клетки не считаются, нехватка ресурсов, край.
    result = match.submit_intent(1, build_intent("farm", cx, cz));
    expect(!result.accepted && result.reason == dicecore::kRejectCellOccupied,
            "стройка на занятых клетках отклонена");
    result = match.submit_intent(1, build_intent("farm", -1, 0));
    expect(!result.accepted && result.reason == dicecore::kRejectOutOfBounds,
            "стройка вне сетки отклонена");
    result = match.submit_intent(1, build_intent("farm", 0, 0));
    expect(!result.accepted && result.reason == dicecore::kRejectCellNotBuildable,
            "стройка на непригодной клетке отклонена");
    int32_t cx2 = -1;
    int32_t cz2 = -1;
    expect(find_free_spot(match.snapshot(), kSeedGuest, 2, cx2, cz2), "есть второе место");
    result = match.submit_intent(1, build_intent("farm", cx2, cz2));
    expect(!result.accepted && result.reason == dicecore::kRejectNotEnoughResources,
            "вторая ферма не по карману (осталось 2Д)");
    result = match.submit_intent(1, build_intent("wonder", cx2, cz2));
    expect(!result.accepted && result.reason == dicecore::kRejectUnknownBuilding,
            "неизвестное здание отклонено");
    result = match.submit_intent(1, build_intent("castle", cx2, cz2));
    expect(!result.accepted && result.reason == dicecore::kRejectNotConstructible,
            "замок нельзя строить");

    // Активация: фаза 0 следующего хода.
    advance_to_phase(match, static_cast<int32_t>(dicecore::Phase::Raids));
    advance_to_phase(match, static_cast<int32_t>(dicecore::Phase::Food));
    snapshot = match.snapshot();
    expect(snapshot.turn == 2, "наступил ход 2");
    for (const dicecore::BuildingSnapshot &b : snapshot.buildings) {
        if (b.type == "farm") {
            expect(b.status == static_cast<int32_t>(dicecore::BuildingStatus::Active),
                    "ферма активировалась в фазу 0");
        }
    }

    // Снос в фазу Развития хода 2: возврат 50% дерева, клетки свободны.
    advance_to_phase(match, static_cast<int32_t>(dicecore::Phase::Development));
    // Снос адресует только собственный остров: у игрока 0 в свободной клетке
    // его сетки здания нет, чужие острова недостижимы по построению.
    int32_t host_cx = -1;
    int32_t host_cz = -1;
    expect(find_free_spot(match.snapshot(), kSeedHost, 2, host_cx, host_cz),
            "нашлась свободная клетка у игрока 0");
    result = match.submit_intent(0, demolish_intent(host_cx, host_cz));
    expect(!result.accepted && result.reason == dicecore::kRejectNoBuildingHere,
            "снос пустой клетки своего острова отклонён");
    result = match.submit_intent(1, demolish_intent(cx, cz));
    expect(result.accepted, "снос своей фермы принят");
    snapshot = match.snapshot();
    expect(count_buildings(snapshot, 1, "farm") == 0, "ферма исчезла из снапшота");
    guest = player_by_id(snapshot, 1);
    expect(guest->wood == 4 && guest->hammers == 0, "возврат 50% (2Д), молоток списан");

    // Клетки освободились: можно строить казарму на том же месте (4К+2Д есть).
    result = match.submit_intent(1, build_intent("barracks", cx, cz));
    expect(!result.accepted && result.reason == dicecore::kRejectNotEnoughResources,
            "казарма без молотков отклонена (молотки кончились)");

    // Снос замка запрещён; снос пустого места — no_building_here.
    const dicecore::TurnSnapshot final_snapshot = match.snapshot();
    for (const dicecore::BuildingSnapshot &b : final_snapshot.buildings) {
        if (b.player_id == 1 && b.type == "castle") {
            result = match.submit_intent(1, demolish_intent(b.cell_x, b.cell_z));
            expect(!result.accepted && result.reason == dicecore::kRejectCastleProtected,
                    "замок сносить нельзя");
        }
    }
    result = match.submit_intent(1, demolish_intent(cx, cz));
    expect(!result.accepted && result.reason == dicecore::kRejectNoBuildingHere,
            "снос пустого места отклонён");
}

void test_grid_matches_island() {
    // Сетка быстрого пути (хост) совпадает с сеткой полной генерации (клиент).
    const dicecore::gen::GeneratorParams params;
    const dicecore::gen::GridData grid = dicecore::gen::generate_grid(kSeedHost, params);
    const dicecore::gen::IslandData island = dicecore::gen::generate_island(kSeedHost, params);
    expect(grid.types == island.grid.types, "типы клеток идентичны");
    expect(grid.heights == island.grid.heights, "высоты идентичны");
    expect(grid.poi.size() == island.grid.poi.size(), "POI идентичны по числу");
    for (size_t i = 0; i < grid.poi.size(); ++i) {
        expect(grid.poi[i].cell_x == island.grid.poi[i].cell_x &&
                        grid.poi[i].kind == island.grid.poi[i].kind,
                "POI идентичны по позициям и видам");
    }
}

void test_bad_configs() {
    dicecore::Match match;
    std::string error;
    dicecore::MatchConfig config = make_config();
    config.buildings_json = "{ это не json";
    expect(!match.start(config, error) && error == dicecore::kErrorBadBuildingsConfig,
            "битый buildings_json отклонён");

    config = make_config();
    config.buildings_json = R"({"buildings": {"farm": {"size_x": 2, "size_z": 2, "hp": 12}}})";
    expect(!match.start(config, error) && error == dicecore::kErrorBadBuildingsConfig,
            "каталог без замка отклонён");

    config = make_config();
    config.generator_json = "{ мусор";
    expect(!match.start(config, error) && error == dicecore::kErrorBadGeneratorConfig,
            "битый generator_json отклонён");
    expect(!match.active(), "после ошибок конфига партия неактивна");
}

} // namespace

int main() {
    test_match_setup();
    test_build_validation_and_flow();
    test_grid_matches_island();
    test_bad_configs();

    if (g_failures == 0) {
        std::printf("OK: все тесты размещения пройдены\n");
        return 0;
    }
    std::fprintf(stderr, "ПРОВАЛЕНО ПРОВЕРОК: %d\n", g_failures);
    return 1;
}
