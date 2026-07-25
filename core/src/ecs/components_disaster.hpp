#pragma once

#include "ecs/components.hpp"

#include <cstdint>
#include <string>
#include <vector>

// Компоненты катастроф (SPEC §7) — только данные, из data/disasters.json.

namespace dicecore::ecs {

// Таргетинг катастрофы (SPEC §7.2).
inline constexpr const char *kTargetSelf = "self"; // бьёт по владельцу шкалы
inline constexpr const char *kTargetOpponent = "opponent"; // по чужой команде

// Эффекты катастроф (расширяется на этапе 12).
inline constexpr const char *kEffectStealResource = "steal_resource";

// Порог шкалы: значение крестов -> тяжесть I..IV.
struct DangerThreshold {
    int32_t value = 0;
    int32_t tier = 0;
};

// Определение катастрофы.
struct DisasterDef {
    std::string id;
    std::string name;
    int32_t tier = 0;
    std::string target; // kTargetSelf | kTargetOpponent
    std::string effect; // kEffectStealResource ...
    // Параметры steal_resource:
    int32_t steal_percent = 0;
    std::vector<std::string> steal_from; // ключи ресурсов ("food", ...)
};

// Компонент сущности партии: шкала и пул катастроф.
struct DisasterCatalog {
    int32_t danger_max = 12;
    std::vector<DangerThreshold> thresholds; // по возрастанию value
    std::vector<DisasterDef> disasters;
};

} // namespace dicecore::ecs
