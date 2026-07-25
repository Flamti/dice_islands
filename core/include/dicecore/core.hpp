#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace dicecore {

// Версия ядра. Сверяется тестами и отдаётся движку через адаптер.
inline constexpr const char *kCoreVersion = "0.2.0";

// Типы намерений, известные ядру на текущем этапе.
inline constexpr const char *kIntentEcho = "echo";
inline constexpr const char *kIntentPhaseReady = "phase_ready";
inline constexpr const char *kIntentBuild = "build";
inline constexpr const char *kIntentDemolish = "demolish";
inline constexpr const char *kIntentReroll = "reroll";
inline constexpr const char *kIntentHarvest = "harvest"; // добыча POI молотком
inline constexpr const char *kIntentRaid = "raid"; // отправка рейда (SPEC §9.4)
inline constexpr const char *kIntentResearch = "research"; // покупка узла (SPEC §8)
inline constexpr const char *kIntentTargetPick = "target_pick"; // выбор цели (Тёмная магия)
inline constexpr const char *kIntentCure = "cure"; // лечение болезни (SPEC §7.2)
inline constexpr const char *kIntentStormRite = "storm_rite"; // Обряд бури (SPEC §8)

// Ключи полезной нагрузки намерений.
inline constexpr const char *kPayloadReady = "ready";
inline constexpr const char *kPayloadBuilding = "building";
inline constexpr const char *kPayloadCellX = "cell_x";
inline constexpr const char *kPayloadCellZ = "cell_z";
inline constexpr const char *kPayloadDice = "dice"; // индексы через запятую: "0,2,5"
inline constexpr const char *kPayloadTargetPlayer = "target_player";
inline constexpr const char *kPayloadTargetBuilding = "target_building"; // ID сущности
inline constexpr const char *kPayloadCount = "count";
inline constexpr const char *kPayloadSide = "side"; // сторона высадки 0..3
inline constexpr const char *kPayloadNode = "node"; // id узла исследования
inline constexpr const char *kPayloadPick = "pick"; // выбор цели: "0" или "1"

// Коды отказа валидации намерений.
inline constexpr const char *kRejectUnknownIntent = "unknown_intent_type";
inline constexpr const char *kRejectEmptyType = "empty_intent_type";
inline constexpr const char *kRejectNoActiveMatch = "no_active_match";
inline constexpr const char *kRejectUnknownPlayer = "unknown_player";
inline constexpr const char *kRejectNotDecisionPhase = "not_decision_phase";
inline constexpr const char *kRejectWrongPhase = "wrong_phase";
inline constexpr const char *kRejectUnknownBuilding = "unknown_building";
inline constexpr const char *kRejectNotConstructible = "not_constructible";
inline constexpr const char *kRejectOutOfBounds = "out_of_bounds";
inline constexpr const char *kRejectCellNotBuildable = "cell_not_buildable";
inline constexpr const char *kRejectCellOccupied = "cell_occupied";
inline constexpr const char *kRejectNotEnoughResources = "not_enough_resources";
inline constexpr const char *kRejectNoBuildingHere = "no_building_here";
inline constexpr const char *kRejectNotYourBuilding = "not_your_building";
inline constexpr const char *kRejectCastleProtected = "cannot_demolish_castle";
inline constexpr const char *kRejectNoRerollsLeft = "no_rerolls_left";
inline constexpr const char *kRejectBadDiceSelection = "bad_dice_selection";
inline constexpr const char *kRejectCrossLocked = "cross_locked";
inline constexpr const char *kRejectNotEdge = "not_on_edge";
inline constexpr const char *kRejectNoPoiHere = "no_poi_here";
inline constexpr const char *kRejectNoPort = "no_active_port";
inline constexpr const char *kRejectBadRaidTarget = "bad_raid_target";
inline constexpr const char *kRejectBadRaidCount = "bad_raid_count";
inline constexpr const char *kRejectNotEnoughGarrison = "not_enough_garrison";
inline constexpr const char *kRejectRaidLimit = "raid_limit_reached";
inline constexpr const char *kRejectAllyTarget = "cannot_raid_ally";
inline constexpr const char *kRejectNoUniversity = "no_active_university";
inline constexpr const char *kRejectUnknownNode = "unknown_research_node";
inline constexpr const char *kRejectAlreadyResearched = "already_researched";
inline constexpr const char *kRejectNodeLocked = "research_node_locked";
inline constexpr const char *kRejectNotEnoughCulture = "not_enough_culture";
inline constexpr const char *kRejectNoChoice = "no_pending_choice";
inline constexpr const char *kRejectNotDiseased = "building_not_diseased";
inline constexpr const char *kRejectNoDarkMagic = "no_dark_magic";
inline constexpr const char *kRejectNoStormRite = "no_storm_rite";
inline constexpr const char *kRejectStormRiteCooldown = "storm_rite_cooldown";

