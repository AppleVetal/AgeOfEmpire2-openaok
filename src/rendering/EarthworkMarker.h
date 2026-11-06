#pragma once

#include <SDL2/SDL.h>
#include "core/Command.h"

namespace Rendering::earthwork_marker
{
  SDL_FRect bounds(float x, float y);
  void draw(SDL_Renderer* renderer, Core::WorldJobKind kind, float x, float y,
    float progress, bool ghost = false, bool clear = true, bool fogged = false);
}
