#pragma once

#include <entt/entt.hpp>

namespace ECS
{
  class Simulation;
}

namespace ECS::wild_trees
{
  // Places the neutral population after a random map and its starting units.
  void populate(Simulation& simulation);
  // A claimed wild tree becomes a normal soldier, with no further herding.
  void adopt(entt::registry& registry);
}
