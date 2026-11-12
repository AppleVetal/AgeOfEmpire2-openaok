#include "ecs/TransportCombat.h"

#include <algorithm>
#include <map>
#include "core/TrojanHorse.h"
#include "ecs/Combat.h"
#include "ecs/Players.h"
#include "ecs/components/Combat.h"
#include "ecs/components/Garrison.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Path.h"
#include "ecs/components/Transform.h"

namespace ECS::transport_combat
{
  namespace
  {
    namespace c = Components;
    namespace r = Core::Rules;
    using Core::Fixed32;

    // Кэширует вычисляемое вооружение повозок; после загрузки восстанавливается из пассажиров.
    struct Batteries
    {
      std::map<entt::entity, r::UnitRules> m_profiles;
    };
  }

  bool isHorse(const entt::registry& registry, entt::entity entity)
  {
    const auto* identity = registry.try_get<c::Identity>(entity);
    return identity != nullptr && identity->unit_id == Core::trojan_horse::unitId;
  }

  bool isMoving(const entt::registry& registry, entt::entity entity)
  {
    const auto* path = registry.try_get<c::Path>(entity);
    return path != nullptr && path->current_waypoint_index < path->waypoints.size();
  }

  bool weapon(const entt::registry& registry, entt::entity passenger, r::UnitRules& result)
  {
    const auto* identity = registry.try_get<c::Identity>(passenger);
    if (identity == nullptr) return false;
    const auto* rules = Players::RulesFor(registry, passenger, identity->unit_id);
    if (rules == nullptr || rules->attack_count == 0 || rules->reload.value <= 0) return false;
    const auto& settings = Core::trojan_horse::settings();
    const bool isSpearman = std::find(settings.m_spearUnits.begin(), settings.m_spearUnits.end(),
      identity->unit_id) != settings.m_spearUnits.end();
    if (!isSpearman && (rules->cls != 0 || rules->projectile < 0)) return false;
    result = *rules;
    if (isSpearman)
    {
      constexpr int meleeClass = 4;
      constexpr int pierceClass = 3;
      result.attacks[0] = {pierceClass, static_cast<int16_t>(
        r::ClassValue(rules->attacks, rules->attack_count, meleeClass, 0))};
      result.attack_count = 1;
      result.projectile = settings.m_spearProjectile;
      result.max_range = Fixed32(settings.m_spearRange);
      result.min_range = {};
      result.accuracy = 100;
      result.dispersion = {};
      result.blast_width = {};
      result.blast_level = 3;
    }
    return true;
  }

  const r::UnitRules* fightingRules(const entt::registry& registry, entt::entity entity)
  {
    const auto* batteries = registry.ctx().find<Batteries>();
    if (batteries == nullptr) return nullptr;
    const auto found = batteries->m_profiles.find(entity);
    return found != batteries->m_profiles.end() ? &found->second : nullptr;
  }

  void refresh(entt::registry& registry, entt::entity entity)
  {
    if (!isHorse(registry, entity)) return;
    auto* batteries = registry.ctx().find<Batteries>();
    if (batteries == nullptr) batteries = &registry.ctx().emplace<Batteries>();
    const auto* base = Players::RulesFor(registry, entity, Core::trojan_horse::unitId);
    const auto* garrison = registry.try_get<c::Garrison>(entity);
    auto profile = base != nullptr ? *base : r::UnitRules{};
    profile.max_range = {};
    profile.reload = {};
    int damage = 0;
    int count = 0;
    if (base != nullptr && Combat::Alive(registry, entity) && garrison != nullptr)
    {
      for (const auto passenger : garrison->inside)
      {
        r::UnitRules shot;
        if (!weapon(registry, passenger, shot)) continue;
        profile.min_range = count == 0 ? shot.min_range : std::min(profile.min_range, shot.min_range);
        ++count;
        profile.projectile = shot.projectile;
        profile.max_range = std::max(profile.max_range, shot.max_range);
        profile.reload = std::max(profile.reload, shot.reload);
        damage += r::ClassValue(shot.attacks, shot.attack_count, 3, 0);
      }
    }
    if (count == 0)
    {
      batteries->m_profiles.erase(entity);
      if (auto* combat = registry.try_get<c::Combat>(entity))
      {
        if (combat->state != c::CombatState::Idle) registry.remove<c::Path>(entity);
        combat->state = c::CombatState::Idle;
        combat->target = entt::null;
        combat->ordered = false;
        combat->strike_ms = -1;
        combat->has_post = false;
      }
      return;
    }
    profile.attack_count = 1;
    profile.attacks[0] = {3, static_cast<int16_t>(std::min(damage, 32767))};
    profile.frame_delay = 0;
    profile.search_radius = profile.max_range;
    profile.combat_level = 4;
    batteries->m_profiles.insert_or_assign(entity, profile);
    if (!registry.all_of<c::Combat>(entity))
    {
      auto& combat = registry.emplace<c::Combat>(entity);
      combat.stance = c::Stance::StandGround;
      Combat::FirstLook(registry, entity);
    }
  }

  void refreshAll(entt::registry& registry)
  {
    if (auto* batteries = registry.ctx().find<Batteries>())
      std::erase_if(batteries->m_profiles, [&](const auto& item)
      {
        return !registry.valid(item.first) || !Combat::Alive(registry, item.first);
      });
    for (const auto entity : registry.view<const c::Garrison>()) refresh(registry, entity);
  }
}
