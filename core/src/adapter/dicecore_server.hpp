#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

namespace dicecore {

// Адаптер ядра для движка. Единственная точка обмена: внутрь — плоские
// Dictionary-намерения, наружу — Dictionary-результаты. Типы Godot не
// проникают ниже этого класса (см. ARCHITECTURE.md).
class DiceCoreServer : public godot::RefCounted {
    GDCLASS(DiceCoreServer, godot::RefCounted)

public:
    godot::String get_core_version() const;

    // intent: { "type": String, "payload": Dictionary(String -> String) }
    // Возвращает: { "accepted": bool, "reason": String, "type": String,
    //               "payload": Dictionary(String -> String) }
    godot::Dictionary submit_intent(const godot::Dictionary &intent) const;

protected:
    static void _bind_methods();
};

} // namespace dicecore
