#include "dicecore_server.hpp"

#include "dicecore/core.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>

namespace dicecore {

namespace {

// Ключи протокола намерений на границе адаптера.
const char *const kKeyType = "type";
const char *const kKeyPayload = "payload";
const char *const kKeyAccepted = "accepted";
const char *const kKeyReason = "reason";

std::string to_std_string(const godot::String &value) {
    return std::string(value.utf8().get_data());
}

} // namespace

godot::String DiceCoreServer::get_core_version() const {
    return godot::String(kCoreVersion);
}

godot::Dictionary DiceCoreServer::submit_intent(const godot::Dictionary &intent) const {
    Intent core_intent;
    core_intent.type = to_std_string(intent.get(kKeyType, godot::String()));

    const godot::Dictionary payload = intent.get(kKeyPayload, godot::Dictionary());
    const godot::Array payload_keys = payload.keys();
    for (int64_t i = 0; i < payload_keys.size(); ++i) {
        const godot::String key = payload_keys[i];
        const godot::String value = payload[key];
        core_intent.payload[to_std_string(key)] = to_std_string(value);
    }

    const IntentResult result = process_intent(core_intent);

    godot::Dictionary out;
    out[kKeyAccepted] = result.accepted;
    out[kKeyReason] = godot::String(result.reason.c_str());
    out[kKeyType] = godot::String(result.type.c_str());
    godot::Dictionary out_payload;
    for (const auto &[key, value] : result.payload) {
        out_payload[godot::String(key.c_str())] = godot::String(value.c_str());
    }
    out[kKeyPayload] = out_payload;
    return out;
}

void DiceCoreServer::_bind_methods() {
    godot::ClassDB::bind_method(godot::D_METHOD("get_core_version"), &DiceCoreServer::get_core_version);
    godot::ClassDB::bind_method(godot::D_METHOD("submit_intent", "intent"), &DiceCoreServer::submit_intent);
}

} // namespace dicecore
