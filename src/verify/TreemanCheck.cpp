#include "verify/Harness.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <thread>

#include "core/CommandQueue.h"
#include "core/ConfigManager.h"
#include "core/FixedMath.h"
#include "core/Treeman.h"
#include "ecs/Buildings.h"
#include "ecs/Animals.h"
#include "ecs/Navigation.h"
#include "ecs/Combat.h"
#include "ecs/Digest.h"
#include "ecs/Mortality.h"
#include "ecs/Orders.h"
#include "ecs/Players.h"
#include "ecs/Population.h"
#include "ecs/Simulation.h"
#include "ecs/Units.h"
#include "ecs/WorldJobs.h"
#include "ecs/components/Animation.h"
#include "ecs/components/Combat.h"
#include "ecs/components/Grazer.h"
#include "ecs/components/Health.h"
#include "ecs/components/Identity.h"
#include "ecs/components/Mortal.h"
#include "ecs/components/Owner.h"
#include "ecs/components/Selectable.h"
#include "ecs/components/Resource.h"
#include "ecs/components/Sprite.h"
#include "ecs/components/Worker.h"
#include "ecs/components/WorldJob.h"
#include "hal/SlpFile.h"
#include "net/Lobby.h"
#include "net/Wire.h"
#include "pathfinding/Pathfinder.h"
#include "rendering/UIRenderer.h"
#include "save/Recording.h"
#include "save/Save.h"

namespace Verify
{
  namespace
  {
    namespace c = ECS::Components;
    namespace tree = Core::treeman;
    using Core::Fixed32;

