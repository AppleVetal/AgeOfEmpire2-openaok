#include "verify/Harness.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <thread>
#include "core/CommandQueue.h"
#include "core/Worldcraft.h"
#include "ecs/Buildings.h"
#include "ecs/Digest.h"
#include "ecs/Fog.h"
#include "ecs/components/Doppelganger.h"
#include "ecs/Orders.h"
#include "ecs/Players.h"
#include "ecs/Resources.h"
#include "ecs/Simulation.h"
#include "ecs/Vision.h"
#include "ecs/WorldJobs.h"
#include "ecs/Earthworks.h"
#include "ecs/components/Health.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Resource.h"
#include "ecs/components/Selectable.h"
#include "ecs/components/Transform.h"
#include "ecs/components/Worker.h"
#include "ecs/components/WorldJob.h"
#include "net/Lobby.h"
#include "net/Wire.h"
#include "rendering/UIRenderer.h"
#include "save/Recording.h"
#include "save/Save.h"

namespace Verify
{
  namespace
  {
    namespace c = ECS::Components;
    using Core::Fixed32;

    Core::GameSetup setup()
    {
      auto result = Core::DefaultSetup();
      result.reveal = Core::Reveal::AllVisible;
      result.resources = Core::StartingResources::DeathMatch;
      for (auto& player : result.players) player.computer = false;
      return result;
    }

    std::vector<entt::entity> workers(const ECS::Simulation& sim, Core::PlayerId player)
    {
      std::vector<entt::entity> result;
      const auto& registry = sim.GetRegistry();
      for (const auto entity : registry.view<const c::Worker, const c::Owner>())
        if (registry.get<c::Owner>(entity).player_id == player) result.push_back(entity);
      std::sort(result.begin(), result.end());
      return result;
    }

    bool patch(ECS::Simulation& sim, Core::PlayerId player, int width, int height, int& x, int& y,
      Core::WorldJobKind kind = Core::WorldJobKind::PlantTree)
    {
      const auto crew = workers(sim, player);
      if (crew.empty()) return false;
      const auto& at = sim.GetRegistry().get<c::Transform>(crew.front()).position;
      const int cx = Physics::TileOf(at.x), cy = Physics::TileOf(at.y);
      for (int radius = 2; radius <= 16; ++radius)
        for (int dy = -radius; dy <= radius; ++dy)
          for (int dx = -radius; dx <= radius; ++dx)
          {
            if (std::max(std::abs(dx), std::abs(dy)) != radius) continue;
            bool hasRoom = true;
            for (int row = 0; row < height && hasRoom; ++row)
              for (int col = 0; col < width && hasRoom; ++col)
                hasRoom = Core::isEarthwork(kind) ? ECS::earthworks::canWork(sim.GetRegistry(), sim.GetGrid(), player,
                    cx + dx + col, cy + dy + row, kind) : ECS::world_jobs::canPlant(sim.GetRegistry(), sim.GetGrid(), player,
                    cx + dx + col, cy + dy + row);
            if (hasRoom) { x = cx + dx; y = cy + dy; return true; }
          }
      return false;
    }

    entt::entity atSite(const entt::registry& registry, int x, int y)
    {
      for (const auto entity : registry.view<const c::WorldJob>())
      {
        const auto& job = registry.get<c::WorldJob>(entity);
        if (job.tileX == x && job.tileY == y) return entity;
      }
      return entt::null;
    }

    int treeCount(const entt::registry& registry)
    {
      int count = 0;
      for ([[maybe_unused]] const auto entity : registry.view<const c::Planted, const c::Resource>()) ++count;
      return count;
    }