// Таймер мини-решения выбора цели (Тёмная магия, SPEC §4/§8) [баланс].
inline constexpr double kDarkChoiceTimeoutSec = 15.0;

// Коды ошибок старта партии.
inline constexpr const char *kErrorMatchAlreadyActive = "match_already_active";
inline constexpr const char *kErrorBadMatchConfig = "bad_match_config";
inline constexpr const char *kErrorBadBuildingsConfig = "bad_buildings_config";
inline constexpr const char *kErrorBadGeneratorConfig = "bad_generator_config";
inline constexpr const char *kErrorNoCastleSpot = "no_castle_spot";
inline constexpr const char *kErrorBadDiceConfig = "bad_dice_config";
inline constexpr const char *kErrorBadDisastersConfig = "bad_disasters_config";
inline constexpr const char *kErrorBadCombatConfig = "bad_combat_config";
inline constexpr const char *kErrorBadResearchConfig = "bad_research_config";

// Максимум перебросов за ход (SPEC §6).
inline constexpr int32_t kMaxRerolls = 2;

// Статусы зданий (SPEC §5).
enum class BuildingStatus : int32_t {
    UnderConstruction = 0, // активируется в фазу 0 следующего хода
    Active = 1,
    Starving = 2,
    Destroyed = 3,
    Diseased = 4,
    Disabled = 5,
};

// Типы событий, порождаемых тиком партии.
inline constexpr const char *kEventTurnStarted = "turn_started";
inline constexpr const char *kEventPhaseEntered = "phase_entered";
inline constexpr const char *kEventDisaster = "disaster"; // катастрофа сработала
inline constexpr const char *kEventExpansion = "expansion"; // платформа достроила леса
inline constexpr const char *kEventBattle = "battle"; // бой на острове разрешён
inline constexpr const char *kEventTargetChoice = "target_choice"; // выбор цели (2 кандидата)
inline constexpr const char *kEventLandscape = "landscape"; // деструкция ландшафта
inline constexpr const char *kEventElimination = "elimination"; // игрок выбыл (SPEC §10)
inline constexpr const char *kEventVictory = "victory"; // команда победила

// Фазы хода по SPEC §4 (утверждено 25.07.2026).
enum class Phase : int32_t {
    TurnStart = 0, // авто: автосейв, активация построенного
    Food = 1, // авто: апкип еды
    Income = 2, // авто: сбор кубиков
    Rolls = 3, // решение: броски и рероллы
    Resources = 4, // авто: конвертация граней, капы
    Disasters = 5, // авто + мини-решение: шкалы и катастрофы
    Development = 6, // решение: стройка, исследования, апгрейды
    Raids = 7, // решение: планирование набегов
    Combat = 8, // авто: общий бой
    Checks = 9, // авто: победа/поражение
};

inline constexpr int32_t kPhaseCount = 10;

