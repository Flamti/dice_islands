// Тесты Turn State Machine: фазы 0–9, барьеры PhaseReady, таймеры, ИИ-заглушка.
// Собираются и работают без Godot.
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

constexpr double kTickStepSec = 0.25;
constexpr double kNoLimit = 0.0;

dicecore::MatchConfig make_config(int humans, int ais, dicecore::PhaseTimers timers) {
    dicecore::MatchConfig config;
    config.timers = timers;
    for (int i = 0; i < humans + ais; ++i) {
        dicecore::PlayerConfig player;
        player.id = i;
        player.team = i + 1;
        player.is_ai = i >= humans;
        config.players.push_back(player);
    }
    return config;
}

dicecore::Intent ready_intent(bool ready = true) {
    dicecore::Intent intent;
    intent.type = dicecore::kIntentPhaseReady;
    intent.payload[dicecore::kPayloadReady] = ready ? "true" : "false";
    return intent;
}

int count_events(const std::vector<dicecore::Event> &events, const char *type) {
    int count = 0;
    for (const dicecore::Event &event : events) {
        if (event.type == type) {
            ++count;
        }
    }
    return count;
}

const dicecore::PlayerSnapshot *find_player(const dicecore::TurnSnapshot &snapshot, int32_t id) {
    for (const dicecore::PlayerSnapshot &player : snapshot.players) {
        if (player.id == id) {
            return &player;
        }
    }
    return nullptr;
}

// Тест этапа 2 из PLAN.md: хост + 3 ИИ проходят 10 пустых ходов.
void test_ten_turns_host_plus_ai() {
    dicecore::Match match;
    std::string error;
    expect(match.start(make_config(1, 3, {kNoLimit, kNoLimit, kNoLimit}), error), "партия 1+3 стартует");
    expect(match.active(), "партия активна после старта");

    const dicecore::TurnSnapshot initial = match.snapshot();
    expect(initial.turn == 1 && initial.phase == 0, "старт: ход 1, фаза 0");
    expect(initial.players.size() == 4, "в снапшоте 4 игрока");

    constexpr int kTargetTurns = 10;
    constexpr int kMaxTicks = 4000; // потолок с запасом против зависания теста
    int phase_entries = 0;
    int turn = 1;
    bool ai_ready_seen = false;
    for (int i = 0; i < kMaxTicks && turn <= kTargetTurns; ++i) {
        const std::vector<dicecore::Event> events = match.tick(kTickStepSec);
        phase_entries += count_events(events, dicecore::kEventPhaseEntered);
        const dicecore::TurnSnapshot snapshot = match.snapshot();
        turn = snapshot.turn;
        if (snapshot.is_decision) {
            // ИИ-заглушка отмечается готовой мгновенно при входе в фазу.
            const dicecore::PlayerSnapshot *ai = find_player(snapshot, 3);
            ai_ready_seen = ai_ready_seen || (ai != nullptr && ai->ready);
            const dicecore::PlayerSnapshot *human = find_player(snapshot, 0);
            if (human != nullptr && !human->ready) {
                const dicecore::IntentResult result = match.submit_intent(0, ready_intent());
                expect(result.accepted, "phase_ready человека принят в фазе-решении");
            }
        }
    }
    expect(turn > kTargetTurns, "партия 1 человек + 3 ИИ прошла 10 ходов");
    // За 10 полных ходов входов в фазы ровно 10 * 10 (переходы детерминированы).
    expect(phase_entries >= dicecore::kPhaseCount * kTargetTurns, "каждая фаза каждого хода была входом");
    expect(ai_ready_seen, "ИИ-заглушка мгновенно готова в фазах-решениях");

    const dicecore::TurnSnapshot final_snapshot = match.snapshot();
    const dicecore::PlayerSnapshot *human = find_player(final_snapshot, 0);
    expect(human != nullptr && human->wood == 0 && human->food == 0, "ресурсы этапа 2 нулевые");
}

// Прогон авто-фаз до заданной фазы (все игроки не трогают барьеры).
void advance_to_phase(dicecore::Match &match, int32_t phase) {
    constexpr int kMaxTicks = 400;
    for (int i = 0; i < kMaxTicks; ++i) {
        if (match.snapshot().phase == phase) {
            return;
        }
        match.tick(kTickStepSec);
    }
}