    const char* basic()
    {
      ECS::Simulation sim;
      sim.Init(setup());
      auto& registry = sim.GetRegistry();
      auto& grid = sim.GetGrid();
      const auto crew = workers(sim, 1);
      int x = 0, y = 0;
      if (crew.size() < 2 || !patch(sim, 1, 3, 3, x, y)) return "no planting test site";
      for (const auto selected : registry.view<c::Selectable>())
        registry.get<c::Selectable>(selected).is_selected = selected == crew.front();
      Rendering::UIRenderer::OpenBuildMenu(sim, 0);
      Rendering::UIRenderer::Command buttons[Rendering::UIRenderer::kCommandSlots];
      const int buttonCount = Rendering::UIRenderer::BuildCommands(sim, 1024, 768, buttons, Rendering::UIRenderer::kCommandSlots);
      int plantingButtons = 0;
      for (int i = 0; i < buttonCount; ++i)
        if (buttons[i].kind == Rendering::UIRenderer::CommandKind::PlantTree &&
            !Rendering::UIRenderer::CommandHelp(sim, buttons[i]).empty()) ++plantingButtons;
      if (plantingButtons != 2) return "the villager is missing line or area planting buttons";
      auto& seat = *ECS::Players::Find(registry, 1);
      const int before = ECS::Players::Stock(seat, Core::ResourceType::Wood);
      const int cost = Core::worldcraft::plantRules().woodCost;
      const auto issue = [&](const Core::Command& command) { ECS::Orders::Execute(registry, grid, command); };
      Core::WorldJobCommand command{{crew.front()}, Core::WorldJobKind::PlantTree, x, y, false, 2};
      issue(command);
      if (atSite(registry, x, y) != entt::null) return "another player ordered our villager";
      command.player = 1;
      command.tileX = INT32_MAX;
      issue(command);
      command.tileX = x;
      ECS::Players::SetResource(seat, Core::Rules::Res::Wood, Fixed32(0));
      issue(command);
      if (atSite(registry, x, y) != entt::null) return "a job began without payment";
      ECS::Players::SetResource(seat, Core::Rules::Res::Wood, Fixed32(before));
      issue(command);
      const auto site = atSite(registry, x, y);
      if (site == entt::null || ECS::Players::Stock(seat, Core::ResourceType::Wood) != before - cost)
        return "plant order failed or charged the wrong price";
      if (ECS::world_jobs::canPlant(registry, grid, 1, x, y)) return "a job failed to reserve its tile";
      if (ECS::Buildings::CheckAboveGround(registry, 1, 70, x, y, ECS::Buildings::Ask::Placing) == ECS::Buildings::Site::Clear)
        return "a house can overlap a reserved planting";
      command.workers = {crew[1], crew[1]};
      issue(command);
      if (ECS::Players::Stock(seat, Core::ResourceType::Wood) != before - cost)
        return "joining a job paid a second time";
      issue(Core::CancelWorldJobCommand{site, 2});
      if (!registry.valid(site)) return "another player cancelled our job";
      command.workers = {crew.front()};
      command.append = true;
      command.tileX = x + 1;
      issue(command);
      if (registry.get<c::WorldJobQueue>(crew.front()).count != 2) return "append replaced the work queue";
      auto digest = ECS::Digest::Of(sim);
      registry.get<c::WorldJob>(site).workedMs = Fixed32(1);
      if (ECS::Digest::Of(sim) == digest) return "work progress escaped the digest";
      registry.get<c::WorldJob>(site).workedMs = Fixed32(0);
      auto& queue = registry.get<c::WorldJobQueue>(crew.front());
      std::swap(queue.sites[0], queue.sites[1]);
      if (ECS::Digest::Of(sim) == digest) return "queue order escaped the digest";
      std::swap(queue.sites[0], queue.sites[1]);
      issue(Core::CancelWorldJobCommand{site, 1});
      issue(Core::StopCommand{crew.front(), 1});
      issue(Core::StopCommand{crew[1], 1});
      sim.Update(16);
      if (!registry.view<c::WorldJob>().empty() || ECS::Players::Stock(seat, Core::ResourceType::Wood) != before)
        return "cancellation left a reservation or failed to refund unstarted work";

      command = {{crew.front(), crew[1]}, Core::WorldJobKind::PlantTree, x, y, false, 1};
      issue(command);
      const auto shared = atSite(registry, x, y);
      if (shared == entt::null) return "could not restart the cancelled planting";
      for (int tick = 0; tick < 1200 && registry.valid(shared) &&
          registry.all_of<c::WorldJob>(shared) && registry.get<c::WorldJob>(shared).workedMs < Fixed32(1000); ++tick)
        sim.Update(16);
      if (!registry.valid(shared) || registry.get<c::WorldJob>(shared).workedMs < Fixed32(1000))
      {
        if (registry.valid(shared))
        {
          const auto& stalled = registry.get<c::WorldJob>(shared);
          std::printf("  stalled site=%d,%d work=%d blocked=%d\n", x, y, stalled.workedMs.ToInt(), stalled.hasBlocked);
          for (const auto worker : crew)
          {
            const auto& at = registry.get<c::Transform>(worker).position;
            const auto& state = registry.get<c::Worker>(worker);
            std::printf("  worker=%u at=%d,%d working=%d state=%d\n", entt::to_integral(worker),
              (at.x * Fixed32(1000)).ToInt(), (at.y * Fixed32(1000)).ToInt(), state.working, static_cast<int>(state.state));
          }
        }
        return "villagers never worked beside the reserved tile";
      }
      if (!grid.IsBlocked(x, y)) return "started planting did not block its ground";
      issue(Core::DeleteCommand{crew.front(), 1});
      sim.Update(16);
      if (!registry.valid(shared)) return "one lost worker cancelled the remaining crew's job";
      issue(Core::StopCommand{crew[1], 1});
      sim.Update(16);
      const int refunded = ECS::Players::Stock(seat, Core::ResourceType::Wood);
      if (registry.valid(shared) || !grid.IsWalkable(x, y) || refunded >= before || refunded < before - cost)
        return "partial work cancellation did not refund only the unused price";

      // The public command bytes must round-trip, and malformed or truncated packets fail.
      for (const Core::Command& packet : {Core::Command(command), Core::Command(Core::CancelWorldJobCommand{shared, 1}),
          Core::Command(Core::PlantLayoutCommand{crew, x, y, x + 2, y + 2, Core::PlantShape::Area, true, 1})})
      {
        Net::Wire::Writer bytes, round;
        Net::Wire::Write(bytes, packet);
        Net::Wire::Reader read(bytes.bytes);
        Core::Command decoded;
        if (!Net::Wire::Read(read, decoded) || !read.Done()) return "new command did not decode";
        Net::Wire::Write(round, decoded);
        if (round.bytes != bytes.bytes) return "new command changed on the wire";
        bytes.bytes.pop_back();
        Net::Wire::Reader shortRead(bytes.bytes);
        if (Net::Wire::Read(shortRead, decoded)) return "truncated job packet was accepted";
      }
      if (!ECS::world_jobs::layout(grid, -1, y, x, y, Core::PlantShape::Line).empty() ||
          !ECS::world_jobs::layout(grid, 0, 0, 16, 16, Core::PlantShape::Area).empty())
        return "an out-of-bounds or oversized planting layout was accepted";
      const auto line = ECS::world_jobs::layout(grid, x, y, x + 4, y + 2, Core::PlantShape::Line);
      if (line.size() != 5 || line.front() != std::pair{x, y} || line.back() != std::pair{x + 4, y + 2})
        return "planting line lost its endpoints";
      const auto area = ECS::world_jobs::layout(grid, x, y, x + 2, y + 2, Core::PlantShape::Area);
      if (area.size() != 9 || area.front() != std::pair{x + 1, y + 1}) return "area did not start in its middle";
      return nullptr;
    }

