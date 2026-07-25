#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Компоненты ECS — только плоские данные, без логики (ARCHITECTURE.md §1).

namespace dicecore::ecs {

// --- Компоненты сущности партии (одна на реестр) ---

// Текущее положение стейт-машины хода.
struct MatchState {
    int32_t turn = 0; // номер хода, с 1
    int32_t phase = 0; // фаза 0–9 (dicecore::Phase)
    double phase_elapsed_sec = 0.0; // время в текущей фазе
};

// Таймеры фаз-решений, заданные лобби; <= 0 — без лимита.
struct MatchTimers {
    double rolls_sec = 0.0;
    double development_sec = 0.0;
    double raids_sec = 0.0;
};

// Единственный RNG партии (SPEC §12.1): живёт на хосте, попадает в снапшоты.
struct MatchRng {
    uint64_t state = 0;
};

// --- Компоненты сущности игрока ---

struct PlayerInfo {
    int32_t id = 0; // стабильный ID (индекс слота лобби)
    int32_t team = 1;
    bool is_ai = false;
    bool alive = true;
};

// Барьер фазы: игрок подтвердил готовность в текущей фазе-решении.
struct PhaseReady {
    bool ready = false;
};

// Личная шкала опасности (SPEC §7.1): накапливает кресты, сбрасывается в 0
// при срабатывании катастрофы. Видна всем игрокам.
struct DangerMeter {
    int32_t value = 0;
};

// Запасы игрока (SPEC §3). Кресты не хранятся — сгорают в шкалу опасности.
struct Resources {
    int32_t wood = 0;
    int32_t stone = 0;
    int32_t food = 0;
    int32_t gold = 0;
    int32_t hammers = 0;
    int32_t swords = 0;
    int32_t culture = 0;
};

// Кубик в пуле игрока текущего хода.
struct DieState {
    int32_t type_index = 0; // индекс в DiceCatalog.defs
    int32_t face = -1; // выпавшая грань 0..5; -1 — не брошен
    int32_t food_bonus = 0; // мельница: +еда при выпадении food-грани (SPEC §5)
};

// Пул кубиков игрока: живёт от фазы Дохода до фазы Ресурсов (SPEC §4).
struct PlayerDice {
    std::vector<DieState> dice;
    int32_t rerolls_left = 0;
};

// Грань кубика: выигрыш ресурсов и кресты (SPEC §6).
struct DieFace {
    Resources gain;
    int32_t crosses = 0;
};

// Тип кубика из data/dice.json.
struct DiceDef {
    std::string id;
    DieFace faces[6];
};

// Компонент сущности партии: каталог кубиков и капы хранения (SPEC §3).
struct DiceCatalog {
    std::vector<DiceDef> defs;
    Resources caps; // 0 — без капа
};

} // namespace dicecore::ecs