void test_barrier_two_humans() {
    dicecore::Match match;
    std::string error;
    expect(match.start(make_config(2, 0, {kNoLimit, kNoLimit, kNoLimit}), error), "партия 2 людей стартует");

    advance_to_phase(match, static_cast<int32_t>(dicecore::Phase::Rolls));
    dicecore::TurnSnapshot snapshot = match.snapshot();
    expect(snapshot.phase == static_cast<int32_t>(dicecore::Phase::Rolls), "достигнута фаза Бросков");
    expect(snapshot.is_decision, "Броски — фаза-решение");
    expect(snapshot.timer_remaining_sec < 0.0, "таймер без лимита не тикает");

    expect(match.submit_intent(0, ready_intent()).accepted, "готовность игрока 0 принята");
    for (int i = 0; i < 40; ++i) {
        match.tick(kTickStepSec);
    }
    snapshot = match.snapshot();
    expect(snapshot.phase == static_cast<int32_t>(dicecore::Phase::Rolls),
            "фаза не завершается, пока готов только один из двух");
    const dicecore::PlayerSnapshot *first = find_player(snapshot, 0);
    expect(first != nullptr && first->ready, "готовность игрока 0 видна в снапшоте");

    // Снятие готовности — валидный ход барьера.
    expect(match.submit_intent(0, ready_intent(false)).accepted, "снятие готовности принято");
    expect(!find_player(match.snapshot(), 0)->ready, "готовность игрока 0 снята");
    expect(match.submit_intent(0, ready_intent()).accepted, "повторная готовность принята");

    expect(match.submit_intent(1, ready_intent()).accepted, "готовность игрока 1 принята");
    match.tick(0.0); // барьер снят — переход не требует времени
    expect(match.snapshot().phase == static_cast<int32_t>(dicecore::Phase::Resources),
            "после готовности обоих фаза Бросков завершилась");
}

void test_timer_expiry() {
    constexpr double kRollsLimitSec = 2.0;
    dicecore::Match match;
    std::string error;
    expect(match.start(make_config(2, 0, {kRollsLimitSec, kNoLimit, kNoLimit}), error),
            "партия с таймером Бросков стартует");

    advance_to_phase(match, static_cast<int32_t>(dicecore::Phase::Rolls));
    dicecore::TurnSnapshot snapshot = match.snapshot();
    expect(snapshot.timer_remaining_sec > 0.0 && snapshot.timer_remaining_sec <= kRollsLimitSec,
            "таймер фазы Бросков запущен");

    match.tick(kRollsLimitSec - 0.5);
    snapshot = match.snapshot();
    expect(snapshot.phase == static_cast<int32_t>(dicecore::Phase::Rolls), "до истечения таймера фаза держится");
    expect(snapshot.timer_remaining_sec <= 0.5 + 1e-9, "оставшееся время убывает");

    // 0.5 с до истечения таймера + 0.1 с внутрь следующей авто-фазы.
    match.tick(0.6);
    expect(match.snapshot().phase == static_cast<int32_t>(dicecore::Phase::Resources),
            "по истечении таймера фаза завершается без готовности игроков");
}

void test_intent_rejects() {
    dicecore::Match match;
    dicecore::IntentResult result = match.submit_intent(0, ready_intent());
    expect(!result.accepted && result.reason == dicecore::kRejectNoActiveMatch,
            "phase_ready без партии отклонён");

    std::string error;
    expect(match.start(make_config(2, 0, {kNoLimit, kNoLimit, kNoLimit}), error), "партия для отказов стартует");

    // Фаза 0 — авто: барьер не принимает готовность.
    result = match.submit_intent(0, ready_intent());
    expect(!result.accepted && result.reason == dicecore::kRejectNotDecisionPhase,
            "phase_ready в авто-фазе отклонён");

    advance_to_phase(match, static_cast<int32_t>(dicecore::Phase::Rolls));
    result = match.submit_intent(99, ready_intent());
    expect(!result.accepted && result.reason == dicecore::kRejectUnknownPlayer,
            "phase_ready неизвестного игрока отклонён");

    // Эхо продолжает работать при активной партии (не ломаем этап 1).
    dicecore::Intent echo;
    echo.type = dicecore::kIntentEcho;
    echo.payload = {{"message", "ping"}};
    result = match.submit_intent(0, echo);
    expect(result.accepted && result.payload == echo.payload, "эхо работает при активной партии");

    std::string second_error;
    expect(!match.start(make_config(2, 0, {}), second_error) && second_error == dicecore::kErrorMatchAlreadyActive,
            "повторный старт партии отклонён");
}

void test_bad_config() {
    dicecore::Match match;
    std::string error;
    expect(!match.start(dicecore::MatchConfig{}, error) && error == dicecore::kErrorBadMatchConfig,
            "старт без игроков отклонён");

    dicecore::MatchConfig duplicated = make_config(2, 0, {});
    duplicated.players[1].id = duplicated.players[0].id;
    expect(!match.start(duplicated, error) && error == dicecore::kErrorBadMatchConfig,
            "дубликаты ID игроков отклонены");
    expect(!match.active(), "после ошибок старта партия неактивна");
}

} // namespace

int main() {
    test_ten_turns_host_plus_ai();
    test_barrier_two_humans();
    test_timer_expiry();
    test_intent_rejects();
    test_bad_config();

    if (g_failures == 0) {
        std::printf("OK: все тесты Turn State Machine пройдены\n");
        return 0;
    }
    std::fprintf(stderr, "ПРОВАЛЕНО ПРОВЕРОК: %d\n", g_failures);
    return 1;
}