    const char* persistence()
    {
      ECS::Simulation sim, restored;
      sim.Init(setup());
      int x = 0, y = 0;
      if (!patch(sim, 1, 3, 3, x, y)) return "no open patch for the recorded game";
      const auto crew = workers(sim, 1);
      Save::Recording recording;
      recording.setup = sim.Setup();
      bool hasRestore = false;
      auto& queue = Core::CommandQueue::Get();
      queue.PopAllCommands();
      constexpr int ticks = 14000;
      const char* artifactDirectory = std::getenv("AOE_WORLDCRAFT_ARTIFACTS");
      for (int tick = 0; tick < ticks; ++tick)
      {
        if (tick == 20) queue.PushCommand(Core::PlantLayoutCommand{crew, x, y, x + 2, y + 2,
          Core::PlantShape::Area, false, 1});
        Save::TakeTurn(recording, sim.Tick() + 1);
        sim.Update(16);
        Save::Witness(recording, sim);
        if (hasRestore)
        {
          restored.Update(16);
          if (tick % 100 == 0 && ECS::Digest::Of(restored) != ECS::Digest::Of(sim))
            return "planting diverged after loading a save";
        }
        if (tick == 1000)
        {
          if (sim.GetRegistry().view<c::WorldJob>().empty()) return "recorded game created no jobs";
          if (!Save::Uncovered(sim.GetRegistry()).empty()) return "a job component is not saved";
          std::vector<uint8_t> bytes;
          std::string error;
          if (!Save::Write(sim, bytes) || !Save::Read(restored, bytes, &error))
          {
            std::printf("  job save: %s\n", error.c_str());
            return "the planting queue could not be saved and restored";
          }
          if (ECS::Digest::Of(restored) != ECS::Digest::Of(sim)) return "the loaded work queue differs";
          hasRestore = true;
          if (artifactDirectory != nullptr)
            Save::WriteFile(sim, (std::filesystem::path(artifactDirectory) / "planting.sav").string());
        }
      }
      if (treeCount(sim.GetRegistry()) != 9 || !sim.GetRegistry().view<c::WorldJob>().empty())
      {
        std::printf("  planted trees=%d; sites=%zu\n", treeCount(sim.GetRegistry()), sim.GetRegistry().view<c::WorldJob>().size());
        return "the dense patch never finished growing";
      }
      if (artifactDirectory != nullptr)
      {
        Save::WriteFile(sim, (std::filesystem::path(artifactDirectory) / "forest.sav").string());
        std::printf("  forest capture centre=%d,%d\n", x + 1, y + 1);
      }
      const auto path = std::filesystem::temp_directory_path() / "openaok-worldcraft-verify.mgl";
      Save::Recording read;
      const bool hasFile = Save::WriteRecording(recording, path.string()) && Save::ReadRecording(path.string(), read);
      std::filesystem::remove(path);
      if (!hasFile) return "the planting recording did not survive its file";
      ECS::Simulation replay;
      replay.Init(read.setup);
      std::size_t cursor = 0, witness = 0;
      for (int tick = 0; tick < ticks; ++tick)
      {
        Save::Replay(read, cursor, replay.Tick() + 1);
        replay.Update(16);
        if (!Save::StillTheGame(read, witness, replay)) return "planting replay diverged from the original";
      }
      if (ECS::Digest::Of(replay) != ECS::Digest::Of(sim)) return "the final replay forest differs";
      // Fully grown trees use the normal gathering/depletion path and release their tiles.
      const auto trees = sim.GetRegistry().view<c::Planted, c::Resource>();
      entt::entity tree = entt::null;
      Fixed32 distance(INT32_MAX);
      const auto& workerAt = sim.GetRegistry().get<c::Transform>(crew.front()).position;
      for (const auto candidate : trees)
      {
        const auto& point = sim.GetRegistry().get<c::Transform>(candidate).position;
        const auto dx = point.x - workerAt.x, dy = point.y - workerAt.y;
        if (dx * dx + dy * dy < distance) { distance = dx * dx + dy * dy; tree = candidate; }
      }
      const auto at = sim.GetRegistry().get<c::Transform>(tree).position;
      const int wood = sim.GetRegistry().get<c::Resource>(tree).current_amount;
      ECS::Orders::Execute(sim.GetRegistry(), sim.GetGrid(), Core::GatherCommand{crew.front(), tree, 1});
      for (int tick = 0; tick < 4000 && sim.GetRegistry().valid(tree) &&
          sim.GetRegistry().get<c::Resource>(tree).current_amount == wood; ++tick) sim.Update(16);
      if (!sim.GetRegistry().valid(tree) || sim.GetRegistry().get<c::Resource>(tree).current_amount >= wood)
        return "a villager cannot fell and gather a planted tree";
      sim.GetRegistry().get<c::Resource>(tree).current_amount = 0;
      ECS::Resources::Exhaust(sim.GetRegistry(), sim.GetGrid(), tree);
      if (!sim.GetGrid().IsWalkable(Physics::TileOf(at.x), Physics::TileOf(at.y)) ||
          sim.GetGrid().GetTile(Physics::TileOf(at.x), Physics::TileOf(at.y)).has_tree)
        return "a depleted planted tree kept blocking its tile";
      return nullptr;
    }

