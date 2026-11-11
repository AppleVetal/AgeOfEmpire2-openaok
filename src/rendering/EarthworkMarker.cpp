#include "rendering/EarthworkMarker.h"

#include <algorithm>
#include <array>

namespace Rendering::earthwork_marker
{
  SDL_FRect bounds(float x, float y) { return {x - 31.0f, y - 40.0f, 62.0f, 56.0f}; }

  void draw(SDL_Renderer* renderer, Core::WorldJobKind kind, float x, float y,
    float progress, bool ghost, bool clear, bool fogged)
  {
    constexpr std::array<SDL_FPoint, 8> rim{{{0, -12}, {22, -8}, {30, 0}, {22, 10},
      {0, 15}, {-22, 10}, {-30, 0}, {-22, -8}}};
    std::array<SDL_Vertex, 72> vertices{};
    int count = 0;
    const bool lake = kind == Core::WorldJobKind::DigLake || Core::isWaterwork(kind);
    const bool hole = kind == Core::WorldJobKind::DigDitch || kind == Core::WorldJobKind::DigChannel || lake;
    const float stage = std::clamp(progress, 0.0f, 1.0f);
    const float peak = kind == Core::WorldJobKind::RestoreGround ? -5.0f : -24.0f - 12.0f * stage;
    auto colour = [&](int r, int g, int b)
    {
      if (!clear) { r = 210; g /= 3; b /= 3; }
      if (fogged) { r /= 2; g /= 2; b /= 2; }
      return SDL_Color{static_cast<Uint8>(r), static_cast<Uint8>(g), static_cast<Uint8>(b), static_cast<Uint8>(ghost ? 180 : 255)};
    };
    auto add = [&](SDL_FPoint point, SDL_Color tint)
    {
      vertices[count++] = {{x + point.x, y + point.y}, tint, {0, 0}};
    };
    for (int i = 0; i < 8; ++i)
    {
      const auto a = rim[i], b = rim[(i + 1) % 8];
      const int shade = i >= 3 && i <= 6 ? 28 : 0;
      const auto earth = colour(150 - shade, 116 - shade, 65 - shade);
      if (!hole)
      {
        add(a, earth); add(b, earth);
        add({-3, peak}, colour(111 - shade, 122 - shade, 66 - shade));
      }
      else
      {
        const SDL_FPoint insideA{a.x * 0.68f, a.y * 0.56f + 3.0f};
        const SDL_FPoint insideB{b.x * 0.68f, b.y * 0.56f + 3.0f};
        const auto wall = colour(85 - shade, 64 - shade, 37 - shade);
        add(a, earth); add(b, earth); add(insideA, wall);
        add(b, earth); add(insideB, wall); add(insideA, wall);
        const auto bottom = lake ? colour(49, 106, 144) : colour(49, 37, 23);
        add(insideA, bottom); add(insideB, bottom); add({0, 4}, bottom);
      }
    }
    SDL_RenderGeometry(renderer, nullptr, vertices.data(), count, nullptr, 0);
  }
}
