#include "systems/research.hpp"

#include "ecs/components.hpp"
#include "ecs/components_building.hpp"
#include "save/json.hpp"

#include <string>

namespace dicecore::systems {

namespace {

entt::entity match_entity(const entt::registry &registry) {
    return registry.view<const ecs::MatchState>().front();
}

entt::entity find_player(const entt::registry &registry, int32_t player_id) {
    for (auto [entity, info] : registry.view<const ecs::PlayerInfo>().each()) {
        if (info.id == player_id) {
            return entity;
        }
    }
    return entt::null;
}

} // namespace

bool parse_research_json(const std::string &json_text, ecs::ResearchCatalog &catalog,
        std::string &error) {
    save::JsonValue root;
    if (!save::parse_json(json_text, root, error)) {
        return false;
    }
    if (!root.is_object() || !root.has("branches")) {
        error = "ожидается объект с ключом branches";
        return false;
    }
    for (const auto &[branch, nodes_json] : root.as_object().at("branches").as_object()) {
        if (!nodes_json.is_array()) {
            error = "ветка " + branch + " должна быть массивом узлов";
            return false;
        }
        int32_t tier = 1;
        for (const save::JsonValue &node_json : nodes_json.as_array()) {
            ecs::ResearchNode node;
            node.branch = branch;
            node.id = node_json.as_object().count("id") ? node_json.as_object().at("id").as_string() : "";
            node.effect = node.id; // эффект = id узла
            node.tier = tier++;
            node.cost = node_json.int_at("cost", 0);
            if (node_json.has("params")) {
                for (const auto &[key, value] : node_json.as_object().at("params").as_object()) {
                    node.params[key] = static_cast<int32_t>(value.as_number(0.0));
                }
            }
            if (node.id.empty() || node.cost <= 0) {
                error = "узел без id или со стоимостью <= 0 в ветке " + branch;
                return false;
            }
            catalog.nodes.push_back(std::move(node));
        }
    }
    if (catalog.nodes.empty()) {
        error = "пустое дерево исследований";
        return false;
    }
    return true;
}

bool has_active_university(const entt::registry &registry, int32_t player_id) {
    const auto *catalog = registry.try_get<const ecs::BuildingCatalog>(match_entity(registry));
    if (catalog == nullptr) {
        return false;
    }
    for (auto [entity, building] : registry.view<const ecs::Building>().each()) {
        if (building.player_id == player_id &&
                building.status == static_cast<int32_t>(BuildingStatus::Active) &&
                catalog->defs[building.def_index].unlocks_research) {
            return true;
        }
    }
    return false;
}

bool player_has_research(const entt::registry &registry, entt::entity player,
        const std::string &effect) {
    const auto *research = registry.try_get<const ecs::PlayerResearch>(player);
    return research != nullptr && research->has(effect);
}

IntentResult handle_research(entt::registry &registry, int32_t player_id, const Intent &intent) {
    IntentResult result;
    result.type = intent.type;

    const entt::entity match = match_entity(registry);
    const auto &state = registry.get<const ecs::MatchState>(match);
    if (state.phase != static_cast<int32_t>(Phase::Development)) {
        result.reason = kRejectWrongPhase;
        return result;
    }
    const entt::entity player = find_player(registry, player_id);
    const auto *catalog = registry.try_get<const ecs::ResearchCatalog>(match);
    if (player == entt::null || catalog == nullptr) {
        result.reason = kRejectUnknownNode;
        return result;
    }
    if (!has_active_university(registry, player_id)) {
        result.reason = kRejectNoUniversity;
        return result;
    }

    const auto node_it = intent.payload.find(kPayloadNode);
    const ecs::ResearchNode *node =
            node_it != intent.payload.end() ? catalog->by_id(node_it->second) : nullptr;
    if (node == nullptr) {
        result.reason = kRejectUnknownNode;
        return result;
    }

    auto &research = registry.get_or_emplace<ecs::PlayerResearch>(player);
    if (research.has(node->id)) {
        result.reason = kRejectAlreadyResearched;
        return result;
    }
    // Узлы ветки открываются последовательно: нужен предыдущий по tier.
    if (node->tier > 1) {
        bool prev_unlocked = false;
        for (const ecs::ResearchNode &other : catalog->nodes) {
            if (other.branch == node->branch && other.tier == node->tier - 1 &&
                    research.has(other.id)) {
                prev_unlocked = true;
            }
        }
        if (!prev_unlocked) {
            result.reason = kRejectNodeLocked;
            return result;
        }
    }

    auto &resources = registry.get<ecs::Resources>(player);
    if (resources.culture < node->cost) {
        result.reason = kRejectNotEnoughCulture;
        return result;
    }
    // Трата культуры уменьшает и баланс, и счёт культурной победы (§3).
    resources.culture -= node->cost;
    research.unlocked.insert(node->id);

    result.accepted = true;
    result.payload[kPayloadNode] = node->id;
    result.payload["cost"] = std::to_string(node->cost);
    return result;
}

void apply_research_income(entt::registry &registry) {
    const auto *catalog = registry.try_get<const ecs::ResearchCatalog>(match_entity(registry));
    if (catalog == nullptr) {
        return;
    }
    const int32_t hammers = catalog->param(ecs::kResearchCraft, "hammers_per_turn", 0);
    for (auto [entity, research, resources] :
            registry.view<const ecs::PlayerResearch, ecs::Resources>().each()) {
        if (research.has(ecs::kResearchCraft)) {
            resources.hammers += hammers; // Ремесло: +молотки каждый ход (SPEC §8)
        }
    }
}

} // namespace dicecore::systems