    const char* fog()
    {
      auto options = setup();
      options.reveal = Core::Reveal::Explored;
      ECS::Simulation sim;
      sim.Init(options);
      auto& registry = sim.GetRegistry();
      int x = 0, y = 0;
      if (!patch(sim, 1, 1, 1, x, y)) return "no visible planting site for fog check";
      if (ECS::Vision::Visible(registry, 2, x, y)) return "fog check site is visible to the opponent";
      ECS::Orders::Execute(registry, sim.GetGrid(), Core::WorldJobCommand{workers(sim, 1),
        Core::WorldJobKind::PlantTree, x, y, false, 1});
      const auto site = atSite(registry, x, y);
      if (site == entt::null) return "fog check planting was rejected";
      ECS::world_jobs::observe(registry);
      if (ECS::Vision::Shows(registry, 2, site)) return "a new planting appeared in unexplored fog memory";
      ECS::Vision::ToggleSeeAll(registry, 2);
      ECS::world_jobs::observe(registry);
      if (!ECS::Vision::Shows(registry, 2, site)) return "the opponent cannot discover a visible planting";
      ECS::Vision::ToggleSeeAll(registry, 2);
      ECS::Fog::Going(registry, site);
      bool hasPicture = false;
      for (const auto entity : registry.view<c::Doppelganger>())
      {
        const auto& picture = registry.get<c::Doppelganger>(entity);
        hasPicture = hasPicture || (picture.source == site && picture.player == 2);
      }
      if (!hasPicture || ECS::Vision::Shows(registry, 2, site)) return "unseen growth did not keep the previous picture";
      return nullptr;
    }

