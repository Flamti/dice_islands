// Smoke-тесты чистого ядра. Собираются и работают без Godot.
#include "dicecore/core.hpp"

#include <cstdio>
#include <cstring>
#include <string>

namespace {

int g_failures = 0;

void expect(bool condition, const char *message) {
    if (!condition) {
        ++g_failures;
        std::fprintf(stderr, "FAIL: %s\n", message);
    }
}

void test_version() {
    expect(std::strlen(dicecore::kCoreVersion) > 0, "версия ядра непуста");
    expect(std::string(dicecore::kCoreVersion) == "0.1.0", "версия ядра совпадает с ожидаемой");
}

void test_echo_roundtrip() {
    dicecore::Intent intent;
    intent.type = dicecore::kIntentEcho;
    intent.payload = {{"message", "привет"}, {"n", "42"}};

    const dicecore::IntentResult result = dicecore::process_intent(intent);
    expect(result.accepted, "echo-намерение принято");
    expect(result.reason.empty(), "у принятого намерения нет причины отказа");
    expect(result.type == dicecore::kIntentEcho, "тип ответа совпадает с типом намерения");
    expect(result.payload == intent.payload, "payload возвращается без искажений");
}

void test_unknown_intent_rejected() {
    dicecore::Intent intent;
    intent.type = "no_such_intent";

    const dicecore::IntentResult result = dicecore::process_intent(intent);
    expect(!result.accepted, "неизвестное намерение отклонено");
    expect(result.reason == dicecore::kRejectUnknownIntent, "причина отказа — неизвестный тип");
}

void test_empty_intent_rejected() {
    const dicecore::IntentResult result = dicecore::process_intent(dicecore::Intent{});
    expect(!result.accepted, "пустое намерение отклонено");
    expect(result.reason == dicecore::kRejectEmptyType, "причина отказа — пустой тип");
}

} // namespace

int main() {
    test_version();
    test_echo_roundtrip();
    test_unknown_intent_rejected();
    test_empty_intent_rejected();

    if (g_failures == 0) {
        std::printf("OK: все smoke-тесты ядра пройдены\n");
        return 0;
    }
    std::fprintf(stderr, "ПРОВАЛЕНО ПРОВЕРОК: %d\n", g_failures);
    return 1;
}
