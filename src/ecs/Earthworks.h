#pragma once

#include "core/Command.h"
#include "ecs/BuildingGeometry.h"
#include "physics/Grid.h"

namespace ECS::earthworks
{
  bool isWater(Core::TerrainType terrain);
  int radius(Core::WorldJobKind kind, int rise);
  Buildings::Bounds bounds(int x, int y, Core::WorldJobKind kind = Core::WorldJobKind::DigDitch, int rise = 1);
  int targetLevel(const Physics::Tile& tile, Core::WorldJobKind kind, int levels = 1);
  bool canWork(const entt::registry& registry, const Physics::Grid& grid,
    Core::PlayerId player, int x, int y, Core::WorldJobKind kind,
    int levels = 1, entt::entity ownSite = entt::null);
  bool finish(entt::registry& registry, Physics::Grid& grid, entt::entity site);
}
