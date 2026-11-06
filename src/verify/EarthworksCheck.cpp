#include "verify/Harness.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include "core/CommandQueue.h"
#include "core/ConfigManager.h"
#include "core/IsometricMath.h"
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
#include "ecs/Units.h"
#include "ecs/components/Building.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Resource.h"
#include "ecs/components/Health.h"
#include "ecs/components/Sprite.h"
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
      if (ECS::earthworks::targetLevel(slope.GetTile(6, 6), WorldJobKind::RaiseGround, 12) != 12)
        return "a tower-height mountain is capped at a small hill";
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
      Rendering::UIRenderer::OpenBuildMenu(sim, Rendering::UIRenderer::earthworkPage);
      Rendering::UIRenderer::Command buttons[Rendering::UIRenderer::kCommandSlots];
      const int count = Rendering::UIRenderer::BuildCommands(sim, 1024, 768, buttons, Rendering::UIRenderer::kCommandSlots);
      int terrainButtons = 0;
      for (int i = 0; i < count; ++i)
        if (buttons[i].kind == Rendering::UIRenderer::CommandKind::Earthwork &&
            !Rendering::UIRenderer::CommandHelp(sim, buttons[i]).empty()) ++terrainButtons;
      if (terrainButtons != 10) return "a villager is missing earthwork line or area controls";
      int heightButtons = 0;
      for (int i = 0; i < count; ++i)
        if (buttons[i].kind == Rendering::UIRenderer::CommandKind::EarthworkHeight && Core::isMountainHeight(buttons[i].height)) ++heightButtons;
      if (heightButtons != 3) return "the three mountain heights are missing";
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
      for (const auto kind : {WorldJobKind::RaiseGround, WorldJobKind::DigDitch, WorldJobKind::RestoreGround,
          WorldJobKind::DigChannel, WorldJobKind::DigLake})
      for (const int height : {Core::hillHeight, Core::mountainHeight, Core::highMountainHeight})
      {
        const Core::Command packet = Core::PlantLayoutCommand{crew, x, y, x + 2, y + 2, Core::PlantShape::Area, true, 1, kind, height};
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

    const char* heights()
    {
      for (const int height : {Core::hillHeight, Core::mountainHeight, Core::highMountainHeight})
      {
        ECS::Simulation sim, restored;
        sim.Init(setup());
        auto& registry = sim.GetRegistry();
        auto& grid = sim.GetGrid();
        // A separate open site makes the height independent of pinned building corners.
        const int x = 20, y = 20;
        const auto before = Rendering::Ground::HeightPx(grid, x, y);
        Core::WorldJobCommand command{workers(sim), WorldJobKind::RaiseGround, x, y, false, 1, height};
        if (!ECS::world_jobs::order(registry, grid, command)) return "a selectable mountain height cannot be ordered";
        const int target = registry.get<c::WorldJob>(registry.view<c::WorldJob>().front()).targetElevation;
        const auto site = registry.view<c::WorldJob>().front();
        if (!ECS::earthworks::finish(registry, grid, site)) return "a selectable mountain height cannot finish";
        registry.destroy(site);
        if (grid.GetTile(x, y).elevation != target || Rendering::Ground::HeightPx(grid, x, y) - before < height * 8)
          return "a selectable mountain height is too low";
        std::vector<uint8_t> bytes;
        if (!Save::Write(sim, bytes) || !Save::Read(restored, bytes) || ECS::Digest::Of(sim) != ECS::Digest::Of(restored))
          return "a completed mountain changed after loading";
        if (const auto* artifacts = std::getenv("AOE_EARTHWORK_ARTIFACTS"))
        {
          const auto name = "height-" + std::to_string(height) + ".sav";
          if (!Save::WriteFile(sim, (std::filesystem::path(artifacts) / name).string())) return "cannot save a mountain capture";
        }
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
      constexpr int ticks = 40000;
      const auto* artifacts = std::getenv("AOE_EARTHWORK_ARTIFACTS");
      for (int tick = 0; tick < ticks; ++tick)
      {
        if (tick == 20) queue.PushCommand(Core::PlantLayoutCommand{crew, x, y, x + 2, y + 2,
          Core::PlantShape::Area, false, 1, WorldJobKind::RaiseGround, Core::hillHeight});
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
          const int expected = initial[row * 3 + col] + Core::hillHeight;
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

    const char* freedom()
    {
      ECS::Simulation sim;
      sim.Init(setup());
      auto& registry = sim.GetRegistry();
      auto& grid = sim.GetGrid();
      const auto crew = workers(sim);
      int x = 0, y = 0;
      if (!patch(sim, x, y)) return "no site for displacement";
      auto& person = registry.get<c::Transform>(crew.back());
      person.position = {Fixed32(x), Fixed32(y)};
      person.previous_position = person.position;
      const auto originalPerson = person.position;
      const int hp = registry.get<c::Health>(crew.back()).current_hp;
      std::vector<entt::entity> animals;
      for (const char* name : {"HAWKX", "SHEEPG"})
      {
        const auto animal = ECS::Units::Spawn(registry, Core::ConfigManager::Get().GetUnitIdByName(name), 0, originalPerson);
        if (animal == entt::null) return "test creature is missing from the game data";
        animals.push_back(animal);
      }
      for (const auto entity : registry.view<const c::Health, const c::Owner, c::Transform>())
        if (!registry.any_of<c::Worker, c::Building>(entity) && registry.get<c::Owner>(entity).player_id == 1)
        {
          registry.get<c::Transform>(entity).position = originalPerson;
          animals.push_back(entity);
          break;
        }
      if (animals.size() != 3) return "no horse for displacement";
      std::vector<std::pair<int, Core::PlayerId>> animalState;
      for (const auto animal : animals) animalState.emplace_back(registry.get<c::Health>(animal).current_hp, registry.get<c::Owner>(animal).player_id);
      const auto tree = registry.create();
      const c::Vector2D treeAt{Fixed32(x + 1), Fixed32(y)};
      registry.emplace<c::Transform>(tree, treeAt, treeAt);
      registry.emplace<c::Resource>(tree, Core::ResourceType::Wood, 77);
      registry.emplace<c::Identity>(tree, Core::worldcraft::plantRules().treeUnit);
      c::Sprite art;
      art.slp_id = Core::ConfigManager::Get().GetUnitGraphicById(Core::worldcraft::plantRules().treeUnit, "idle_graphic0").slp_id;
      registry.emplace<c::Sprite>(tree, art);
      grid.GetTileMut(x + 1, y).has_tree = true;
      grid.SetWalkable(x + 1, y, false);
      const int beforeLevel = grid.GetTile(x, y).elevation;
      Core::WorldJobCommand raise{crew, WorldJobKind::RaiseGround, x, y, false, 1, Core::mountainHeight};
      if (!ECS::world_jobs::order(registry, grid, raise)) return "a person or tree still rejects the mountain order";
      const auto site = registry.view<c::WorldJob>().front();
      if (registry.get<c::Sprite>(site).slp_id != Core::worldcraft::workMarkerSlp(WorldJobKind::RaiseGround))
        return "earthworks still use the tree stump";
      if (!ECS::earthworks::finish(registry, grid, site)) return "a mountain could not clear its footprint";
      registry.destroy(site);
      const auto& moved = registry.get<c::Transform>(crew.back()).position;
      for (std::size_t i = 0; i < animals.size(); ++i)
      {
        const auto animal = animals[i];
        const auto& at = registry.get<c::Transform>(animal).position;
        if ((at.x == originalPerson.x && at.y == originalPerson.y) || registry.get<c::Health>(animal).current_hp != animalState[i].first ||
            registry.get<c::Owner>(animal).player_id != animalState[i].second) return "a horse, bird or animal was lost during displacement";
      }
      const auto& movedTree = registry.get<c::Transform>(tree).position;
      if ((moved.x == originalPerson.x && moved.y == originalPerson.y) || registry.get<c::Health>(crew.back()).current_hp != hp ||
          registry.get<c::Owner>(crew.back()).player_id != 1) return "displacement lost or harmed a villager";
      if ((movedTree.x == treeAt.x && movedTree.y == treeAt.y) || registry.get<c::Resource>(tree).current_amount != 77 ||
          !grid.GetTile(Physics::TileOf(movedTree.x), Physics::TileOf(movedTree.y)).has_tree) return "moving a tree lost its wood or blockage";
      if (grid.GetTile(x, y).elevation != beforeLevel + Core::mountainHeight ||
          Rendering::Ground::HeightPx(grid, static_cast<float>(x), static_cast<float>(y)) < 100.0f)
        return "the completed mountain is still too low";
      const auto projected = Core::IsometricMath::WorldToScreen(Fixed32(x), Fixed32(y));
      const auto picked = Rendering::Ground::Pick(grid, projected.x,
        projected.y - Rendering::Ground::HeightPx(grid, static_cast<float>(x), static_cast<float>(y)));
      if (std::abs(picked.first - x) > 0.1f || std::abs(picked.second - y) > 0.1f) return "the mouse misses the tall mountain";
      if (const auto* artifacts = std::getenv("AOE_EARTHWORK_ARTIFACTS"))
      {
        Save::WriteFile(sim, (std::filesystem::path(artifacts) / "tall-mountain.sav").string());
        std::printf("  tall mountain centre=%d,%d\n", x, y);
      }
      return nullptr;
    }

    const char* noFreeSpace()
    {
      ECS::Simulation sim;
      sim.Init(setup());
      auto& registry = sim.GetRegistry();
      auto& grid = sim.GetGrid();
      const auto crew = workers(sim);
      int x = 0, y = 0;
      if (!patch(sim, x, y)) return "no site for the clearance test";
      registry.get<c::Transform>(crew.front()).position = {Fixed32(x), Fixed32(y)};
      if (!ECS::world_jobs::order(registry, grid, {crew, WorldJobKind::DigLake, x, y, false, 1})) return "occupied lake was rejected";
      const auto site = registry.view<c::WorldJob>().front();
      for (int row = 0; row < grid.GetHeight(); ++row)
        for (int col = 0; col < grid.GetWidth(); ++col) grid.SetWalkable(col, row, false);
      const auto before = ECS::Digest::Of(sim);
      if (ECS::earthworks::finish(registry, grid, site) || ECS::Digest::Of(sim) != before)
        return "failed clearance moved objects or changed ground partially";
      return nullptr;
    }

    const char* shoreline()
    {
      ECS::Simulation sim;
      sim.Init(setup());
      auto& registry = sim.GetRegistry();
      auto& grid = sim.GetGrid();
      const auto crew = workers(sim);
      int x = 0, y = 0;
      if (!patch(sim, x, y)) return "no water work site";
      grid.SetTerrain(x, y, Core::TerrainType::DeepWater);
      Core::WorldJobCommand command{crew, WorldJobKind::DigChannel, x, y, false, 1};
      if (!ECS::world_jobs::order(registry, grid, command)) return "water still rejects dry excavation";
      auto site = registry.view<c::WorldJob>().front();
      if (!ECS::earthworks::finish(registry, grid, site)) return "water could not be filled with sand";
      registry.destroy(site);
      if (grid.GetTile(x, y).terrain != Core::TerrainType::Sand || !grid.IsWalkable(x, y)) return "a shallow channel is not passable sand";
      grid.SetEarthwork(x, y, -2, Physics::Earthwork::Channel);
      command.kind = WorldJobKind::DigLake;
      if (!ECS::world_jobs::order(registry, grid, command)) return "the lake order was rejected";
      site = registry.view<c::WorldJob>().front();
      if (!ECS::earthworks::finish(registry, grid, site)) return "the lake did not finish";
      registry.destroy(site);
      if (grid.GetTile(x, y).earthwork != Physics::Earthwork::Lake || grid.IsWalkable(x, y) ||
          grid.GetTile(x, y).terrain != Core::TerrainType::MediumWater) return "the lake has no water or blocks the wrong layer";
      if (const auto* artifacts = std::getenv("AOE_EARTHWORK_ARTIFACTS"))
        Save::WriteFile(sim, (std::filesystem::path(artifacts) / "lake.sav").string());
      command.kind = WorldJobKind::RestoreGround;
      if (!ECS::world_jobs::order(registry, grid, command)) return "a lake cannot be restored";
      site = registry.view<c::WorldJob>().front();
      if (!ECS::earthworks::finish(registry, grid, site) || grid.GetTile(x, y).terrain != Core::TerrainType::DeepWater)
        return "restoration lost the original water";
      registry.destroy(site);
      return nullptr;
    }

    const char* buildingsStay()
    {
      ECS::Simulation sim;
      sim.Init(setup());
      auto& registry = sim.GetRegistry();
      auto& grid = sim.GetGrid();
      entt::entity town = entt::null;
      for (const auto entity : registry.view<const c::Building, const c::Owner>())
        if (registry.get<c::Owner>(entity).player_id == 1) { town = entity; break; }
      if (town == entt::null) return "no building for adjacent work";
      const auto at = registry.get<c::Transform>(town).position;
      const int id = registry.get<c::Identity>(town).unit_id;
      const auto footprint = ECS::Buildings::FootprintOf(id);
      const int x = Physics::TileOf(at.x) + footprint.x.high + 1;
      const int y = Physics::TileOf(at.y);
      const auto corner = grid.CornerHeight(x, y);
      const int level = grid.GetTile(x - 1, y).elevation;
      Core::WorldJobCommand command{workers(sim), WorldJobKind::RaiseGround, x, y, false, 1};
      if (!ECS::world_jobs::order(registry, grid, command)) return "a mountain cannot touch a building";
      const auto site = registry.view<c::WorldJob>().front();
      if (!ECS::earthworks::finish(registry, grid, site)) return "work beside the building did not finish";
      if (!registry.valid(town) || grid.GetTile(x - 1, y).elevation != level || grid.CornerHeight(x, y) != corner ||
          registry.get<c::Transform>(town).position.x != at.x || registry.get<c::Transform>(town).position.y != at.y)
        return "earthworks changed the building or its foundation";
      registry.destroy(site);
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
    if (failure == nullptr) failure = heights();
    if (failure == nullptr) failure = freedom();
    if (failure == nullptr) failure = noFreeSpace();
    if (failure == nullptr) failure = shoreline();
    if (failure == nullptr) failure = buildingsStay();
    if (failure == nullptr) failure = fog();
    if (failure == nullptr) failure = persistence();
    Core::CommandQueue::Get().PopAllCommands();
    std::printf("earthworks=%s\n", failure != nullptr ? failure : "ok");
    return failure == nullptr;
  }
}
