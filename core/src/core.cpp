#include "dicecore/core.hpp"

namespace dicecore {

IntentResult process_intent(const Intent &intent) {
    IntentResult result;
    result.type = intent.type;

    if (intent.type.empty()) {
        result.reason = kRejectEmptyType;
        return result;
    }

    if (intent.type == kIntentEcho) {
        // Этап 1: подтверждение возвращает полезную нагрузку без изменений.
        result.accepted = true;
        result.payload = intent.payload;
        return result;
    }

    result.reason = kRejectUnknownIntent;
    return result;
}

} // namespace dicecore
