#include "rendering/RiverFlow.h"

#include <algorithm>
#include <array>
#include <cmath>
#include "core/Command.h"
#include "core/IsometricMath.h"
#include "rendering/Ground.h"
#include "rendering/Renderer.h"

namespace Rendering::river_flow
{
  void draw(const ECS::Simulation& sim, const Renderer& renderer, SDL_Renderer* sdl)
  {
    const auto& grid = Ground::SeenGrid(sim);
    const float margin = (Physics::earthworkMaxLevel + 2) * Ground::kLevelPx;
    float minX = static_cast<float>(grid.GetWidth()), minY = static_cast<float>(grid.GetHeight());
    float maxX = 0, maxY = 0;
    for (const float sx : {renderer.camera_x - 96, renderer.camera_x + renderer.view_w / renderer.camera_zoom + 96})
      for (const float sy : {renderer.camera_y - margin, renderer.camera_y + renderer.view_h / renderer.camera_zoom + margin})
      {
        const auto [x, y] = Core::IsometricMath::ScreenToWorld(sx, sy);
        minX = std::min(minX, x); maxX = std::max(maxX, x);
        minY = std::min(minY, y); maxY = std::max(maxY, y);
      }
    const int left = std::max(0, static_cast<int>(std::floor(minX)));
    const int right = std::min(grid.GetWidth() - 1, static_cast<int>(std::ceil(maxX)));
    const int top = std::max(0, static_cast<int>(std::floor(minY)));
    const int bottom = std::min(grid.GetHeight() - 1, static_cast<int>(std::ceil(maxY)));
    SDL_Color oldColour;
    SDL_BlendMode oldBlend;
    SDL_GetRenderDrawColor(sdl, &oldColour.r, &oldColour.g, &oldColour.b, &oldColour.a);
    SDL_GetRenderDrawBlendMode(sdl, &oldBlend);
    SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_BLEND);
    const float time = static_cast<float>(sim.Tick() % 125) / 125.0f;
    for (int y = top; y <= bottom; ++y)
      for (int x = left; x <= right; ++x)
      {
        const auto& tile = grid.GetTile(x, y);
        if (tile.m_flowDirection == 0 || tile.terrain != Core::TerrainType::MediumWater) continue;
        const int dx = Core::riverStepX(tile.m_flowDirection), dy = Core::riverStepY(tile.m_flowDirection);
        for (int streak = 0; streak < 2; ++streak)
        {
          const float phase = std::fmod(time + static_cast<float>((x * 7 + y * 13) % 17) / 17.0f + streak * 0.5f, 1.0f);
          const auto alpha = static_cast<Uint8>(130 * (1 - std::abs(2 * phase - 1)));
          SDL_SetRenderDrawColor(sdl, 185, 233, 244, alpha);
          std::array<SDL_FPoint, 3> points;
          for (int point = 0; point < 3; ++point)
          {
            const float along = -0.25f + phase * 0.5f + point * 0.05f;
            const float across = (streak == 0 ? -0.1f : 0.1f) + (point == 1 ? 0.025f : 0);
            const float wx = x + dx * along - dy * across, wy = y + dy * along + dx * across;
            points[point] = {(wx - wy) * 48 - renderer.camera_x,
              (wx + wy) * 24 - renderer.camera_y - Ground::HeightPx(grid, wx, wy)};
          }
          SDL_RenderDrawLinesF(sdl, points.data(), static_cast<int>(points.size()));
        }
      }
    SDL_SetRenderDrawColor(sdl, oldColour.r, oldColour.g, oldColour.b, oldColour.a);
    SDL_SetRenderDrawBlendMode(sdl, oldBlend);
  }
}
