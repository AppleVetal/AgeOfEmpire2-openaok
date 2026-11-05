#pragma once

#include <array>
#include <entt/entt.hpp>
#include "core/Command.h"

namespace ECS::Components
{
  enum class WorldJobPhase : uint8_t { Working, Growing };

  // Хранит оплаченное задание на клетке и его прогресс в миллисекундах труда.
  struct WorldJob
  {
    Core::WorldJobKind kind = Core::WorldJobKind::PlantTree;
    WorldJobPhase phase = WorldJobPhase::Working;
    int32_t tileX = 0;
    int32_t tileY = 0;
    int32_t paidWood = 0;
    int32_t workMs = 0;
    Core::Fixed32 workedMs{};
    int32_t growthLeftMs = 0;
    bool hasBlocked = false;
    int32_t targetElevation = 0;
  };

  constexpr std::size_t worldJobQueueLimit = 256;
  // Содержит упорядоченные ссылки на участки работы одного жителя.
  struct WorldJobQueue
  {
    std::array<entt::entity, worldJobQueueLimit> sites{};
    uint16_t count = 0;
  };

  // Помнит, какие игроки уже видели посаженный во время матча объект.
  struct Planted
  {
    uint16_t seen = 0;
  };
}
