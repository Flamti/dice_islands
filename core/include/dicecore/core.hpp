#pragma once

#include <map>
#include <string>

namespace dicecore {

// Версия ядра. Сверяется тестами и отдаётся движку через адаптер.
inline constexpr const char *kCoreVersion = "0.1.0";

// Типы намерений, известные ядру на текущем этапе.
inline constexpr const char *kIntentEcho = "echo";

// Коды отказа валидации намерений.
inline constexpr const char *kRejectUnknownIntent = "unknown_intent_type";
inline constexpr const char *kRejectEmptyType = "empty_intent_type";

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

// Валидация и применение намерения. Единственная точка входа команд в ядро.
IntentResult process_intent(const Intent &intent);

} // namespace dicecore
