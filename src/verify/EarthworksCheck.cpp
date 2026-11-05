#include "verify/Harness.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include "core/CommandQueue.h"
#include "core/Worldcraft.h"
#include "ecs/Buildings.h"
#include "ecs/Combat.h"
#include "ecs/Digest.h"
#include "ecs/Earthworks.h"
#include "ecs/Fog.h"
#include "ecs/Orders.h"
#include "ecs/Players.h"
#include "ecs/Simulation.h"
#include "ecs/Vision.h"
#include "ecs/WorldJobs.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Selectable.h"
#include "ecs/components/Worker.h"
#include "ecs/components/WorldJob.h"
#include "net/Wire.h"
#include "pathfinding/Pathfinder.h"
#include "rendering/Ground.h"
#include "rendering/UIRenderer.h"
#include "save/Recording.h"
#include "save/Save.h"

namespace Verify
{
  namespace
  {
    namespace c = ECS::Components;
    using Core::Fixed32;
    using Core::WorldJobKind;

    Core::GameSetup setup()
    {
      auto result = Core::DefaultSetup();
      result.reveal = Core::Reveal::AllVisible;
      result.resources = Core::StartingResources::DeathMatch;
      for (auto& player : result.players) player.computer = false;
      return result;
    }

    std::vector<entt::entity> workers(const ECS::Simulation& sim)
    {
      std::vector<entt::entity> result;
      const auto& registry = sim.GetRegistry();
      for (const auto worker : registry.view<const c::Worker, const c::Owner>())
        if (registry.get<c::Owner>(worker).player_id == 1) result.push_back(worker);
      std::sort(result.begin(), result.end());
      return result;
    }

    bool patch(ECS::Simulation& sim, int& x, int& y)
    {
      const auto crew = workers(sim);
      if (crew.empty()) return false;
      const auto& at = sim.GetRegistry().get<c::Transform>(crew.front()).position;
      const int cx = Physics::TileOf(at.x), cy = Physics::TileOf(at.y);
      for (int radius = 3; radius <= 30; ++radius)
        for (int dy = -radius; dy <= radius; ++dy)
          for (int dx = -radius; dx <= radius; ++dx)
          {
            if (std::max(std::abs(dx), std::abs(dy)) != radius) continue;
            bool isClear = true;
            for (int row = 0; row < 3 && isClear; ++row)
              for (int col = 0; col < 3 && isClear; ++col)
                isClear = ECS::earthworks::canWork(sim.GetRegistry(), sim.GetGrid(), 1,
                  cx + dx + col, cy + dy + row, WorldJobKind::RaiseGround);
            if (isClear) { x = cx + dx; y = cy + dy; return true; }
          }
      return false;
    }

    const char* gridRules()
    {
      Physics::Grid slope(12, 12);
      if (!slope.SetEarthwork(6, 6, 1, Physics::Earthwork::Raised) || !slope.IsWalkable(6, 6))
        return "a gentle slope is not walkable";
      const auto revision = slope.WalkRevision();
      if (!slope.SetEarthwork(6, 6, 3, Physics::Earthwork::Raised) || slope.IsWalkable(6, 6) ||
          !slope.IsBlocked(6, 6) || slope.WalkRevision() == revision) return "a steep slope did not close the path";
      slope.SetWalkable(6, 6, true);
      if (slope.IsWalkable(6, 6)) return "freeing an object erased the steep slope";
      for (int y = 5; y <= 7; ++y)
        for (int x = 5; x <= 7; ++x)
          if (x != 6 || y != 6) slope.SetEarthwork(x, y, 2, Physics::Earthwork::Raised);
      if (!slope.IsWalkable(6, 6)) return "softening neighbouring slopes did not reopen the summit";
      if (slope.SetEarthwork(6, 6, Physics::earthworkMaxLevel + 1, Physics::Earthwork::Raised) ||
          slope.SetEarthwork(6, 6, Physics::earthworkMinLevel - 1, Physics::Earthwork::Ditch)) return "height limits were ignored";
      Physics::Grid ditch(12, 12);
      if (Pathfinding::RegionAt(ditch, 2, 6) != Pathfinding::RegionAt(ditch, 10, 6)) return "the initial plain is disconnected";
      for (int y = 0; y < 12; ++y) ditch.SetEarthwork(6, y, -2, Physics::Earthwork::Ditch);
      if (Pathfinding::RegionAt(ditch, 2, 6) == Pathfinding::RegionAt(ditch, 10, 6)) return "a dry ditch did not separate land routes";
      ditch.SetWalkable(6, 6, true);
      if (ditch.IsWalkable(6, 6)) return "a dry ditch lost its independent blockage";
      if (!ditch.SetEarthwork(6, 6, 0, Physics::Earthwork::None) ||
          Pathfinding::RegionAt(ditch, 2, 6) != Pathfinding::RegionAt(ditch, 10, 6)) return "filling a ditch did not reconnect the route";
      if (Pathfinding::AStar::FindPath(ditch, 2, 6, 10, 6).waypoints.empty()) return "the reopened crossing has no path";
      if (ECS::Combat::HeightAdjusted(100, 2, 0) <= ECS::Combat::HeightAdjusted(100, 0, 0)) return "raised ground has no combat bonus";
      return nullptr;
    }

