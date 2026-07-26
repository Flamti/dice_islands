#include "save/json.hpp"

#include <cmath>
#include <cstdlib>

namespace dicecore::save {

namespace {

const std::string kEmptyString;
const JsonObject kEmptyObject;
const JsonArray kEmptyArray;

// Рекурсивный спуск по тексту; позиция и ошибка — общее состояние разбора.
struct Parser {
    const std::string &text;
    size_t pos = 0;
    std::string error;

    bool fail(const std::string &message) {
        if (error.empty()) {
            error = message + " (позиция " + std::to_string(pos) + ")";
        }
        return false;
    }

    void skip_whitespace() {
        while (pos < text.size()) {
            const char c = text[pos];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos;
            } else {
                break;
            }
        }
    }

    bool consume(char expected) {
        skip_whitespace();
        if (pos < text.size() && text[pos] == expected) {
            ++pos;
            return true;
        }
        return false;
    }

    bool parse_value(JsonValue &out) {
        skip_whitespace();
        if (pos >= text.size()) {
            return fail("неожиданный конец текста");
        }
        const char c = text[pos];
        if (c == '{') {
            return parse_object(out);
        }
        if (c == '[') {
            return parse_array(out);
        }
        if (c == '"') {
            std::string value;
            if (!parse_string(value)) {
                return false;
            }
            out = JsonValue::make_string(std::move(value));
            return true;
        }
        if (c == 't' || c == 'f') {
            return parse_keyword(out);
        }
        if (c == 'n') {
            if (text.compare(pos, 4, "null") == 0) {
                pos += 4;
                out = JsonValue();
                return true;
            }
            return fail("ожидался null");
        }
        return parse_number(out);
    }

    bool parse_keyword(JsonValue &out) {
        if (text.compare(pos, 4, "true") == 0) {
            pos += 4;
            out = JsonValue::make_bool(true);
            return true;
        }
        if (text.compare(pos, 5, "false") == 0) {
            pos += 5;
            out = JsonValue::make_bool(false);
            return true;
        }
        return fail("ожидалось true/false");
    }

    bool parse_number(JsonValue &out) {
        const char *start = text.c_str() + pos;
        char *end = nullptr;
        const double value = std::strtod(start, &end);
        if (end == start || !std::isfinite(value)) {
            return fail("некорректное число");
        }
        pos += static_cast<size_t>(end - start);
        out = JsonValue::make_number(value);
        return true;
    }

    bool parse_string(std::string &out) {
        if (!consume('"')) {
            return fail("ожидалась строка");
        }
        out.clear();
        while (pos < text.size()) {
            const char c = text[pos++];
            if (c == '"') {
                return true;
            }
            if (c == '\\') {
                if (pos >= text.size()) {
                    return fail("оборванная escape-последовательность");
                }
                const char esc = text[pos++];
                switch (esc) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'n': out.push_back('\n'); break;
                    case 't': out.push_back('\t'); break;
                    case 'r': out.push_back('\r'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    // \uXXXX не нужен конфигам ядра: не-ASCII хранится в UTF-8 как есть.
                    default: return fail("неподдерживаемый escape");
                }
            } else {
                out.push_back(c);
            }
        }
        return fail("незакрытая строка");
    }

    bool parse_object(JsonValue &out) {
        if (!consume('{')) {
            return fail("ожидался объект");
        }
        JsonObject object;
        skip_whitespace();
        if (consume('}')) {
            out = JsonValue::make_object(std::move(object));
            return true;
        }
        while (true) {
            std::string key;
            skip_whitespace();
            if (!parse_string(key)) {
                return false;
            }
            if (!consume(':')) {
                return fail("ожидалось ':'");
            }
            JsonValue value;
            if (!parse_value(value)) {
                return false;
            }
            object[key] = std::move(value);
            if (consume(',')) {
                continue;
            }
            if (consume('}')) {
                out = JsonValue::make_object(std::move(object));
                return true;
            }
            return fail("ожидалось ',' или '}'");
        }
    }

    bool parse_array(JsonValue &out) {
        if (!consume('[')) {
            return fail("ожидался массив");
        }
        JsonArray array;
        skip_whitespace();
        if (consume(']')) {
            out = JsonValue::make_array(std::move(array));
            return true;
        }
        while (true) {
            JsonValue value;
            if (!parse_value(value)) {
                return false;
            }
            array.push_back(std::move(value));
            if (consume(',')) {
                continue;
            }
            if (consume(']')) {
                out = JsonValue::make_array(std::move(array));
                return true;
            }
            return fail("ожидалось ',' или ']'");
        }
    }
};

} // namespace

bool JsonValue::as_bool(bool fallback) const {
    return kind_ == Kind::Bool ? bool_ : fallback;
}

double JsonValue::as_number(double fallback) const {
    return kind_ == Kind::Number ? number_ : fallback;
}

const std::string &JsonValue::as_string() const {
    return kind_ == Kind::String ? string_ : kEmptyString;
}

const JsonObject &JsonValue::as_object() const {
    return kind_ == Kind::Object && object_ != nullptr ? *object_ : kEmptyObject;
}

const JsonArray &JsonValue::as_array() const {
    return kind_ == Kind::Array && array_ != nullptr ? *array_ : kEmptyArray;
}

double JsonValue::number_at(const std::string &key, double fallback) const {
    const JsonObject &object = as_object();
    const auto it = object.find(key);
    return it != object.end() ? it->second.as_number(fallback) : fallback;
}

int32_t JsonValue::int_at(const std::string &key, int32_t fallback) const {
    return static_cast<int32_t>(number_at(key, fallback));
}

bool JsonValue::has(const std::string &key) const {
    const JsonObject &object = as_object();
    return object.find(key) != object.end();
}

JsonValue JsonValue::make_bool(bool value) {
    JsonValue out;
    out.kind_ = Kind::Bool;
    out.bool_ = value;
    return out;
}

JsonValue JsonValue::make_number(double value) {
    JsonValue out;
    out.kind_ = Kind::Number;
    out.number_ = value;
    return out;
}

JsonValue JsonValue::make_string(std::string value) {
    JsonValue out;
    out.kind_ = Kind::String;
    out.string_ = std::move(value);
    return out;
}

JsonValue JsonValue::make_object(JsonObject value) {
    JsonValue out;
    out.kind_ = Kind::Object;
    out.object_ = std::make_shared<JsonObject>(std::move(value));
    return out;
}

JsonValue JsonValue::make_array(JsonArray value) {
    JsonValue out;
    out.kind_ = Kind::Array;
    out.array_ = std::make_shared<JsonArray>(std::move(value));
    return out;
}

std::string escape(const std::string &value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (const char c : value) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '\r': out += "\\r"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

bool parse_json(const std::string &text, JsonValue &out, std::string &error) {
    Parser parser{text, 0, {}};
    if (!parser.parse_value(out)) {
        error = parser.error;
        return false;
    }
    parser.skip_whitespace();
    if (parser.pos != text.size()) {
        error = "лишние данные после значения";
        return false;
    }
    return true;
}

} // namespace dicecore::save
