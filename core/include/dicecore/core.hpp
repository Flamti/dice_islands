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

// Ключи полезной нагрузки намерений.
inline constexpr const char *kPayloadReady = "ready";

// Коды отказа валидации намерений.
inline constexpr const char *kRejectUnknownIntent = "unknown_intent_type";
inline constexpr const char *kRejectEmptyType = "empty_intent_type";
inline constexpr const char *kRejectNoActiveMatch = "no_active_match";
inline constexpr const char *kRejectUnknownPlayer = "unknown_player";
inline constexpr const char *kRejectNotDecisionPhase = "not_decision_phase";

// Коды ошибок старта партии.
inline constexpr const char *kErrorMatchAlreadyActive = "match_already_active";
inline constexpr const char *kErrorBadMatchConfig = "bad_match_config";

// Типы событий, порождаемых тиком партии.
inline constexpr const char *kEventTurnStarted = "turn_started";
inline constexpr const char *kEventPhaseEntered = "phase_entered";

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
};

struct MatchConfig {
    std::vector<PlayerConfig> players;
    PhaseTimers timers;
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

// Снимок игрока для UI: идентичность + ресурсы (этап 2 — нулевые).
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
};

// Снимок хода для UI: фаза, барьер, оставшееся время таймера.
struct TurnSnapshot {
    bool active = false;
    int32_t turn = 0;
    int32_t phase = 0;
    bool is_decision = false;
    double timer_remaining_sec = -1.0; // < 0 — таймера нет (авто-фаза или без лимита)
    std::vector<PlayerSnapshot> players;
};

// Валидация стейтлес-намерений (echo). Используется и вне активной партии.
IntentResult process_intent(const Intent &intent);

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

    TurnSnapshot snapshot() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace dicecore
