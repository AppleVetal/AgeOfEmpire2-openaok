#pragma once

#include <entt/entt.hpp>
#include "core/Rules.h"

namespace ECS::transport_combat
{
  bool isHorse(const entt::registry& registry, entt::entity entity);
  bool isMoving(const entt::registry& registry, entt::entity entity);
  bool weapon(const entt::registry& registry, entt::entity passenger, Core::Rules::UnitRules& result);
  const Core::Rules::UnitRules* fightingRules(const entt::registry& registry, entt::entity entity);
  void refresh(entt::registry& registry, entt::entity entity);
  void refreshAll(entt::registry& registry);
}
