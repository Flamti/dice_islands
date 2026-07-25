#pragma once

#include "dicecore/core.hpp"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

namespace dicecore {

// Адаптер ядра для движка. Единственная точка обмена: внутрь — плоские
// Dictionary-намерения и конфиг партии, наружу — Dictionary-результаты,
// события и снапшоты. Типы Godot не проникают ниже этого класса
// (см. ARCHITECTURE.md).
class DiceCoreServer : public godot::RefCounted {
    GDCLASS(DiceCoreServer, godot::RefCounted)

public:
    godot::String get_core_version() const;

    // config: { "players": [{ "id": int, "team": int, "is_ai": bool }],
    //           "timers": { "rolls_sec": float, "development_sec": float,
    //                       "raids_sec": float } }  (<= 0 — без лимита)
    // Возвращает: { "ok": bool, "reason": String }
    godot::Dictionary start_match(const godot::Dictionary &config);

    // intent: { "type": String, "payload": Dictionary(String -> String) }
    // Возвращает: { "accepted": bool, "reason": String, "type": String,
    //               "payload": Dictionary(String -> String) }
    godot::Dictionary submit_intent(int32_t player_id, const godot::Dictionary &intent);

    // Продвижение партии на dt секунд.
    // Возвращает Array событий: { "type": String, "payload": Dictionary }.
    godot::Array tick(double dt);

    // Снапшот хода: { "active": bool, "turn": int, "phase": int,
    //   "is_decision": bool, "timer_remaining_sec": float (< 0 — нет таймера),
    //   "players": [{ "id", "team", "is_ai", "alive", "ready", "resources" }] }
    godot::Dictionary get_turn_state() const;

protected:
    static void _bind_methods();

private:
    Match match_;
};

} // namespace dicecore
