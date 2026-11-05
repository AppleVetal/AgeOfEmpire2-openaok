#include "ecs/Earthworks.h"

#include <algorithm>
#include "ecs/Fog.h"
#include "ecs/Navigation.h"
#include "ecs/Vision.h"
#include "ecs/components/Building.h"
#include "ecs/components/Health.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Mortal.h"
#include "ecs/components/Owner.h"
#include "ecs/components/WorldJob.h"

namespace ECS::earthworks
{
  namespace
  {
    namespace c = Components;
    using Core::Fixed32;
    constexpr int ditchDepth = 2;

    bool isDryGround(const Physics::Grid& grid, Core::TerrainType terrain)
    {
      return grid.LandPassable(terrain) && terrain != Core::TerrainType::ShallowWater &&
        terrain != Core::TerrainType::Swamp && terrain != Core::TerrainType::Ice &&
        terrain != Core::TerrainType::Farm && terrain != Core::TerrainType::FarmBuild1 &&
        terrain != Core::TerrainType::FarmBuild2 && terrain != Core::TerrainType::FarmBuild3;
    }
  }

  Buildings::Bounds bounds(int x, int y)
  {
    auto result = Buildings::TileBounds({Fixed32(x), Fixed32(y)});
    result.min_x = result.min_x - Fixed32(1);
    result.min_y = result.min_y - Fixed32(1);
    result.max_x = result.max_x + Fixed32(1);
    result.max_y = result.max_y + Fixed32(1);
    return result;
  }

  int targetLevel(const Physics::Tile& tile, Core::WorldJobKind kind, int levels)
  {
    if (kind == Core::WorldJobKind::RaiseGround)
      return std::min(Physics::earthworkMaxLevel, tile.elevation + std::clamp(levels, 1, Physics::earthworkMaxLevel));
    if (kind == Core::WorldJobKind::DigDitch)
      return std::max(Physics::earthworkMinLevel, tile.elevation - ditchDepth);
    return tile.originalElevation;
  }

  bool canWork(const entt::registry& registry, const Physics::Grid& grid,
    Core::PlayerId player, int x, int y, Core::WorldJobKind kind, int levels, entt::entity ownSite)
  {
    if (player == Core::PLAYER_GAIA || player >= Core::kMaxPlayers ||
        !Core::isEarthwork(kind) || !grid.InBounds(x - 1, y - 1) || !grid.InBounds(x + 1, y + 1)) return false;
    const auto& tile = grid.GetTile(x, y);
    if (tile.elevation < Physics::earthworkMinLevel || tile.elevation > Physics::earthworkMaxLevel) return false;
    if (kind == Core::WorldJobKind::RestoreGround)
    {
      if (tile.earthwork == Physics::Earthwork::None) return false;
    }
    else if (tile.earthwork == Physics::Earthwork::Ditch ||
        targetLevel(tile, kind, levels) == tile.elevation) return false;
    // Corner heights and steepness depend on a one-tile margin. Keep buildings,
    // resources and unit bodies out of the complete affected patch.
    const auto ground = bounds(x, y);
    for (int dy = -1; dy <= 1; ++dy)
      for (int dx = -1; dx <= 1; ++dx)
      {
        const auto& nearby = grid.GetTile(x + dx, y + dy);
        if (nearby.blocked || nearby.has_tree || nearby.gate != 0 ||
            !isDryGround(grid, nearby.terrain) || !Vision::Visible(registry, player, x + dx, y + dy)) return false;
      }
    for (const auto other : registry.view<const c::WorldJob>())
    {
      if (other == ownSite) continue;
      const auto& job = registry.get<c::WorldJob>(other);
      if ((job.tileX == x && job.tileY == y) ||
          (job.kind == Core::WorldJobKind::PlantTree &&
           std::abs(job.tileX - x) <= 1 && std::abs(job.tileY - y) <= 1)) return false;
    }
    for (const auto entity : registry.view<const c::Transform>())
    {
      if (registry.any_of<c::Dying, c::Corpse>(entity)) continue;
      const auto& at = registry.get<c::Transform>(entity).position;
      if (registry.all_of<c::Building, c::Identity>(entity))
      {
        const auto building = Buildings::FootprintBounds(at, registry.get<c::Identity>(entity).unit_id);
        if (ground.min_x < building.max_x && ground.max_x > building.min_x &&
            ground.min_y < building.max_y && ground.max_y > building.min_y) return false;
      }
      else if (registry.all_of<c::Health>(entity) &&
          ground.DistanceFrom(at) < Navigation::RadiusOf(registry, entity)) return false;
    }
    return true;
  }

  bool finish(entt::registry& registry, Physics::Grid& grid, entt::entity site)
  {
    const auto& job = registry.get<c::WorldJob>(site);
    const auto& tile = grid.GetTile(job.tileX, job.tileY);
    const auto player = registry.get<c::Owner>(site).player_id;
    if (!canWork(registry, grid, player, job.tileX, job.tileY, job.kind,
        std::max(1, job.targetElevation - tile.elevation), site)) return false;
    // A simultaneous neighbouring order can change access, but never this
    // site's absolute target. The old ground is remembered before it changes.
    Fog::GroundChanging(registry, job.tileX, job.tileY, tile);
    const auto kind = job.kind == Core::WorldJobKind::RaiseGround ? Physics::Earthwork::Raised :
      job.kind == Core::WorldJobKind::DigDitch ? Physics::Earthwork::Ditch : Physics::Earthwork::None;
    return grid.SetEarthwork(job.tileX, job.tileY, job.targetElevation, kind);
  }
}
