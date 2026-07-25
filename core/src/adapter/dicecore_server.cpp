#include "dicecore_server.hpp"

#include <godot_cpp/core/class_db.hpp>

namespace dicecore {

namespace {

// Ключи протокола на границе адаптера.
const char *const kKeyType = "type";
const char *const kKeyPayload = "payload";
const char *const kKeyAccepted = "accepted";
const char *const kKeyReason = "reason";
const char *const kKeyOk = "ok";
const char *const kKeyPlayers = "players";
const char *const kKeyTimers = "timers";
const char *const kKeyId = "id";
const char *const kKeyTeam = "team";
const char *const kKeyIsAi = "is_ai";
const char *const kKeyAlive = "alive";
const char *const kKeyReady = "ready";
const char *const kKeyResources = "resources";
const char *const kKeyActive = "active";
const char *const kKeyTurn = "turn";
const char *const kKeyPhase = "phase";
const char *const kKeyIsDecision = "is_decision";
const char *const kKeyTimerRemaining = "timer_remaining_sec";
const char *const kKeyRollsSec = "rolls_sec";
const char *const kKeyDevelopmentSec = "development_sec";
const char *const kKeyRaidsSec = "raids_sec";

std::string to_std_string(const godot::String &value) {
    return std::string(value.utf8().get_data());
}

godot::Dictionary payload_to_dictionary(const std::map<std::string, std::string> &payload) {
    godot::Dictionary out;
    for (const auto &[key, value] : payload) {
        out[godot::String(key.c_str())] = godot::String(value.c_str());
    }
    return out;
}

godot::Dictionary intent_result_to_dictionary(const IntentResult &result) {
    godot::Dictionary out;
    out[kKeyAccepted] = result.accepted;
    out[kKeyReason] = godot::String(result.reason.c_str());
    out[kKeyType] = godot::String(result.type.c_str());
    out[kKeyPayload] = payload_to_dictionary(result.payload);
    return out;
}

} // namespace

godot::String DiceCoreServer::get_core_version() const {
    return godot::String(kCoreVersion);
}

godot::Dictionary DiceCoreServer::start_match(const godot::Dictionary &config) {
    MatchConfig core_config;

    const godot::Array players = config.get(kKeyPlayers, godot::Array());
    for (int64_t i = 0; i < players.size(); ++i) {
        const godot::Dictionary player = players[i];
        PlayerConfig core_player;
        core_player.id = static_cast<int32_t>(int64_t(player.get(kKeyId, 0)));
        core_player.team = static_cast<int32_t>(int64_t(player.get(kKeyTeam, 1)));
        core_player.is_ai = bool(player.get(kKeyIsAi, false));
        core_config.players.push_back(core_player);
    }

    const godot::Dictionary timers = config.get(kKeyTimers, godot::Dictionary());
    core_config.timers.rolls_sec = double(timers.get(kKeyRollsSec, 0.0));
    core_config.timers.development_sec = double(timers.get(kKeyDevelopmentSec, 0.0));
    core_config.timers.raids_sec = double(timers.get(kKeyRaidsSec, 0.0));

    std::string error;
    const bool ok = match_.start(core_config, error);
    godot::Dictionary out;
    out[kKeyOk] = ok;
    out[kKeyReason] = godot::String(error.c_str());
    return out;
}

godot::Dictionary DiceCoreServer::submit_intent(int32_t player_id, const godot::Dictionary &intent) {
    Intent core_intent;
    core_intent.type = to_std_string(intent.get(kKeyType, godot::String()));

    const godot::Dictionary payload = intent.get(kKeyPayload, godot::Dictionary());
    const godot::Array payload_keys = payload.keys();
    for (int64_t i = 0; i < payload_keys.size(); ++i) {
        const godot::String key = payload_keys[i];
        const godot::String value = payload[key];
        core_intent.payload[to_std_string(key)] = to_std_string(value);
    }

    return intent_result_to_dictionary(match_.submit_intent(player_id, core_intent));
}

godot::Array DiceCoreServer::tick(double dt) {
    godot::Array out;
    for (const Event &event : match_.tick(dt)) {
        godot::Dictionary entry;
        entry[kKeyType] = godot::String(event.type.c_str());
        entry[kKeyPayload] = payload_to_dictionary(event.payload);
        out.push_back(entry);
    }
    return out;
}

godot::Dictionary DiceCoreServer::get_turn_state() const {
    const TurnSnapshot snapshot = match_.snapshot();

    godot::Dictionary out;
    out[kKeyActive] = snapshot.active;
    out[kKeyTurn] = snapshot.turn;
    out[kKeyPhase] = snapshot.phase;
    out[kKeyIsDecision] = snapshot.is_decision;
    out[kKeyTimerRemaining] = snapshot.timer_remaining_sec;

    godot::Array players;
    for (const PlayerSnapshot &player : snapshot.players) {
        godot::Dictionary entry;
        entry[kKeyId] = player.id;
        entry[kKeyTeam] = player.team;
        entry[kKeyIsAi] = player.is_ai;
        entry[kKeyAlive] = player.alive;
        entry[kKeyReady] = player.ready;
        godot::Dictionary resources;
        resources["wood"] = player.wood;
        resources["stone"] = player.stone;
        resources["food"] = player.food;
        resources["gold"] = player.gold;
        resources["hammers"] = player.hammers;
        resources["swords"] = player.swords;
        resources["culture"] = player.culture;
        entry[kKeyResources] = resources;
        players.push_back(entry);
    }
    out[kKeyPlayers] = players;
    return out;
}

void DiceCoreServer::_bind_methods() {
    godot::ClassDB::bind_method(godot::D_METHOD("get_core_version"), &DiceCoreServer::get_core_version);
    godot::ClassDB::bind_method(godot::D_METHOD("start_match", "config"), &DiceCoreServer::start_match);
    godot::ClassDB::bind_method(
            godot::D_METHOD("submit_intent", "player_id", "intent"), &DiceCoreServer::submit_intent);
    godot::ClassDB::bind_method(godot::D_METHOD("tick", "dt"), &DiceCoreServer::tick);
    godot::ClassDB::bind_method(godot::D_METHOD("get_turn_state"), &DiceCoreServer::get_turn_state);
}

} // namespace dicecore
