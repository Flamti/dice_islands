#include "systems/combat/battle.hpp"

#include "save/json.hpp"

namespace dicecore::systems::combat {

bool parse_combat_json(const std::string &json_text, CombatConfig &config, std::string &error) {
    save::JsonValue root;
    if (!save::parse_json(json_text, root, error)) {
        return false;
    }
    if (!root.is_object()) {
        error = "конфиг боя должен быть JSON-объектом";
        return false;
    }
    const CombatConfig defaults;
    config.tick_rate = root.int_at("tick_rate", defaults.tick_rate);
    config.timeout_sec = static_cast<float>(root.number_at("timeout_sec", defaults.timeout_sec));
    config.reward_percent = root.int_at("reward_percent", defaults.reward_percent);
    config.frame_interval_ticks = root.int_at("frame_interval_ticks", defaults.frame_interval_ticks);
    config.wall_defense_armor_bonus =
            root.int_at("wall_defense_armor_bonus", defaults.wall_defense_armor_bonus);
    config.wall_detour_factor =
            static_cast<float>(root.number_at("wall_detour_factor", defaults.wall_detour_factor));
    config.raid_capacity = root.int_at("raid_capacity", defaults.raid_capacity);
    config.raids_per_turn = root.int_at("raids_per_turn", defaults.raids_per_turn);

    if (root.has("fighter")) {
        const save::JsonValue &f = root.as_object().at("fighter");
        config.fighter.hp = f.int_at("hp", defaults.fighter.hp);
        config.fighter.strength = f.int_at("strength", defaults.fighter.strength);
        config.fighter.attack_rate =
                static_cast<float>(f.number_at("attack_rate", defaults.fighter.attack_rate));
        config.fighter.armor = f.int_at("armor", defaults.fighter.armor);
        config.fighter.dodge = static_cast<float>(f.number_at("dodge", defaults.fighter.dodge));
        config.fighter.range = f.int_at("range", defaults.fighter.range);
        config.fighter.move_speed =
                static_cast<float>(f.number_at("move_speed", defaults.fighter.move_speed));
    }
    if (root.has("tower")) {
        const save::JsonValue &t = root.as_object().at("tower");
        config.tower_radius = t.int_at("radius", defaults.tower_radius);
        config.tower_damage = t.int_at("damage", defaults.tower_damage);
        config.tower_rate = static_cast<float>(t.number_at("rate", defaults.tower_rate));
    }
    if (root.has("pirate")) {
        config.pirate_count = root.as_object().at("pirate").int_at("count", defaults.pirate_count);
    }

    if (config.tick_rate <= 0 || config.timeout_sec <= 0.0f || config.fighter.hp <= 0 ||
            config.fighter.move_speed <= 0.0f || config.tower_rate <= 0.0f ||
            config.fighter.attack_rate <= 0.0f) {
        error = "недопустимые параметры боя (нулевые/отрицательные)";
        return false;
    }
    return true;
}

} // namespace dicecore::systems::combat
