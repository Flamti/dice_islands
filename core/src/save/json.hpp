#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

// Минимальный JSON-парсер ядра: конфиги data/*.json сейчас, снапшоты
// сейвов с этапа 14. Без внешних зависимостей (ARCHITECTURE §5).

namespace dicecore::save {

class JsonValue;
using JsonObject = std::map<std::string, JsonValue>;
using JsonArray = std::vector<JsonValue>;

class JsonValue {
public:
    enum class Kind { Null, Bool, Number, String, Object, Array };

    JsonValue() = default;

    Kind kind() const { return kind_; }
    bool is_object() const { return kind_ == Kind::Object; }
    bool is_array() const { return kind_ == Kind::Array; }

    bool as_bool(bool fallback = false) const;
    double as_number(double fallback = 0.0) const;
    const std::string &as_string() const;
    const JsonObject &as_object() const;
    const JsonArray &as_array() const;

    // Доступ к полям объекта; отсутствующий ключ — fallback.
    double number_at(const std::string &key, double fallback) const;
    int32_t int_at(const std::string &key, int32_t fallback) const;
    bool has(const std::string &key) const;

    static JsonValue make_bool(bool value);
    static JsonValue make_number(double value);
    static JsonValue make_string(std::string value);
    static JsonValue make_object(JsonObject value);
    static JsonValue make_array(JsonArray value);

private:
    Kind kind_ = Kind::Null;
    bool bool_ = false;
    double number_ = 0.0;
    std::string string_;
    std::shared_ptr<JsonObject> object_;
    std::shared_ptr<JsonArray> array_;
};

// Разбор текста. При ошибке возвращает false и описание в error.
bool parse_json(const std::string &text, JsonValue &out, std::string &error);

} // namespace dicecore::save
