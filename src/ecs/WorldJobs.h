#pragma once

#include <entt/entt.hpp>
#include "core/Command.h"
#include "core/Worldcraft.h"
#include "physics/Grid.h"

namespace ECS::world_jobs
{
  constexpr int activeSiteLimit = Core::worldcraft::siteLimit;
  bool canPlant(const entt::registry& registry, const Physics::Grid& grid,
    Core::PlayerId player, int x, int y);
  bool order(entt::registry& registry, Physics::Grid& grid, const Core::WorldJobCommand& command);
  std::vector<std::pair<int, int>> layout(const Physics::Grid& grid, int fromX, int fromY,
    int toX, int toY, Core::PlantShape shape);
  void plantLayout(entt::registry& registry, Physics::Grid& grid, const Core::PlantLayoutCommand& command);
  void cancel(entt::registry& registry, Physics::Grid& grid, entt::entity site, Core::PlayerId player);
  void prepare(entt::registry& registry);
  void update(entt::registry& registry, Physics::Grid& grid, int32_t dtMs);
  void observe(entt::registry& registry);
}
