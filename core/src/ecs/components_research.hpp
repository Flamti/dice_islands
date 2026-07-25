#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

// Компоненты исследований (SPEC §8) — только данные, из data/research.json.

namespace dicecore::ecs {

// Идентификаторы эффектов узлов (совпадают с id узлов в research.json).
inline constexpr const char *kResearchCraft = "craft"; // +молоток/ход
inline constexpr const char *kResearchLogistics = "logistics"; // +к капам складов
inline constexpr const char *kResearchEngineering = "engineering"; // платформа +%
inline constexpr const char *kResearchTactics = "tactics"; // +вместимость рейда
inline constexpr const char *kResearchFortification = "fortification"; // стены/башни
inline constexpr const char *kResearchStrategy = "strategy"; // рейдов за ход
// Магия (эффекты — этап 12): предпросмотр, выбор цели, обряд бури.
inline constexpr const char *kResearchClairvoyance = "clairvoyance";
inline constexpr const char *kResearchDarkMagic = "dark_magic";
inline constexpr const char *kResearchStormRite = "storm_rite";

// Узел дерева исследований.
struct ResearchNode {
    std::string id;
    std::string branch;
    std::string effect;
    int32_t tier = 0; // порядок внутри ветки (1..3)
    int32_t cost = 0; // культура
    std::map<std::string, int32_t> params; // числа эффекта [баланс]
};

// Компонент сущности партии: дерево исследований.
struct ResearchCatalog {
    std::vector<ResearchNode> nodes;

    const ResearchNode *by_id(const std::string &id) const {
        for (const ResearchNode &n : nodes) {
            if (n.id == id) {
                return &n;
            }
        }
        return nullptr;
    }
    const ResearchNode *by_effect(const std::string &effect) const {
        for (const ResearchNode &n : nodes) {
            if (n.effect == effect) {
                return &n;
            }
        }
        return nullptr;
    }
    // Числовой параметр узла с данным эффектом; fallback, если нет.
    int32_t param(const std::string &effect, const std::string &key, int32_t fallback) const {
        const ResearchNode *n = by_effect(effect);
        if (n == nullptr) {
            return fallback;
        }
        const auto it = n->params.find(key);
        return it != n->params.end() ? it->second : fallback;
    }
};

// Компонент игрока: изученные эффекты (по id/effect узлов).
struct PlayerResearch {
    std::set<std::string> unlocked;

    bool has(const std::string &effect) const {
        return unlocked.count(effect) != 0;
    }
};

} // namespace dicecore::ecs
