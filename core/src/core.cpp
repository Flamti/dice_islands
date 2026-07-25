#include "dicecore/core.hpp"

#include "net/intents.hpp"
#include "turn/turn_machine.hpp"

#include <entt/entt.hpp>

#include <set>

namespace dicecore {

IntentResult process_intent(const Intent &intent) {
    IntentResult result;
    result.type = intent.type;

    if (intent.type.empty()) {
        result.reason = kRejectEmptyType;
        return result;
    }

    if (intent.type == kIntentEcho) {
        // Подтверждение возвращает полезную нагрузку без изменений.
        result.accepted = true;
        result.payload = intent.payload;
        return result;
    }

    result.reason = kRejectUnknownIntent;
    return result;
}

struct Match::Impl {
    entt::registry registry;
    bool active = false;
};

Match::Match() : impl_(std::make_unique<Impl>()) {}

Match::~Match() = default;

bool Match::start(const MatchConfig &config, std::string &error) {
    if (impl_->active) {
        error = kErrorMatchAlreadyActive;
        return false;
    }
    if (config.players.empty()) {
        error = kErrorBadMatchConfig;
        return false;
    }
    std::set<int32_t> ids;
    for (const PlayerConfig &player : config.players) {
        if (!ids.insert(player.id).second) {
            error = kErrorBadMatchConfig; // дубликат ID игрока
            return false;
        }
    }

    std::vector<Event> start_events; // события старта отражены в первом снапшоте
    turn::start_match(impl_->registry, config, start_events);
    impl_->active = true;
    error.clear();
    return true;
}

bool Match::active() const {
    return impl_->active;
}

IntentResult Match::submit_intent(int32_t player_id, const Intent &intent) {
    IntentResult result;
    result.type = intent.type;

    if (intent.type.empty()) {
        result.reason = kRejectEmptyType;
        return result;
    }
    // Стейтлес-намерения работают и до старта партии (лобби, эхо-тест).
    if (intent.type == kIntentEcho) {
        return process_intent(intent);
    }
    if (!impl_->active) {
        result.reason = kRejectNoActiveMatch;
        return result;
    }
    return net::dispatch_intent(impl_->registry, player_id, intent);
}

std::vector<Event> Match::tick(double dt) {
    std::vector<Event> events;
    if (impl_->active) {
        turn::tick(impl_->registry, dt, events);
    }
    return events;
}

TurnSnapshot Match::snapshot() const {
    if (!impl_->active) {
        return TurnSnapshot{};
    }
    return turn::make_snapshot(impl_->registry);
}

} // namespace dicecore