    // Закрывает оба сетевых участника при любом исходе проверки.
    struct Pair
    {
      Net::Session host, client;
      ~Pair() { Net::Leave(client); Net::Leave(host); }
    };

    const char* network(bool hasEarthworks = false)
    {
      Pair pair;
      if (!Net::Host(pair.host, 0, setup(), 2) ||
          !Net::Join(pair.client, "127.0.0.1", HAL::Net::BoundPort(pair.host.listener)))
        return "could not connect the planting peers";
      for (int i = 0; i < 500 && !(pair.host.started && pair.client.started); ++i)
      {
        Net::Pump(pair.host); Net::Pump(pair.client);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      }
      if (!(pair.host.started && pair.client.started)) return "planting peers did not start";
      ECS::Simulation host, client;
      host.Init(pair.host.setup); client.Init(pair.client.setup);
      int x = 0, y = 0, cx = 0, cy = 0;
      const auto hostKind = hasEarthworks ? Core::WorldJobKind::RaiseGround : Core::WorldJobKind::PlantTree;
      const auto clientKind = hasEarthworks ? Core::WorldJobKind::DigDitch : Core::WorldJobKind::PlantTree;
      if (!patch(host, 1, 3, 1, x, y, hostKind) || !patch(host, 2, 2, 2, cx, cy, clientKind)) return "no network work sites";
      int peaks[3]{};
      for (int i = 0; i < 3; ++i) peaks[i] = host.GetGrid().GetTile(x + i, y).elevation + Core::mountainHeight;
      Net::Issue(pair.host, Core::PlantLayoutCommand{workers(host, 1), x, y, x + 2, y,
        Core::PlantShape::Line, false, 1, hostKind});
      Net::Issue(pair.client, Core::PlantLayoutCommand{workers(host, 2), cx, cy, cx + 1, cy + 1,
        Core::PlantShape::Area, false, 2, clientKind});
      const uint64_t ticks = hasEarthworks ? 30000 : 10000;
      const auto advance = [ticks](Net::Session& session, ECS::Simulation& sim)
      {
        std::vector<Core::Command> due;
        for (int i = 0; i < Net::kTicksPerTurn && sim.Tick() < ticks && Net::NextTick(session, due); ++i)
        {
          for (const auto& command : due) ECS::Orders::Execute(sim.GetRegistry(), sim.GetGrid(), command);
          sim.Update(16);
          Net::TickDone(session, sim);
        }
      };
      for (int frame = 0; frame < 30000 && (host.Tick() < ticks || client.Tick() < ticks); ++frame)
      {
        Net::Pump(pair.host); Net::Pump(pair.client);
        advance(pair.host, host); advance(pair.client, client);
        if (!pair.host.failure.empty() || !pair.client.failure.empty() || pair.host.desynced || pair.client.desynced)
          return "planting peers failed or diverged";
        std::this_thread::sleep_for(std::chrono::microseconds(100));
      }
      if (host.Tick() != ticks || client.Tick() != ticks) return "planting lockstep stalled";
      const int completed = hasEarthworks ? static_cast<int>(std::count_if(host.GetGrid().Tiles().begin(), host.GetGrid().Tiles().end(),
        [](const Physics::Tile& tile) { return tile.earthwork != Physics::Earthwork::None; })) : treeCount(host.GetRegistry());
      bool finished = hasEarthworks ? completed >= 7 : completed == 7;
      if (hasEarthworks) {
        for (int i = 0; i < 3; ++i) finished = finished && host.GetGrid().GetTile(x + i, y).elevation >= peaks[i];
        for (int row = 0; row < 2; ++row)
          for (int col = 0; col < 2; ++col) finished = finished && host.GetGrid().GetTile(cx + col, cy + row).earthwork == Physics::Earthwork::Ditch;
        finished = finished && host.GetRegistry().view<c::WorldJob>().empty();
      }
      if (!finished || ECS::Digest::Of(host) != ECS::Digest::Of(client))
      {
        std::printf("  network planted=%d,%d sites=%zu,%zu\n", treeCount(host.GetRegistry()), treeCount(client.GetRegistry()),
          host.GetRegistry().view<c::WorldJob>().size(), client.GetRegistry().view<c::WorldJob>().size());
        return "network line and area did not finish identical work";
      }
      return nullptr;
    }
  }

  bool CheckWorldJobs()
  {
    const char* failure = basic();
    if (failure == nullptr) failure = fog();
    if (failure == nullptr) failure = persistence();
    Core::CommandQueue::Get().PopAllCommands();
    std::printf("worldjobs=%s\n", failure != nullptr ? failure : "ok");
    return failure == nullptr;
  }

  bool CheckForestNetwork()
  {
    const char* failure = network();
    Core::CommandQueue::Get().PopAllCommands();
    std::printf("forestnet=%s\n", failure != nullptr ? failure : "ok");
    return failure == nullptr;
  }

  bool CheckEarthworksNetwork()
  {
    const char* failure = network(true);
    Core::CommandQueue::Get().PopAllCommands();
    std::printf("earthnet=%s\n", failure != nullptr ? failure : "ok");
    return failure == nullptr;
  }
}
