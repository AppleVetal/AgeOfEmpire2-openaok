#include <cstdio>
#include "net/Wire.h"

int main()
{
  Net::Wire::Writer bytes;
  bytes.U8(43); // First extension command; original commands keep their indices.
  bytes.U16(1);
  bytes.Entity(static_cast<entt::entity>(7));
  bytes.U8(1); // PlantTree
  bytes.I32(12);
  bytes.I32(13);
  bytes.U8(1); // Append to the villager's queue.
  bytes.U8(1); // Player.
  Net::Wire::Reader input(bytes.bytes);
  Core::Command command;
  const bool passed = Net::Wire::Read(input, command) && input.Done();
  std::printf("worldjobwire=%s\n", passed ? "ok" : "plant order is not supported");
  return passed ? 0 : 1;
}
