#include "verify/Harness.h"

#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <set>
#include "core/CommandQueue.h"
#include "core/ConfigManager.h"
#include "core/Worldcraft.h"
#include "ecs/Digest.h"
#include "ecs/Earthworks.h"
#include "ecs/Fog.h"
#include "ecs/Navigation.h"
#include "ecs/Orders.h"
#include "ecs/Players.h"
#include "ecs/Simulation.h"
#include "ecs/Units.h"
#include "ecs/Vision.h"
#include "ecs/WorldJobs.h"
#include "ecs/components/Building.h"
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
      for (const auto entity : registry.view<const c::Worker, const c::Owner>())
        if (registry.get<c::Owner>(entity).player_id == 1) result.push_back(entity);
      std::sort(result.begin(), result.end());
      return result;
    }

    bool patch(ECS::Simulation& sim, int& x, int& y)
    {
      const auto crew = workers(sim);
      if (crew.empty()) return false;
      const auto& at = sim.GetRegistry().get<c::Transform>(crew.front()).position;
      const int cx = Physics::TileOf(at.x), cy = Physics::TileOf(at.y);
      for (int radius = 4; radius <= 25; ++radius)
        for (int dy = -radius; dy <= radius; ++dy)
          for (int dx = -radius; dx <= radius; ++dx)
          {
            if (std::max(std::abs(dx), std::abs(dy)) != radius) continue;
            bool isClear = true;
            for (int row = -2; row <= 7 && isClear; ++row)
              for (int col = -2; col <= 7 && isClear; ++col)
              {
                const int tx = cx + dx + col, ty = cy + dy + row;
                isClear = sim.GetGrid().InBounds(tx, ty) && sim.GetGrid().IsWalkable(tx, ty) &&
                  sim.GetGrid().GetTile(tx, ty).elevation == 0 &&
                  ECS::earthworks::canWork(sim.GetRegistry(), sim.GetGrid(), 1, tx, ty, WorldJobKind::DigRiver);
              }
            if (isClear) { x = cx + dx; y = cy + dy; return true; }
          }
      return false;
    }

    const char* layoutsAndWire()
    {
      Physics::Grid grid(200, 200);
      Core::PlantLayoutCommand command{{}, 10, 10, 14, 10, Core::PlantShape::Line, false, 1,
        WorldJobKind::DigRiver, Core::mountainHeight, 3};
      if (ECS::world_jobs::layout(grid, command).size() != 21) return "incorrect width-three banks";
      command.m_width = 1; command.toY = 14;
      auto tiles = ECS::world_jobs::layout(grid, command);
      std::set<std::pair<int, int>> pending(tiles.begin(), tiles.end());
      if (tiles.empty() || pending.size() != tiles.size()) return "empty or duplicate river cells";
      std::vector<std::pair<int, int>> reached{tiles.front()};
      pending.erase(tiles.front());
      for (std::size_t i = 0; i < reached.size(); ++i)
      {
        const auto [x, y] = reached[i];
        for (const auto& next : {std::pair{x + 1, y}, {x - 1, y}, {x, y + 1}, {x, y - 1}})
          if (pending.erase(next) != 0) reached.push_back(next);
      }
      if (!pending.empty()) return "diagonal water cells are disconnected";
      std::swap(command.fromX, command.toX); std::swap(command.fromY, command.toY);
      auto reversed = ECS::world_jobs::layout(grid, command);
      std::sort(tiles.begin(), tiles.end()); std::sort(reversed.begin(), reversed.end());
      if (tiles != reversed || Core::riverDirection(10, 10, 14, 14) == Core::riverDirection(14, 14, 10, 10))
        return "reversed river changed footprint or kept current";
      command.m_width = 2;
      if (!ECS::world_jobs::layout(grid, command).empty()) return "invalid width accepted";
      command.m_width = 5; command.toX = 190;
      if (!ECS::world_jobs::layout(grid, command).empty()) return "oversized river partially accepted";
      command.toX = 10; command.kind = WorldJobKind::PlantTree;
      if (!ECS::world_jobs::layout(grid, command).empty()) return "river width applied to trees";
      for (const auto kind : {WorldJobKind::DigRiver, WorldJobKind::DigWaterMoat})
        for (const int width : {1, 3, 5})
        {
          command.kind = kind; command.m_width = width;
          Net::Wire::Writer bytes, round;
          Net::Wire::Write(bytes, Core::Command(command));
          Net::Wire::Reader reader(bytes.bytes);
          Core::Command decoded;
          if (!Net::Wire::Read(reader, decoded) || !reader.Done()) return "water order failed to decode";
          Net::Wire::Write(round, decoded);
          if (bytes.bytes != round.bytes) return "water order lost fields";
          auto invalidCommand = command;
          invalidCommand.m_width = 2;
          bytes.bytes.clear();
          Net::Wire::Write(bytes, Core::Command(invalidCommand));
          Net::Wire::Reader invalid(bytes.bytes);
          if (Net::Wire::Read(invalid, decoded)) return "invalid wire width accepted";
          bytes.bytes = round.bytes;
          bytes.bytes.pop_back();
          Net::Wire::Reader truncated(bytes.bytes);
          if (Net::Wire::Read(truncated, decoded)) return "truncated water order accepted";
        }
      for (uint8_t direction = 1; direction <= 8; ++direction)
      {
        Net::Wire::Writer bytes, round;
        Net::Wire::Write(bytes, Core::Command(Core::WorldJobCommand{{}, WorldJobKind::DigRiver,
          10, 10, false, 1, Core::mountainHeight, direction}));
        Net::Wire::Reader reader(bytes.bytes);
        Core::Command decoded;
        if (!Net::Wire::Read(reader, decoded)) return "current failed to decode";
        Net::Wire::Write(round, decoded);
        if (bytes.bytes != round.bytes) return "current changed in transit";
        bytes.bytes.clear();
        Net::Wire::Write(bytes, Core::Command(Core::WorldJobCommand{{}, WorldJobKind::DigRiver,
          10, 10, false, 1, Core::mountainHeight, 9}));
        Net::Wire::Reader invalid(bytes.bytes);
        if (Net::Wire::Read(invalid, decoded)) return "invalid current accepted";
      }
      return nullptr;
    }

    const char* navigation()
    {
      Physics::Grid grid(12, 12);
      std::vector<bool> land(64, true), sea(64, false);
      land[static_cast<int>(Core::TerrainType::MediumWater)] = false;
      sea[static_cast<int>(Core::TerrainType::MediumWater)] = true;
      grid.SetLayers({land, sea}, {0, 1});
      for (int y = 0; y < 12; ++y)
        for (int x = 5; x <= 7; ++x) grid.SetEarthwork(x, y, -2, Physics::Earthwork::River, 3);
      if (grid.IsWalkable(6, 6) || !grid.Passable(1, 6, 6) ||
          Pathfinding::RegionAt(grid, 2, 6) == Pathfinding::RegionAt(grid, 10, 6)) return "river navigation is incorrect";
      if (Pathfinding::AStar::FindPath(grid, 6, 2, 6, 10, 1).waypoints.empty()) return "no ship route along river";
      for (int x = 5; x <= 7; ++x) grid.SetEarthwork(x, 6, -2, Physics::Earthwork::WaterMoat);
      if (grid.Passable(1, 6, 6) || grid.IsWalkable(6, 6)) return "water moat allowed a crossing";
      for (int x = 5; x <= 7; ++x) grid.SetEarthwork(x, 6, 0, Physics::Earthwork::None);
      if (Pathfinding::RegionAt(grid, 2, 6) != Pathfinding::RegionAt(grid, 10, 6) || grid.GetTile(6, 6).m_flowDirection != 0)
        return "filling did not restore land";
      auto corrupt = grid.Tiles(); corrupt[0].m_flowDirection = 9;
      if (grid.RestoreTiles(corrupt)) return "invalid saved current accepted";
      corrupt = grid.Tiles(); corrupt[0].m_flowDirection = 1;
      if (grid.RestoreTiles(corrupt)) return "current on dry land accepted";
      return nullptr;
    }

    const char* orders()
    {
      ECS::Simulation sim, restored;
      sim.Init(setup());
      auto& registry = sim.GetRegistry(); auto& grid = sim.GetGrid();
      const auto crew = workers(sim);
      int x = 0, y = 0;
      if (!patch(sim, x, y)) return "no clear river test site";
      for (const auto entity : registry.view<c::Selectable>()) registry.get<c::Selectable>(entity).is_selected = entity == crew.front();
      using UI = Rendering::UIRenderer;
      UI::OpenBuildMenu(sim, UI::waterworkPage);
      UI::Command buttons[UI::kCommandSlots];
      const int count = UI::BuildCommands(sim, 1024, 768, buttons, UI::kCommandSlots);
      int widths = 0, works = 0;
      for (int i = 0; i < count; ++i)
      {
        if (buttons[i].kind == UI::CommandKind::WaterWidth) ++widths;
        if (buttons[i].kind == UI::CommandKind::Earthwork && !UI::CommandHelp(sim, buttons[i]).empty()) ++works;
      }
      if (widths != 3 || works != 6) return "incomplete waterwork menu";
      UI::OpenBuildMenu(sim, 0);
      Core::WorldJobCommand single{crew, WorldJobKind::DigRiver, x, y, false, 2};
      if (ECS::world_jobs::order(registry, grid, single)) return "foreign water order accepted";
      single.player = 1;
      auto& seat = *ECS::Players::Find(registry, 1);
      const int stock = ECS::Players::Stock(seat, Core::ResourceType::Wood);
      ECS::Players::SetResource(seat, Core::Rules::Res::Wood, Fixed32(0));
      if (ECS::world_jobs::order(registry, grid, single)) return "unpaid water order accepted";
      ECS::Players::SetResource(seat, Core::Rules::Res::Wood, Fixed32(stock));
      if (!ECS::world_jobs::order(registry, grid, single)) return "valid water order rejected";
      const auto site = registry.view<c::WorldJob>().front();
      if (ECS::Players::Stock(seat, Core::ResourceType::Wood) != stock - Core::worldcraft::workRules(single.kind).woodCost * 2)
        return "incorrect excavation price";
      ECS::world_jobs::cancel(registry, grid, site, 1);
      if (ECS::Players::Stock(seat, Core::ResourceType::Wood) != stock) return "unstarted work was not refunded";
      for (const auto entity : registry.view<const c::Building, const c::Transform>())
      {
        const auto& at = registry.get<c::Transform>(entity).position;
        for (const auto kind : {WorldJobKind::DigRiver, WorldJobKind::DigWaterMoat})
          if (ECS::earthworks::canWork(registry, grid, 1, Physics::TileOf(at.x), Physics::TileOf(at.y), kind))
            return "waterwork overlaps a building";
      }
      return nullptr;
    }

    const char* construction()
    {
      ECS::Simulation sim, restored;
      sim.Init(setup());
      const auto crew = workers(sim);
      int x = 0, y = 0;
      if (!patch(sim, x, y)) return "no clear river test site";
      Save::Recording recording; recording.setup = sim.Setup();
      auto& commands = Core::CommandQueue::Get(); commands.PopAllCommands();
      constexpr int ticks = 35000;
      bool hasRestore = false;
      for (int tick = 0; tick < ticks; ++tick)
      {
        if (tick == 20)
        {
          commands.PushCommand(Core::PlantLayoutCommand{crew, x, y, x + 4, y, Core::PlantShape::Line,
            false, 1, WorldJobKind::DigRiver, Core::mountainHeight, 3});
          commands.PushCommand(Core::PlantLayoutCommand{crew, x, y + 5, x + 1, y + 6, Core::PlantShape::Area,
            true, 1, WorldJobKind::DigWaterMoat});
        }
        Save::TakeTurn(recording, sim.Tick() + 1); sim.Update(16); Save::Witness(recording, sim);
        if (hasRestore)
        {
          restored.Update(16);
          if (tick % 250 == 0 && ECS::Digest::Of(restored) != ECS::Digest::Of(sim)) return "save continuation diverged";
        }
        if (tick == 500)
        {
          if (sim.GetRegistry().view<c::WorldJob>().size() != 25) return "water cells were not all reserved";
          std::vector<uint8_t> bytes;
          if (!Save::Write(sim, bytes) || !Save::Read(restored, bytes)) return "unfinished waterworks cannot be saved";
          hasRestore = true;
        }
      }
      if (!sim.GetRegistry().view<c::WorldJob>().empty())
      {
        for (const auto entity : sim.GetRegistry().view<c::WorldJob>())
        {
          const auto& job = sim.GetRegistry().get<c::WorldJob>(entity);
          std::printf("  river pending=%d,%d work=%d/%d\n", job.tileX, job.tileY, job.workedMs.ToInt(), job.workMs);
        }
        return "villagers did not finish waterworks";
      }
      for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 5; ++dx)
          if (sim.GetGrid().GetTile(x + dx, y + dy).earthwork != Physics::Earthwork::River ||
              sim.GetGrid().GetTile(x + dx, y + dy).m_flowDirection != 1) return "river cells or current missing";
      for (int dy = 5; dy <= 6; ++dy)
        for (int dx = 0; dx <= 1; ++dx)
          if (sim.GetGrid().GetTile(x + dx, y + dy).earthwork != Physics::Earthwork::WaterMoat) return "moat incomplete";
      ECS::Simulation replay; replay.Init(recording.setup);
      std::size_t cursor = 0, witness = 0;
      const auto file = std::filesystem::temp_directory_path() / "openaok-rivers.aokrec";
      Save::Recording read;
      const bool hasRecording = Save::WriteRecording(recording, file.string()) && Save::ReadRecording(file.string(), read);
      std::filesystem::remove(file);
      if (!hasRecording) return "river recording file unreadable";
      for (int tick = 0; tick < ticks; ++tick)
      {
        Save::Replay(read, cursor, replay.Tick() + 1); replay.Update(16);
        if (!Save::StillTheGame(read, witness, replay)) return "river replay diverged";
      }
      if (ECS::Digest::Of(sim) != ECS::Digest::Of(replay)) return "replay ended in another world";
      const auto ship = ECS::Units::Spawn(sim.GetRegistry(), Core::ConfigManager::Get().GetUnitIdByName("SGALY"), 1,
        {Fixed32(x), Fixed32(y)});
      if (ship == entt::null || !ECS::Navigation::CanStand(sim.GetRegistry(), sim.GetGrid(), ship, {Fixed32(x), Fixed32(y)}))
        return "galley cannot enter completed river";
      ECS::Orders::Execute(sim.GetRegistry(), sim.GetGrid(), Core::MoveCommand{ship, Fixed32(x + 4), Fixed32(y), 1});
      for (int tick = 0; tick < 1200; ++tick) sim.Update(16);
      const auto& destination = sim.GetRegistry().get<c::Transform>(ship).position;
      if (std::abs((destination.x - Fixed32(x + 4)).value) > Fixed32(1).value ||
          std::abs((destination.y - Fixed32(y)).value) > Fixed32(1).value) return "galley failed to sail along river";
      if (const auto* artifacts = std::getenv("AOE_RIVER_ARTIFACTS"))
      {
        if (!Save::WriteFile(sim, (std::filesystem::path(artifacts) / "waterworks.sav").string())) return "cannot save river capture";
        std::printf("  river capture centre=%d,%d\n", x + 2, y + 2);
      }
      std::vector<uint8_t> complete;
      if (!Save::Write(sim, complete) || !Save::Read(restored, complete) || ECS::Digest::Of(sim) != ECS::Digest::Of(restored))
        return "completed waterworks cannot be saved";
      ECS::Orders::Execute(sim.GetRegistry(), sim.GetGrid(), Core::PlantLayoutCommand{crew,
        x, y + 5, x + 1, y + 6, Core::PlantShape::Area, false, 1, WorldJobKind::RestoreGround});
      for (int tick = 0; tick < 12000 && !sim.GetRegistry().view<c::WorldJob>().empty(); ++tick) sim.Update(16);
      for (int dy = 5; dy <= 6; ++dy)
        for (int dx = 0; dx <= 1; ++dx)
          if (!sim.GetGrid().IsWalkable(x + dx, y + dy) || sim.GetGrid().GetTile(x + dx, y + dy).earthwork != Physics::Earthwork::None)
            return "villagers did not fill moat";
      return nullptr;
    }

    const char* fog()
    {
      auto options = setup(); options.reveal = Core::Reveal::Explored;
      ECS::Simulation sim, restored; sim.Init(options);
      auto& registry = sim.GetRegistry(); auto& grid = sim.GetGrid();
      int x = 5;
      while (x < grid.GetWidth() - 2 && ECS::Vision::Visible(registry, 2, x, 5)) ++x;
      if (x >= grid.GetWidth() - 2) return "no hidden cell for fog check";
      ECS::Fog::GroundChanging(registry, x, 5, grid.GetTile(x, 5));
      grid.SetEarthwork(x, 5, -2, Physics::Earthwork::River, 4);
      if (Rendering::Ground::SeenGrid(sim, 2).GetTile(x, 5).m_flowDirection != 0) return "current leaked through fog";
      auto& memory = registry.ctx().get<ECS::Fog::Memory>().ground[2];
      const auto digest = ECS::Digest::Of(sim);
      memory.front().m_flowDirection = 2;
      if (ECS::Digest::Of(sim) == digest) return "remembered current escaped digest";
      memory.front().m_flowDirection = 0;
      std::vector<uint8_t> bytes;
      if (!Save::Write(sim, bytes) || !Save::Read(restored, bytes) || ECS::Digest::Of(restored) != digest)
        return "fog current cannot be saved";
      ECS::Vision::ToggleSeeAll(registry, 2);
      if (Rendering::Ground::SeenGrid(sim, 2).GetTile(x, 5).m_flowDirection != 4) return "visible current did not refresh";
      return nullptr;
    }
  }

  bool CheckRivers()
  {
    const char* failure = nullptr;
    const char* (*checks[])() = {layoutsAndWire, navigation, orders, construction, fog};
    for (const auto check : checks)
    {
      if (const auto* error = check())
      {
        std::printf("  river check: %s\n", error);
        if (failure == nullptr) failure = error;
      }
      Core::CommandQueue::Get().PopAllCommands();
    }
    std::printf("rivers=%s\n", failure != nullptr ? failure : "ok");
    return failure == nullptr;
  }
}
