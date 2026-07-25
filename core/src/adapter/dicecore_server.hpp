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

    // config: { "players": [{ "id": int, "team": int, "is_ai": bool,
    //                          "island_seed": int }],
    //           "timers": { "rolls_sec": float, "development_sec": float,
    //                       "raids_sec": float },  (<= 0 — без лимита)
    //           "generator_json": String, "buildings_json": String }
    // Тексты конфигов data/*.json читает вызывающая сторона; пустой
    // buildings_json — партия без зданий и сеток.
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

    // Генерация острова по сиду (SPEC §11.1). config_json — текст
    // data/generator.json (пустая строка — дефолты). Детерминирована, не
    // зависит от стейта партии — доступна и гостям для локального рендера.
    // Возвращает: { "ok": bool, "reason": String, "glb": PackedByteArray,
    //   "grid": { "cells_x", "cells_z", "cell_size", "origin_x", "origin_z",
    //             "types": PackedInt32Array, "heights": PackedFloat32Array,
    //             "poi": [{ "cell_x", "cell_z", "kind", "amount" }] } }
    godot::Dictionary generate_island(int64_t seed, const godot::String &config_json) const;

protected:
    static void _bind_methods();

private:
    Match match_;
};

} // namespace dicecore