// Фазы-решения: завершаются барьером PhaseReady либо таймером (SPEC §12.2).
constexpr bool phase_is_decision(Phase phase) {
    return phase == Phase::Rolls || phase == Phase::Development || phase == Phase::Raids;
}

// Отображаемая длительность пустой авто-фазы: пауза, за которую игроки видят
// смену фаз в HUD. Темп интерфейса, не игровой баланс.
inline constexpr double kAutoPhaseDurationSec = 0.5;

// Защита от зацикливания стейт-машины внутри одного тика.
inline constexpr int32_t kMaxTransitionsPerTick = 256;

// Таймеры фаз-решений в секундах; значение <= 0 — без лимита.
// Настраиваются хостом в лобби (SPEC §2.3), ядро дефолтов не навязывает.
struct PhaseTimers {
    double rolls_sec = 0.0;
    double development_sec = 0.0;
    double raids_sec = 0.0;
};

struct PlayerConfig {
    int32_t id = 0; // стабильный ID игрока в партии (индекс слота лобби)
    int32_t team = 1;
    bool is_ai = false;
    uint64_t island_seed = 0; // сид острова (SPEC §11.1)
};

struct MatchConfig {
    std::vector<PlayerConfig> players;
    PhaseTimers timers;
    uint64_t match_seed = 0; // единственный RNG партии — на хосте (SPEC §12.1)
    // Тексты конфигов data/*.json (файлы читает вызывающая сторона).
    // Пустой buildings_json — партия без зданий и сеток (тесты этапа 2);
    // пустой dice_json — партия без кубиков.
    std::string generator_json;
    std::string buildings_json;
    std::string dice_json;
    std::string disasters_json;
    std::string combat_json;
    std::string research_json;
};

// Плоское намерение игрока: тип + строковая полезная нагрузка.
// Адаптер конвертирует Dictionary движка в эту структуру на границе;
// ниже адаптера типы Godot не используются.
struct Intent {
    std::string type;
    std::map<std::string, std::string> payload;
};

// Результат обработки намерения хостом.
struct IntentResult {
    bool accepted = false;
    std::string reason; // код отказа; пустая строка при успехе
    std::string type; // тип исходного намерения
    std::map<std::string, std::string> payload; // ответная нагрузка
};

// Событие тика партии: плоские данные для адаптера и лога.
struct Event {
    std::string type;
    std::map<std::string, std::string> payload;
};

// Снимок кубика в пуле игрока.
struct DieSnapshot {
    std::string type; // id из data/dice.json
    int32_t face = -1; // выпавшая грань 0..5; -1 — не брошен
    int32_t crosses = 0; // крестов на выпавшей грани (> 0 — переброс запрещён)
    int32_t wood = 0; // выигрыш грани по ресурсам
    int32_t stone = 0;
    int32_t food = 0;
    int32_t gold = 0;
    int32_t hammers = 0;
    int32_t swords = 0;
    int32_t culture = 0;
    int32_t food_bonus = 0; // мельница: +еда при food-грани (SPEC §5)
};

// Клетка сетки (леса, добавленные платформой).
struct CellRef {
    int32_t x = 0;
    int32_t z = 0;
};

// POI на острове игрока (SPEC §11.2).
struct PoiRef {
    int32_t x = 0;
    int32_t z = 0;
    std::string kind; // "stone" | "wood"
    int32_t amount = 0;
};

