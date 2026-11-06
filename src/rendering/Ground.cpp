#include "Ground.h"

#include <algorithm>
#include <cmath>

#include "core/IsometricMath.h"
#include "ecs/Simulation.h"
#include "ecs/Fog.h"
#include "ecs/Vision.h"

namespace Rendering::Ground {

namespace {

int Level(const Physics::Grid& grid, int x, int y) {
    x = std::clamp(x, 0, grid.GetWidth() - 1);
    y = std::clamp(y, 0, grid.GetHeight() - 1);
    return grid.GetTile(x, y).elevation;
}

// How much each level of slope across a tile turns it toward or away from
// the light.
constexpr float kPerLevel = 0.5f;
uint32_t seenRevision = 0;

} // namespace

const Physics::Grid& SeenGrid(const ECS::Simulation& sim, Core::PlayerId viewer)
{
    static Physics::Grid shown(1, 1);
    static uint32_t sourceSerial = 0, terrainRevision = 0, memoryRevision = 0;
    static Core::PlayerId lastViewer = Core::PLAYER_NONE;
    static uint16_t lastSeeAll = 0;
    const auto& grid = sim.GetGrid();
    const auto& registry = sim.GetRegistry();
    const auto* memory = ECS::Fog::Read(registry);
    const auto* vision = ECS::Vision::MapOf(registry);
    const auto revision = memory != nullptr ? memory->revision : 0;
    const uint16_t seeAll = vision != nullptr ? vision->see_all : 0;
    const bool hasMemory = memory != nullptr && viewer < Core::kMaxPlayers && !memory->ground[viewer].empty();
    if (sourceSerial == grid.Serial() && terrainRevision == grid.TerrainRevision() &&
        memoryRevision == revision && lastViewer == viewer && lastSeeAll == seeAll) return hasMemory ? shown : grid;
    // Reuse vector capacity. Nothing is allocated or copied while the map and
    // its remembered surface are unchanged, including ordinary unit movement.
    if (hasMemory)
    {
        shown = grid;
        for (const auto& remembered : memory->ground[viewer])
        {
            const int x = remembered.tile % grid.GetWidth(), y = remembered.tile / grid.GetWidth();
            if (!grid.InBounds(x, y) || ECS::Vision::Visible(registry, viewer, x, y)) continue;
            auto& tile = shown.GetTileMut(x, y);
            tile.terrain = remembered.terrain;
            tile.elevation = remembered.elevation;
            tile.pinnedCorners = remembered.pinnedCorners;
            tile.cornerHeights = remembered.cornerHeights;
        }
    }
    sourceSerial = grid.Serial();
    terrainRevision = grid.TerrainRevision();
    memoryRevision = revision;
    lastViewer = viewer;
    lastSeeAll = seeAll;
    ++seenRevision;
    return hasMemory ? shown : grid;
}

uint32_t SeenRevision()
{
    return seenRevision;
}

float CornerLevel(const Physics::Grid& grid, int i, int j) {
    return 0.25f * static_cast<float>(grid.CornerHeight(i, j));
}

float HeightPx(const Physics::Grid& grid, float wx, float wy) {
    const float fx = wx + 0.5f;
    const float fy = wy + 0.5f;
    const int i = static_cast<int>(std::floor(fx));
    const int j = static_cast<int>(std::floor(fy));
    const float u = fx - static_cast<float>(i);
    const float v = fy - static_cast<float>(j);
    const float top = CornerLevel(grid, i, j) * (1.0f - u) + CornerLevel(grid, i + 1, j) * u;
    const float bottom =
        CornerLevel(grid, i, j + 1) * (1.0f - u) + CornerLevel(grid, i + 1, j + 1) * u;
    return (top * (1.0f - v) + bottom * v) * kLevelPx;
}

float Light(const Physics::Grid& grid, int i, int j) {
    // The slope at the corner, from the four tiles round it. The upper left of
    // the screen is world -x, a little -y; ground rising that way faces away.
    const float a = static_cast<float>(Level(grid, i - 1, j - 1));
    const float b = static_cast<float>(Level(grid, i, j - 1));
    const float c = static_cast<float>(Level(grid, i - 1, j));
    const float d = static_cast<float>(Level(grid, i, j));
    const float along_x = 0.5f * ((b + d) - (a + c));
    const float along_y = 0.5f * ((c + d) - (a + b));
    const float facing = along_x + 0.5f * along_y;   // > 0: rises away from the light
    return std::clamp(-facing * kPerLevel, -1.0f, 1.0f);
}

std::pair<float, float> Pick(const Physics::Grid& grid, float screen_x, float screen_y) {
    // Intersect the viewing ray from front to back. Fixed-point iteration
    // oscillates on a steep mountain and can select ground behind its summit.
    constexpr float step = kLevelPx / 4.0f;
    float upper = Physics::earthworkMaxLevel * kLevelPx;
    for (float height = upper; height >= Physics::earthworkMinLevel * kLevelPx; height -= step) {
        const auto candidate = Core::IsometricMath::ScreenToWorld(screen_x, screen_y + height);
        if (HeightPx(grid, candidate.first, candidate.second) >= height) {
            float low = height, high = upper;
            for (int i = 0; i < 10; ++i) {
                const float mid = (low + high) * 0.5f;
                const auto at = Core::IsometricMath::ScreenToWorld(screen_x, screen_y + mid);
                if (HeightPx(grid, at.first, at.second) >= mid) low = mid;
                else high = mid;
            }
            return Core::IsometricMath::ScreenToWorld(screen_x, screen_y + (low + high) * 0.5f);
        }
        upper = height;
    }
    // The ground drawn at a screen point stands higher on a hill, so the world
    // point under it is further down the screen than a flat map says. A few
    // rounds of correction settle it.
    auto world = Core::IsometricMath::ScreenToWorld(screen_x, screen_y);
    for (int round = 0; round < 3; ++round) {
        const float lift = HeightPx(grid, world.first, world.second);
        world = Core::IsometricMath::ScreenToWorld(screen_x, screen_y + lift);
    }
    return world;
}

} // namespace Rendering::Ground