    Core::GameSetup setup()
    {
      auto game = Core::DefaultSetup();
      game.reveal = Core::Reveal::AllVisible;
      game.resources = Core::StartingResources::DeathMatch;
      for (auto& player : game.players) player.computer = false;
      return game;
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

    // Ищет доступный участок рядом со стартом для юнита или обычного дома.
    struct Site
    {
      int x = 0;
      int y = 0;
    };

    bool findSite(const ECS::Simulation& sim, Core::PlayerId player, Site& site, bool isHouse = false)
    {
      const auto crew = workers(sim, player);
      if (crew.empty()) return false;
      const auto& at = sim.GetRegistry().get<c::Transform>(crew.front()).position;
      const int cx = Physics::TileOf(at.x), cy = Physics::TileOf(at.y);
      for (int radius = 2; radius < 20; ++radius)
        for (int dy = -radius; dy <= radius; ++dy)
          for (int dx = -radius; dx <= radius; ++dx)
          {
            if (std::max(std::abs(dx), std::abs(dy)) != radius) continue;
            const bool isOpen = isHouse
              ? ECS::Buildings::Check(sim.GetRegistry(), sim.GetGrid(), player, 70, cx + dx, cy + dy,
                  ECS::Buildings::Ask::Beginning) == ECS::Buildings::Site::Clear
              : ECS::world_jobs::canPlant(sim.GetRegistry(), sim.GetGrid(), player, cx + dx, cy + dy);
            if (isOpen) { site = {cx + dx, cy + dy}; return true; }
          }
      return false;
    }

    entt::entity livingTree(const entt::registry& registry, Core::PlayerId player)
    {
      for (const auto entity : registry.view<const c::Identity, const c::Owner, const c::Health>())
        if (registry.get<c::Identity>(entity).unit_id == tree::unitId &&
            registry.get<c::Owner>(entity).player_id == player && ECS::Combat::Alive(registry, entity))
          return entity;
      return entt::null;
    }

    entt::entity jobAt(const entt::registry& registry, Site at)
    {
      for (const auto entity : registry.view<const c::WorldJob>())
      {
        const auto& job = registry.get<c::WorldJob>(entity);
        if (job.tileX == at.x && job.tileY == at.y) return entity;
      }
      return entt::null;
    }

    Core::WorldJobCommand grow(const ECS::Simulation& sim, Core::PlayerId player, Site site)
    {
      return {{workers(sim, player).front()}, Core::WorldJobKind::GrowTreeman, site.x, site.y, false, player};
    }

    const char* art()
    {
      HAL::DrsArchive archive;
      if (!HAL::DrsOpen(tree::archivePath, archive)) return "native archive is unavailable";
      const auto& config = Core::ConfigManager::Get();
      for (int id = tree::idleGraphic; id <= tree::corpseGraphic; ++id)
      {
        const auto graphic = config.GetGraphic(id);
        const auto headers = HAL::SlpReadHeaders(archive, id);
        if (graphic.angle_count != 8 || headers.size() != static_cast<size_t>(graphic.frame_count * 5) ||
            config.GetFrameMs(id, 0) != 80) return "native directions or frame timing differ";
        for (uint32_t frame = 0; frame < headers.size(); ++frame)
        {
          HAL::SlpFrame decoded;
          if (!HAL::SlpDecodeFrame(archive, id, frame, decoded) ||
              std::none_of(decoded.alpha.begin(), decoded.alpha.end(), [](uint8_t value) { return value != 0; }))
            return "native animation has an undecodable or empty frame";
        }
      }
      return nullptr;
    }

    const char* wildPopulation()
    {
      const int wildId = Core::ConfigManager::Get().GetUnitIdByName("WALKTREE_WILD");
      if (wildId < 0) return "wild walking tree is not registered";
      for (const auto* map : Core::kMapScripts)
      {
        auto game = setup();
        game.map_script = map;
        ECS::Simulation sim;
        sim.Init(game);
        int count = 0;
        const auto& registry = sim.GetRegistry();
        for (const auto entity : registry.view<const c::Identity, const c::Owner>())
        {
          if (registry.get<c::Identity>(entity).unit_id != wildId) continue;
          ++count;
          const auto& at = registry.get<c::Transform>(entity).position;
          if (registry.get<c::Owner>(entity).player_id != Core::PLAYER_GAIA ||
              registry.any_of<c::Combat, c::Resource>(entity) || !ECS::Animals::Herdable(registry, entity) ||
              !ECS::Navigation::CanStand(registry, sim.GetGrid(), entity, at))
            return "wild walking tree is armed, edible or on impassable ground";
          for (const auto& start : sim.player_starts)
          {
            const auto dx = at.x - Fixed32(start.first), dy = at.y - Fixed32(start.second);
            const auto minimum = Fixed32(tree::settings().m_wildStartDistance);
            if (dx * dx + dy * dy < minimum * minimum) return "wild walking tree appeared beside a starting base";
          }
        }
        if (count != tree::settings().m_wildCount)
        {
          std::printf("  wild trees on %s: %d\n", map, count);
          return "new map does not contain the configured wild population";
        }
      }
      return nullptr;
    }

    entt::entity nearestWild(const ECS::Simulation& sim, entt::entity claimant, entt::entity excluded = entt::null)
    {
      const auto& registry = sim.GetRegistry();
      const auto& at = registry.get<c::Transform>(claimant).position;
      const auto layer = ECS::Navigation::LayerOf(registry, sim.GetGrid(), claimant);
      const int region = ECS::Navigation::RegionOf(registry, sim.GetGrid(), claimant);
      entt::entity nearest = entt::null;
      Fixed32 best;
      for (const auto entity : registry.view<const c::Identity, const c::Transform>())
      {
        if (entity == excluded || registry.get<c::Identity>(entity).unit_id != tree::wildUnitId) continue;
        const auto& point = registry.get<c::Transform>(entity).position;
        if (Pathfinding::RegionAt(sim.GetGrid(), Physics::TileOf(point.x), Physics::TileOf(point.y), layer) != region) continue;
        const auto dx = point.x - at.x, dy = point.y - at.y;
        const auto distance = dx * dx + dy * dy;
        if (nearest == entt::null || distance < best || (distance == best && entity < nearest))
        {
          nearest = entity;
          best = distance;
        }
      }
      return nearest;
    }

    const char* taming()
    {
      ECS::Simulation sim, restored;
      sim.Init(setup());
      auto& registry = sim.GetRegistry();
      auto& grid = sim.GetGrid();
      const auto wild = nearestWild(sim, workers(sim, 1).front());
      if (wild == entt::null) return "no reachable wild tree for taming";
      for (int tick = 0; tick < 16; ++tick) sim.Update(16);
      if (registry.get<c::Owner>(wild).player_id != Core::PLAYER_GAIA)
        return "revealing the map tamed a distant wild tree";
      std::vector<uint8_t> bytes;
      std::string error;
      if (!Save::Write(sim, bytes) || !Save::Read(restored, bytes, &error) ||
          ECS::Digest::Of(sim) != ECS::Digest::Of(restored)) return "wild tree save failed";
      const auto at = registry.get<c::Transform>(wild).position;
      if (const auto* directory = std::getenv("AOE_TREEMAN_ARTIFACTS"))
      {
        const std::filesystem::path root(directory);
        std::filesystem::create_directories(root);
        if (!Save::WriteFile(sim, (root / "wild-treeman.sav").string())) return "wild tree capture save failed";
        std::printf("  wild treeman capture centre=%d,%d\n", at.x.ToInt(), at.y.ToInt());
      }
      std::vector<std::pair<int, int>> offsets;
      for (int y = -2; y <= 2; ++y)
        for (int x = -2; x <= 2; ++x)
          if ((x != 0 || y != 0) && grid.IsWalkable(at.x.ToInt() + x, at.y.ToInt() + y))
            offsets.emplace_back(x, y);
      std::sort(offsets.begin(), offsets.end(), [](const auto& a, const auto& b)
      {
        const int da = a.first * a.first + a.second * a.second, db = b.first * b.first + b.second * b.second;
        return da != db ? da < db : a < b;
      });
      if (offsets.size() < 2) return "no room for taming claimants";
      const int villager = Core::ConfigManager::Get().GetUnitIdByName("VMBAS");
      const auto ours = ECS::Units::Spawn(registry, villager, 1,
        {at.x + Fixed32(offsets[0].first), at.y + Fixed32(offsets[0].second)});
      const auto theirs = ECS::Units::Spawn(registry, villager, 2,
        {at.x + Fixed32(offsets[1].first), at.y + Fixed32(offsets[1].second)});
      if (ours == entt::null || theirs == entt::null || ECS::Combat::Hostile(registry, ours, wild) ||
          ECS::Combat::Hostile(registry, wild, ours)) return "untamed walking tree is hostile";
      const int used = ECS::Population::Used(registry, 1);
      const auto* player = ECS::Players::Find(registry, 1);
      const int wood = ECS::Players::Stock(*player, Core::ResourceType::Wood);
      const int gold = ECS::Players::Stock(*player, Core::ResourceType::Gold);
      for (int tick = 0; tick < 16 && registry.get<c::Identity>(wild).unit_id == tree::wildUnitId; ++tick) sim.Update(16);
      if (registry.get<c::Identity>(wild).unit_id != tree::unitId || registry.get<c::Owner>(wild).player_id != 1 ||
          !registry.all_of<c::Combat>(wild) || registry.all_of<c::Grazer>(wild) || ECS::Animals::Herdable(registry, wild) ||
          ECS::Population::Used(registry, 1) != used + tree::settings().population ||
          ECS::Players::Stock(*player, Core::ResourceType::Wood) != wood ||
          ECS::Players::Stock(*player, Core::ResourceType::Gold) != gold)
        return "nearest claimant did not receive a free permanent fighter with its population";
      ECS::Orders::Execute(registry, grid, Core::StopCommand{wild, 1});
      registry.get<c::Combat>(wild).stance = c::Stance::NoAttack;
      ECS::Mortality::Kill(registry, grid, ours);
      for (int tick = 0; tick < 32; ++tick) sim.Update(16);
      if (registry.get<c::Owner>(wild).player_id != 1) return "a tamed walking tree was stolen by proximity";
      if (!Save::Write(sim, bytes) || !Save::Read(restored, bytes, &error) ||
          ECS::Digest::Of(sim) != ECS::Digest::Of(restored)) return "tamed tree save failed";
      for (int tick = 0; tick < 32; ++tick) { sim.Update(16); restored.Update(16); }
      if (ECS::Digest::Of(sim) != ECS::Digest::Of(restored)) return "taming diverged after loading";
      if (const auto* directory = std::getenv("AOE_TREEMAN_ARTIFACTS"))
        if (!Save::WriteFile(sim, (std::filesystem::path(directory) / "tamed-treeman.sav").string()))
          return "tamed tree capture save failed";
      return nullptr;
    }

    const char* growth()
    {
      ECS::Simulation sim;
      sim.Init(setup());
      auto& registry = sim.GetRegistry();
      auto& grid = sim.GetGrid();
      Site at;
      if (!findSite(sim, 1, at)) return "no site for growing a walking tree";
      const auto crew = workers(sim, 1);
      for (const auto selected : registry.view<c::Selectable>())
        registry.get<c::Selectable>(selected).is_selected = selected == crew.front();
      Rendering::UIRenderer::OpenBuildMenu(sim, 0);
      Rendering::UIRenderer::Command buttons[Rendering::UIRenderer::kCommandSlots];
      const int count = Rendering::UIRenderer::BuildCommands(sim, 1024, 768, buttons, Rendering::UIRenderer::kCommandSlots);
      bool hasButton = false;
      for (int i = 0; i < count; ++i)
        if (buttons[i].kind == Rendering::UIRenderer::CommandKind::PlantTree &&
            buttons[i].jobKind == Core::WorldJobKind::GrowTreeman &&
            Rendering::UIRenderer::CommandHelp(sim, buttons[i]).find(tree::settings().grow) != std::string::npos)
          hasButton = true;
      if (!hasButton) return "villager has no walking tree button and help";
      auto command = grow(sim, 1, at);
      command.player = 2;
      if (ECS::world_jobs::order(registry, grid, command)) return "enemy grew a tree using our villager";
      command.player = 1;
      auto& player = *ECS::Players::Find(registry, 1);
      const int wood = ECS::Players::Stock(player, Core::ResourceType::Wood);
      const int gold = ECS::Players::Stock(player, Core::ResourceType::Gold);
      ECS::Players::SetResource(player, Core::Rules::Res::Gold, Fixed32(0));
      if (ECS::world_jobs::order(registry, grid, command)) return "tree grew without its gold cost";
      ECS::Players::SetResource(player, Core::Rules::Res::Gold, Fixed32(gold));
      if (!ECS::world_jobs::order(registry, grid, command)) return "growth order failed";
      auto job = jobAt(registry, at);
      if (job == entt::null || ECS::Players::Stock(player, Core::ResourceType::Wood) != wood - tree::settings().woodCost ||
          ECS::Players::Stock(player, Core::ResourceType::Gold) != gold - tree::settings().goldCost)
        return "growth charged the wrong resources";
      if (ECS::world_jobs::canPlant(registry, grid, 1, at.x, at.y)) return "growth did not reserve its tile";
      ECS::world_jobs::cancel(registry, grid, job, 1);
      if (ECS::Players::Stock(player, Core::ResourceType::Wood) != wood ||
          ECS::Players::Stock(player, Core::ResourceType::Gold) != gold) return "cancel failed to refund both resources";
      if (!ECS::world_jobs::order(registry, grid, command)) return "growth could not restart";
      job = jobAt(registry, at);
      for (int tick = 0; tick < 7000; ++tick) sim.Update(16);
      if (!registry.valid(job) || !registry.all_of<c::WorldJob>(job) ||
          registry.get<c::WorldJob>(job).workedMs != Fixed32(tree::settings().workMs) ||
          livingTree(registry, 1) != entt::null) return "growth failed to wait at the population limit";
      std::vector<uint8_t> bytes;
      ECS::Simulation restored;
      std::string error;
      if (!Save::Write(sim, bytes) || !Save::Read(restored, bytes, &error) ||
          ECS::Digest::Of(sim) != ECS::Digest::Of(restored)) return "population-wait save did not restore";
      const int used = ECS::Population::Used(registry, 1);
      ECS::Players::SetResource(player, Core::Rules::Res::PopulationHeadroom, Fixed32(10));
      sim.Update(16);
      const auto unit = livingTree(registry, 1);
      if (unit == entt::null || registry.valid(job) || !grid.IsWalkable(at.x, at.y) ||
          ECS::Population::Used(registry, 1) != used + tree::settings().population ||
          !registry.all_of<c::Animation, c::Combat>(unit) || registry.any_of<c::Resource, c::WorldJob>(unit))
        return "finished tree did not become a mobile population-counted fighter";
      if (registry.get<c::Health>(unit).max_hp != Core::Rules::Unit(tree::unitId)->hp)
        return "walking tree has the wrong hit points";
      return nullptr;
    }

    const char* persistence()
    {
      ECS::Simulation sim, restored;
      sim.Init(setup());
      const auto crew = workers(sim, 1);
      Site at;
      if (crew.size() < 3 || !findSite(sim, 1, at)) return "no recorded growth site";
      const auto wild = nearestWild(sim, crew[2]);
      if (wild == entt::null) return "no reachable tree for recorded taming";
      const auto destination = sim.GetRegistry().get<c::Transform>(wild).position;
      Save::Recording recording;
      recording.setup = sim.Setup();
      auto& queue = Core::CommandQueue::Get();
      queue.PopAllCommands();
      constexpr int ticks = 10000;
      bool hasRestore = false;
      for (int tick = 0; tick < ticks; ++tick)
      {
        if (tick == 1) queue.PushCommand(grow(sim, 1, at));
        if (tick == 2)
        {
          Site house;
          if (!findSite(sim, 1, house, true)) return "no house site for recorded growth";
          queue.PushCommand(Core::BuildCommand{crew[1], 70, house.x, house.y, 1});
        }
        if (tick == 3) queue.PushCommand(Core::MoveCommand{crew[2], destination.x, destination.y, 1});
        Save::TakeTurn(recording, sim.Tick() + 1);
        sim.Update(16);
        Save::Witness(recording, sim);
        if (hasRestore)
        {
          restored.Update(16);
          if (tick % 100 == 0 && ECS::Digest::Of(restored) != ECS::Digest::Of(sim))
            return "walking tree diverged after save/load";
        }
        if (tick == 1000 || tick == ticks - 1)
        {
          std::vector<uint8_t> bytes;
          std::string error;
          if (!Save::Write(sim, bytes) || !Save::Read(restored, bytes, &error) ||
              ECS::Digest::Of(restored) != ECS::Digest::Of(sim)) return "growing or living tree could not be saved";
          hasRestore = true;
        }
      }
      if (livingTree(sim.GetRegistry(), 1) == entt::null || jobAt(sim.GetRegistry(), at) != entt::null)
        return "recorded villager never finished its tree";
      if (sim.GetRegistry().get<c::Identity>(wild).unit_id != tree::unitId ||
          sim.GetRegistry().get<c::Owner>(wild).player_id != 1) return "recorded move never tamed its wild tree";
      const auto path = std::filesystem::temp_directory_path() / "openaok-treeman-verify.mgl";
      Save::Recording read;
      const bool hasFile = Save::WriteRecording(recording, path.string()) && Save::ReadRecording(path.string(), read);
      std::filesystem::remove(path);
      if (!hasFile) return "walking tree recording file failed";
      ECS::Simulation replay;
      replay.Init(read.setup);
      size_t cursor = 0, witness = 0;
      for (int tick = 0; tick < ticks; ++tick)
      {
        Save::Replay(read, cursor, replay.Tick() + 1);
        replay.Update(16);
        if (!Save::StillTheGame(read, witness, replay)) return "walking tree replay diverged";
      }
      if (ECS::Digest::Of(sim) != ECS::Digest::Of(replay)) return "walking tree replay ended differently";
      if (const auto* directory = std::getenv("AOE_TREEMAN_ARTIFACTS"))
      {
        const std::filesystem::path root(directory);
        std::filesystem::create_directories(root);
        if (!Save::WriteFile(sim, (root / "treeman.sav").string()) ||
            !Save::WriteRecording(recording, (root / "treeman.mgl").string())) return "could not write walking tree artifacts";
        std::printf("  treeman capture centre=%d,%d\n", at.x, at.y);
      }
      return nullptr;
    }

    const char* combat()
    {
      ECS::Simulation sim;
      sim.Init(setup());
      auto& registry = sim.GetRegistry();
      auto& grid = sim.GetGrid();
      Site at;
      if (!findSite(sim, 1, at)) return "no combat site";
      const c::Vector2D start{Fixed32(at.x), Fixed32(at.y)};
      const auto unit = ECS::Units::Spawn(registry, tree::unitId, 1, start);
      if (unit == entt::null) return "walking tree cannot spawn";
      Site destination;
      if (!findSite(sim, 1, destination)) return "no movement destination";
      ECS::Orders::Execute(registry, grid, Core::MoveCommand{unit, Fixed32(destination.x), Fixed32(destination.y), 1});
      bool hasWalk = false;
      for (int tick = 0; tick < 500; ++tick)
      {
        sim.Update(16);
        hasWalk = hasWalk || registry.get<c::Sprite>(unit).slp_id == tree::walkGraphic;
      }
      const auto position = registry.get<c::Transform>(unit).position;
      if (!hasWalk || (position.x == start.x && position.y == start.y)) return "walking tree failed to move with its walk animation";
      ECS::Orders::Execute(registry, grid, Core::StopCommand{unit, 1});
      const auto enemy = ECS::Units::Spawn(registry, 74, 2, {position.x + Fixed32(1) / Fixed32(2), position.y});
      registry.get<c::Combat>(enemy).stance = c::Stance::NoAttack;
      registry.get<c::Health>(enemy).current_hp = registry.get<c::Health>(enemy).max_hp = 1000;
      ECS::Orders::Execute(registry, grid, Core::AttackCommand{unit, enemy, 1});
      int swingAt = -1, hitAt = -1;
      bool hasAttack = false;
      for (int tick = 0; tick < 400 && hitAt < 0; ++tick)
      {
        sim.Update(16);
        const auto& state = registry.get<c::Combat>(unit);
        if (swingAt < 0 && state.swing_ms > 0) swingAt = tick;
        hasAttack = hasAttack || registry.get<c::Sprite>(unit).slp_id == tree::attackGraphic;
        if (registry.get<c::Health>(enemy).current_hp < 1000) hitAt = tick;
      }
      const int delay = Core::Rules::Unit(tree::unitId)->frame_delay * 80;
      if (!hasAttack || swingAt < 0 || hitAt < 0 || std::abs((hitAt - swingAt) * 16 - delay) > 16)
        return "walking tree impact did not match its attack frame";
      const int expected = ECS::Combat::HeightAdjusted(
        ECS::Combat::Damage(ECS::Players::RulesFor(registry, unit, tree::unitId)->attacks,
          ECS::Players::RulesFor(registry, unit, tree::unitId)->attack_count, *ECS::Players::RulesFor(registry, enemy, 74)),
        ECS::Combat::LevelOf(registry, grid, unit), ECS::Combat::LevelOf(registry, grid, enemy));
      if (registry.get<c::Health>(enemy).current_hp != 1000 - expected) return "walking tree melee damage differs";
      const int used = ECS::Population::Used(registry, 1);
      ECS::Mortality::Kill(registry, grid, unit);
      sim.Update(16);
      if (registry.get<c::Sprite>(unit).slp_id != tree::dieGraphic ||
          ECS::Population::Used(registry, 1) != used - tree::settings().population)
        return "walking tree death failed to animate or release population";
      for (int tick = 0; tick < 90; ++tick) sim.Update(16);
      bool hasCorpse = false;
      for (const auto corpse : registry.view<const c::Corpse, const c::Sprite>())
        hasCorpse = hasCorpse || registry.get<c::Sprite>(corpse).slp_id == tree::corpseGraphic;
      if (registry.valid(unit) || !hasCorpse) return "walking tree did not leave its own remains";
      return nullptr;
    }

    // Закрывает оба TCP-соединения при любом результате проверки.
    struct Peers
    {
      Net::Session host, client;
      ~Peers() { Net::Leave(client); Net::Leave(host); }
    };

    const char* network()
    {
      Peers peers;
      if (!Net::Host(peers.host, 0, setup(), 2) ||
          !Net::Join(peers.client, "127.0.0.1", HAL::Net::BoundPort(peers.host.listener))) return "TCP connection failed";
      for (int i = 0; i < 500 && !(peers.host.started && peers.client.started); ++i)
      {
        Net::Pump(peers.host); Net::Pump(peers.client);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      }
      if (!(peers.host.started && peers.client.started)) return "TCP walking tree match did not start";
      ECS::Simulation host, client;
      host.Init(peers.host.setup); client.Init(peers.client.setup);
      std::array<entt::entity, 2> wilds{entt::null, entt::null};
      std::array<Site, 2> sites;
      for (Core::PlayerId player : {1, 2})
      {
        auto& at = sites[player - 1];
        if (!findSite(host, player, at)) return "no network growth site";
        auto& session = player == 1 ? peers.host : peers.client;
        Net::Issue(session, grow(host, player, at));
        const auto crew = workers(host, player);
        if (crew.size() < 3) return "no free network claimant";
        const auto wild = nearestWild(host, crew[2], wilds[0]);
        if (wild == entt::null) return "no reachable network wild tree";
        wilds[player - 1] = wild;
        const auto destination = host.GetRegistry().get<c::Transform>(wild).position;
        Net::Issue(session, Core::MoveCommand{crew[2], destination.x, destination.y, player});
        // The test fixture supplies population equally on both actual peers.
        for (auto* sim : {&host, &client})
          ECS::Players::SetResource(*ECS::Players::Find(sim->GetRegistry(), player),
            Core::Rules::Res::PopulationHeadroom, Fixed32(10));
      }
      constexpr uint64_t ticks = 10000;
      const auto advance = [](Net::Session& session, ECS::Simulation& sim)
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
        Net::Pump(peers.host); Net::Pump(peers.client);
        advance(peers.host, host); advance(peers.client, client);
        if (!peers.host.failure.empty() || !peers.client.failure.empty() || peers.host.desynced || peers.client.desynced)
          return "TCP walking tree peers diverged";
        std::this_thread::sleep_for(std::chrono::microseconds(100));
      }
      if (host.Tick() != ticks || client.Tick() != ticks || ECS::Digest::Of(host) != ECS::Digest::Of(client) ||
          livingTree(host.GetRegistry(), 1) == entt::null || livingTree(host.GetRegistry(), 2) == entt::null)
        return "TCP peers did not grow identical walking trees";
      for (Core::PlayerId player : {1, 2})
      {
        const auto wild = wilds[player - 1];
        if (host.GetRegistry().get<c::Identity>(wild).unit_id != tree::unitId ||
            host.GetRegistry().get<c::Owner>(wild).player_id != player ||
            jobAt(host.GetRegistry(), sites[player - 1]) != entt::null)
          return "TCP peers did not grow and tame both players' trees";
      }
      return nullptr;
    }
  }

  bool CheckTreeman()
  {
    const char* failure = Core::ConfigManager::Get().GetUnitIdByName("WALKTREE") < 0
      ? "walking tree is not registered" : nullptr;
    if (failure == nullptr) failure = art();
    if (failure == nullptr) failure = wildPopulation();
    if (failure == nullptr) failure = taming();
    if (failure == nullptr) failure = growth();
    if (failure == nullptr) failure = persistence();
    if (failure == nullptr) failure = combat();
    if (failure == nullptr) failure = network();
    Core::CommandQueue::Get().PopAllCommands();
    std::printf("treeman=%s\n", failure != nullptr ? failure : "ok");
    return failure == nullptr;
  }
}