// Снимок игрока для UI: идентичность, ресурсы, пул кубиков текущего хода.
struct PlayerSnapshot {
    int32_t id = 0;
    int32_t team = 1;
    bool is_ai = false;
    bool alive = true;
    bool ready = false;
    int32_t wood = 0;
    int32_t stone = 0;
    int32_t food = 0;
    int32_t gold = 0;
    int32_t hammers = 0;
    int32_t swords = 0;
    int32_t culture = 0;
    int32_t cap_food = 0; // эффективные капы с учётом складов; 0 — без капа
    int32_t cap_wood = 0;
    int32_t cap_stone = 0;
    int32_t danger = 0; // текущее заполнение шкалы опасности (SPEC §7.1)
    int32_t clairvoyance_tier = 0; // предпросмотр тяжести (Ясновидение); 0 — нет
    int32_t rerolls_left = 0;
    bool research_available = false; // активный университет (SPEC §8)
    std::vector<DieSnapshot> dice; // пул от Дохода до Катастроф, иначе пуст
    std::vector<CellRef> scaffolds; // клетки-леса от платформ (SPEC §11.4)
    std::vector<PoiRef> pois; // оставшиеся POI на острове
    std::vector<std::string> research; // изученные эффекты (id узлов)
};

// Снимок здания для UI.
struct BuildingSnapshot {
    uint32_t id = 0; // стабильный ID сущности здания
    int32_t player_id = 0;
    std::string type; // id из data/buildings.json ("farm")
    int32_t cell_x = 0;
    int32_t cell_z = 0;
    int32_t size_x = 1;
    int32_t size_z = 1;
    int32_t status = 0; // BuildingStatus
    int32_t hp = 0;
    int32_t wonder_stage = 0; // Чудо: текущий этап (0 — не Чудо)
    int32_t wonder_stages = 0; // Чудо: всего этапов
};

// Снимок хода для UI: фаза, барьер, оставшееся время таймера.
struct TurnSnapshot {
    bool active = false;
    int32_t turn = 0;
    int32_t phase = 0;
    bool is_decision = false;
    double timer_remaining_sec = -1.0; // < 0 — таймера нет (авто-фаза или без лимита)
    int32_t danger_max = 0; // предел шкалы опасности; 0 — система опасности выкл.
    bool finished = false; // партия завершена (SPEC §10)
    int32_t winner_team = -1; // команда-победитель; -1 — идёт
    std::vector<PlayerSnapshot> players;
    std::vector<BuildingSnapshot> buildings;
};

// Валидация стейтлес-намерений (echo). Используется и вне активной партии.
IntentResult process_intent(const Intent &intent);

// --- Лог боя для проигрывания клиентами (SPEC §9.3) ---
struct BattleLogUnit {
    int32_t id = 0;
    int32_t team = 0;
    bool attacker = false;
    float x = 0.0f;
    float z = 0.0f;
    int32_t hp = 0;
};

struct BattleLogFrame {
    int32_t tick = 0;
    std::vector<BattleLogUnit> units;
};

struct BattleLog {
    int32_t defender_player = 0;
    int32_t defender_survivors = 0;
    std::vector<int32_t> destroyed_buildings; // ID зданий, ставших Destroyed
    std::vector<BattleLogFrame> frames;
};

// Обновление меша острова после деструкции ландшафта (SPEC §11.5): новый GLB
// для перезагрузки на клиенте.
struct IslandUpdate {
    int32_t player = 0;
    std::vector<uint8_t> glb;
};

// Партия: реестр EnTT и Turn State Machine. Живёт только на хосте.
class Match {
public:
    Match();
    ~Match();
    Match(const Match &) = delete;
    Match &operator=(const Match &) = delete;

    // Старт партии. При ошибке возвращает false и код в error.
    bool start(const MatchConfig &config, std::string &error);

    bool active() const;

    // Единственная точка входа команд игроков в ядро.
    IntentResult submit_intent(int32_t player_id, const Intent &intent);

    // Продвижение стейт-машины на dt секунд. Возвращает события переходов.
    std::vector<Event> tick(double dt);

    // Забрать и очистить логи боёв, разрешённых в последних тиках (для стрима
    // визуализации боя клиентам, SPEC §9.3).
    std::vector<BattleLog> take_battle_logs();

    // Забрать обновления мешей островов после деструкции ландшафта (SPEC §11.5).
    std::vector<IslandUpdate> take_island_updates();

    TurnSnapshot snapshot() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace dicecore
