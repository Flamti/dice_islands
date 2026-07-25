#include "net/intents.hpp"

#include "systems/danger.hpp"
#include "systems/dice.hpp"
#include "systems/placement.hpp"
#include "systems/poi.hpp"
#include "systems/raid.hpp"
#include "systems/research.hpp"
#include "turn/turn_machine.hpp"

namespace dicecore::net {

bool parse_bool(const std::map<std::string, std::string> &payload, const char *key, bool fallback) {
    const auto it = payload.find(key);
    if (it == payload.end()) {
        return fallback;
    }
    return it->second == "true";
}

IntentResult dispatch_intent(entt::registry &registry, int32_t player_id, const Intent &intent) {
    if (intent.type == kIntentPhaseReady) {
        // Отсутствие ключа ready трактуется как подтверждение готовности.
        const bool ready = parse_bool(intent.payload, kPayloadReady, true);
        return turn::set_phase_ready(registry, player_id, ready);
    }
    if (intent.type == kIntentBuild) {
        return systems::handle_build(registry, player_id, intent);
    }
    if (intent.type == kIntentDemolish) {
        return systems::handle_demolish(registry, player_id, intent);
    }
    if (intent.type == kIntentReroll) {
        return systems::handle_reroll(registry, player_id, intent);
    }
    if (intent.type == kIntentHarvest) {
        return systems::handle_harvest(registry, player_id, intent);
    }
    if (intent.type == kIntentRaid) {
        return systems::handle_raid(registry, player_id, intent);
    }
    if (intent.type == kIntentResearch) {
        return systems::handle_research(registry, player_id, intent);
    }
    if (intent.type == kIntentTargetPick) {
        return systems::handle_target_pick(registry, player_id, intent);
    }
    if (intent.type == kIntentCure) {
        return systems::handle_cure(registry, player_id, intent);
    }
    if (intent.type == kIntentStormRite) {
        return systems::handle_storm_rite(registry, player_id, intent);
    }
    // Стейтлес-намерения (echo) не требуют реестра.
    return process_intent(intent);
}

} // namespace dicecore::net
