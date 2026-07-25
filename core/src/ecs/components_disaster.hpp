#pragma once

#include "dicecore/core.hpp"
#include "ecs/components.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

// Компоненты катастроф (SPEC §7) — только данные, из data/disasters.json.

namespace dicecore::ecs {

// Таргетинг катастрофы (SPEC §7.2).
inline constexpr const char *kTargetSelf = "self"; // бьёт по владельцу шкалы
inline constexpr const char *kTargetOpponent = "opponent"; // по чужой команде

// Эффекты катастроф (SPEC §7.2).
inline constexpr const char *kEffectStealResource = "steal_resource";
inline constexpr const char *kEffectPirateRaid = "pirate_raid";
inline constexpr const char *kEffectDamageRandomBuilding = "damage_random_building"; // молния
inline constexpr const char *kEffectDisease = "disease";
inline constexpr const char *kEffectDisableRandomBuilding = "disable_random_building"; // саботаж
inline constexpr const char *kEffectStorm = "storm";
inline constexpr const char *kEffectMeteor = "meteor";
inline constexpr const char *kEffectEdgeCollapse = "edge_collapse";

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
    // Числовые параметры эффекта [баланс] (damage, spread_percent, min_cells, ...).
    std::map<std::string, int32_t> params;
    std::vector<std::string> steal_from; // ключи ресурсов ("food", ...) для кражи

    int32_t param(const std::string &key, int32_t fallback) const {
        const auto it = params.find(key);
        return it != params.end() ? it->second : fallback;
    }
};

// Компонент сущности партии: шкала и пул катастроф.
struct DisasterCatalog {
    int32_t danger_max = 12;
    std::vector<DangerThreshold> thresholds; // по возрастанию value
    std::vector<DisasterDef> disasters;
};

// Отложенный выбор цели Opponent-катастрофы (Тёмная магия, SPEC §8).
struct DarkChoice {
    int32_t chooser = 0; // игрок, выбирающий цель
    int32_t disaster_index = 0; // индекс в DisasterCatalog.disasters
    int32_t candidate_a = 0; // два случайных конкурента
    int32_t candidate_b = 0;
};

// Компонент партии: незакрытые выборы цели (держат фазу Катастроф).
struct PendingChoices {
    std::vector<DarkChoice> choices;
};

// Компонент партии: события, порождённые вне тика (выбор цели), — тик их
// заберёт и вернёт движку.
struct EventQueue {
    std::vector<Event> events;
};

// Клетка сетки, вырезанная деструктором ландшафта (SPEC §11.5).
struct CarvedCell {
    int32_t cell_x = 0;
    int32_t cell_z = 0;
};

// Компонент игрока: накопленные вырезанные клетки; dirty — меш нужно
// ре-полигонизовать и разослать клиентам.
struct PlayerCarved {
    std::vector<CarvedCell> cells;
    bool dirty = false;
};

// Компонент игрока: перезарядки магии (Обряд бури, SPEC §8).
struct PlayerCooldowns {
    int32_t storm_rite_ready_turn = 0; // ход, с которого Обряд снова доступен
};

} // namespace dicecore::ecs