    const char* orders()
    {
      ECS::Simulation sim;
      sim.Init(setup());
      auto& registry = sim.GetRegistry();
      auto& grid = sim.GetGrid();
      const auto crew = workers(sim);
      int x = 0, y = 0;
      if (!patch(sim, x, y)) return "no clear earthwork patch";
      for (const auto unit : registry.view<c::Selectable>()) registry.get<c::Selectable>(unit).is_selected = unit == crew.front();
      Rendering::UIRenderer::OpenBuildMenu(sim, 0);
      Rendering::UIRenderer::Command buttons[Rendering::UIRenderer::kCommandSlots];
      const int count = Rendering::UIRenderer::BuildCommands(sim, 1024, 768, buttons, Rendering::UIRenderer::kCommandSlots);
      int terrainButtons = 0;
      for (int i = 0; i < count; ++i)
        if (buttons[i].kind == Rendering::UIRenderer::CommandKind::Earthwork &&
            !Rendering::UIRenderer::CommandHelp(sim, buttons[i]).empty()) ++terrainButtons;
      if (terrainButtons != 6) return "a villager is missing earthwork line or area controls";
      auto& seat = *ECS::Players::Find(registry, 1);
      const int before = ECS::Players::Stock(seat, Core::ResourceType::Wood);
      Core::WorldJobCommand command{crew, WorldJobKind::RaiseGround, x, y, false, 2};
      if (ECS::world_jobs::order(registry, grid, command)) return "another player ordered our earthworkers";
      command.player = 1;
      ECS::Players::SetResource(seat, Core::Rules::Res::Wood, Fixed32(0));
      if (ECS::world_jobs::order(registry, grid, command)) return "unpaid earthworks were accepted";
      ECS::Players::SetResource(seat, Core::Rules::Res::Wood, Fixed32(before));
      if (!ECS::world_jobs::order(registry, grid, command)) return "villagers cannot raise ground";
      if (ECS::Buildings::CheckAboveGround(registry, 1, 70, x + 1, y, ECS::Buildings::Ask::Placing) == ECS::Buildings::Site::Clear)
        return "a house can overlap the reserved grading margin";
      const auto site = registry.view<c::WorldJob>().front();
      auto digest = ECS::Digest::Of(sim);
      ++registry.get<c::WorldJob>(site).targetElevation;
      if (digest == ECS::Digest::Of(sim)) return "the target elevation escaped the digest";
      --registry.get<c::WorldJob>(site).targetElevation;
      ECS::world_jobs::cancel(registry, grid, site, 1);
      if (ECS::Players::Stock(seat, Core::ResourceType::Wood) != before || grid.GetTile(x, y).earthwork != Physics::Earthwork::None)
        return "cancelling an unstarted mountain changed terrain or failed to refund";
      const int originalLevel = grid.GetTile(x, y).elevation;
      command.kind = WorldJobKind::DigDitch;
      if (!ECS::world_jobs::order(registry, grid, command)) return "a villager cannot order a dry ditch";
      for (int tick = 0; tick < 4000 && !registry.view<c::WorldJob>().empty(); ++tick) sim.Update(16);
      if (grid.IsWalkable(x, y) || grid.GetTile(x, y).earthwork != Physics::Earthwork::Ditch)
        return "villagers did not dig the dry ditch";
      if (const auto* artifacts = std::getenv("AOE_EARTHWORK_ARTIFACTS"))
      {
        Save::WriteFile(sim, (std::filesystem::path(artifacts) / "ditch.sav").string());
        std::printf("  ditch centre=%d,%d\n", x, y);
      }
      command.kind = WorldJobKind::RestoreGround;
      if (!ECS::world_jobs::order(registry, grid, command)) return "a villager cannot order ditch filling";
      for (int tick = 0; tick < 4000 && !registry.view<c::WorldJob>().empty(); ++tick) sim.Update(16);
      if (!grid.IsWalkable(x, y) || grid.GetTile(x, y).elevation != originalLevel ||
          grid.GetTile(x, y).earthwork != Physics::Earthwork::None) return "villagers did not restore the original ground";
      for (const auto kind : {WorldJobKind::RaiseGround, WorldJobKind::DigDitch, WorldJobKind::RestoreGround})
      {
        const Core::Command packet = Core::PlantLayoutCommand{crew, x, y, x + 2, y + 2, Core::PlantShape::Area, true, 1, kind};
        Net::Wire::Writer bytes, round;
        Net::Wire::Write(bytes, packet);
        Net::Wire::Reader reader(bytes.bytes);
        Core::Command decoded;
        if (!Net::Wire::Read(reader, decoded) || !reader.Done()) return "an earthwork command did not decode";
        Net::Wire::Write(round, decoded);
        if (bytes.bytes != round.bytes) return "an earthwork command changed on the wire";
        bytes.bytes.pop_back();
        Net::Wire::Reader shortReader(bytes.bytes);
        if (Net::Wire::Read(shortReader, decoded)) return "a truncated earthwork command was accepted";
      }
      return nullptr;
    }

