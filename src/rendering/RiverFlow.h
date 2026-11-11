#pragma once

#include <SDL2/SDL.h>

namespace ECS { class Simulation; }
namespace Rendering { class Renderer; }

namespace Rendering::river_flow
{
  // Draw the visible current without changing movement or water levels.
  void draw(const ECS::Simulation& sim, const Renderer& renderer, SDL_Renderer* sdl);
}