    const char* persistence()
    {
      ECS::Simulation sim, restored;
      sim.Init(setup());
      int x = 0, y = 0;
      if (!patch(sim, x, y)) return "no recorded mountain site";
      const auto crew = workers(sim);
      int initial[9]{};
      for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col) initial[row * 3 + col] = sim.GetGrid().GetTile(x + col, y + row).elevation;
      Save::Recording recording;
      recording.setup = sim.Setup();
      auto& queue = Core::CommandQueue::Get();
      queue.PopAllCommands();
      bool hasRestore = false;
      constexpr int ticks = 14000;
      const auto* artifacts = std::getenv("AOE_EARTHWORK_ARTIFACTS");
      for (int tick = 0; tick < ticks; ++tick)
      {
        if (tick == 20) queue.PushCommand(Core::PlantLayoutCommand{crew, x, y, x + 2, y + 2,
          Core::PlantShape::Area, false, 1, WorldJobKind::RaiseGround});
        Save::TakeTurn(recording, sim.Tick() + 1);
        sim.Update(16);
        Save::Witness(recording, sim);
        if (hasRestore)
        {
          restored.Update(16);
          if (tick % 100 == 0 && ECS::Digest::Of(restored) != ECS::Digest::Of(sim)) return "earthworks diverged after loading";
        }
        if (tick == 1000)
        {
          if (sim.GetRegistry().view<c::WorldJob>().empty()) return "the mountain order created no work";
          std::vector<uint8_t> bytes;
          std::string error;
          if (!Save::Write(sim, bytes) || !Save::Read(restored, bytes, &error))
          {
            std::printf("  earthwork save: %s\n", error.c_str());
            return "the earthwork queue could not be restored";
          }
          hasRestore = true;
          if (artifacts != nullptr) Save::WriteFile(sim, (std::filesystem::path(artifacts) / "earthwork-queue.sav").string());
        }
      }
      for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col)
        {
          const auto& tile = sim.GetGrid().GetTile(x + col, y + row);
          const int expected = initial[row * 3 + col] + (row == 1 && col == 1 ? 2 : 1);
          if (tile.elevation != expected || tile.earthwork != Physics::Earthwork::Raised)
          {
            std::printf("  unfinished mountain: tile=%d,%d level=%d expected=%d jobs=%zu\n",
              x + col, y + row, tile.elevation, expected, sim.GetRegistry().view<c::WorldJob>().size());
            return "the crew did not finish the mountain slopes";
          }
        }
      if (artifacts != nullptr)
      {
        Save::WriteFile(sim, (std::filesystem::path(artifacts) / "mountain.sav").string());
        std::printf("  mountain centre=%d,%d\n", x + 1, y + 1);
      }
      const auto path = std::filesystem::temp_directory_path() / "openaok-earthworks-verify.mgl";
      Save::Recording read;
      const bool hasRecording = Save::WriteRecording(recording, path.string()) && Save::ReadRecording(path.string(), read);
      std::filesystem::remove(path);
      if (!hasRecording) return "earthwork recording did not survive its file";
      ECS::Simulation replay;
      replay.Init(read.setup);
      std::size_t cursor = 0, witness = 0;
      for (int tick = 0; tick < ticks; ++tick)
      {
        Save::Replay(read, cursor, replay.Tick() + 1);
        replay.Update(16);
        if (!Save::StillTheGame(read, witness, replay)) return "earthwork replay diverged";
      }
      if (ECS::Digest::Of(replay) != ECS::Digest::Of(sim)) return "the replay ended with different mountains";
      auto& changed = sim.GetGrid().GetTileMut(x, y);
      const auto digest = ECS::Digest::Of(sim);
      ++changed.originalElevation;
      if (ECS::Digest::Of(sim) == digest) return "the restoration height escaped the digest";
      --changed.originalElevation;
      return nullptr;
    }

    const char* fog()
    {
      auto options = setup();
      options.reveal = Core::Reveal::Explored;
      ECS::Simulation sim, restored;
      sim.Init(options);
      auto& registry = sim.GetRegistry();
      auto& grid = sim.GetGrid();
      int x = 5, y = 5;
      while (x < grid.GetWidth() - 2 && ECS::Vision::Visible(registry, 2, x, y)) ++x;
      if (x >= grid.GetWidth() - 2) return "no hidden terrain for the fog test";
      const auto original = grid.GetTile(x, y);
      ECS::Fog::GroundChanging(registry, x, y, original);
      grid.SetEarthwork(x, y, original.elevation + 2, Physics::Earthwork::Raised);
      if (ECS::Fog::ElevationSeen(registry, 2, x, y, grid.GetTile(x, y).elevation) != original.elevation ||
          Rendering::Ground::SeenGrid(sim, 2).GetTile(x, y).elevation != original.elevation)
        return "a hidden mountain leaked into the visible terrain";
      const auto digest = ECS::Digest::Of(sim);
      auto& memory = registry.ctx().get<ECS::Fog::Memory>();
      ++memory.ground[2].front().elevation;
      if (ECS::Digest::Of(sim) == digest) return "remembered height escaped the digest";
      --memory.ground[2].front().elevation;
      std::vector<uint8_t> bytes;
      if (!Save::Write(sim, bytes) || !Save::Read(restored, bytes) || ECS::Digest::Of(restored) != digest)
        return "height memory did not survive saving";
      ECS::Vision::ToggleSeeAll(registry, 2);
      if (Rendering::Ground::SeenGrid(sim, 2).GetTile(x, y).elevation != original.elevation + 2)
        return "discovering a mountain did not refresh its height";
      ECS::Fog::Update(registry);
      ECS::Vision::ToggleSeeAll(registry, 2);
      ECS::Fog::GroundChanging(registry, x, y, grid.GetTile(x, y));
      grid.SetEarthwork(x, y, -2, Physics::Earthwork::Ditch);
      const auto& shown = Rendering::Ground::SeenGrid(sim, 2).GetTile(x, y);
      if (shown.elevation != original.elevation + 2 || shown.terrain != original.terrain)
        return "a hidden ditch replaced the remembered mountain";
      return nullptr;
    }
  }

  bool CheckEarthworks()
  {
    const char* failure = gridRules();
    if (failure == nullptr) failure = orders();
    if (failure == nullptr) failure = fog();
    if (failure == nullptr) failure = persistence();
    Core::CommandQueue::Get().PopAllCommands();
    std::printf("earthworks=%s\n", failure != nullptr ? failure : "ok");
    return failure == nullptr;
  }
}
