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
#include "core/TrojanHorse.h"
#include "ecs/Garrisons.h"
#include "ecs/Missiles.h"
#include "ecs/TransportCombat.h"
#include "ecs/components/Projectile.h"
#include "ecs/components/Garrison.h"
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
#include "ecs/components/Path.h"
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
    namespace tree = Core::trojan_horse;
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

    // Описывает свободную клетку строительства в проверочном мире.
    struct Site
    {
      int m_x = 0;
      int m_y = 0;
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

    entt::entity livingHorse(const entt::registry& registry, Core::PlayerId player)
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
        if (job.tileX == at.m_x && job.tileY == at.m_y) return entity;
      }
      return entt::null;
    }

    Core::WorldJobCommand build(const ECS::Simulation& sim, Core::PlayerId player, Site site)
    {
      return {{workers(sim, player).front()}, Core::WorldJobKind::BuildTransport, site.m_x, site.m_y, false, player};
    }

    const char* construction()
    {
      ECS::Simulation sim;
      sim.Init(setup());
      auto& registry = sim.GetRegistry();
      auto& grid = sim.GetGrid();
      Site at;
      if (!findSite(sim, 1, at)) return "no site for growing a Trojan horse";
      const auto crew = workers(sim, 1);
      for (const auto selected : registry.view<c::Selectable>())
        registry.get<c::Selectable>(selected).is_selected = selected == crew.front();
      Rendering::UIRenderer::OpenBuildMenu(sim, 0);
      Rendering::UIRenderer::Command buttons[Rendering::UIRenderer::kCommandSlots];
      const int count = Rendering::UIRenderer::BuildCommands(sim, 1024, 768, buttons, Rendering::UIRenderer::kCommandSlots);
      bool hasButton = false;
      for (int i = 0; i < count; ++i)
        if (buttons[i].kind == Rendering::UIRenderer::CommandKind::PlantTree &&
            buttons[i].jobKind == Core::WorldJobKind::BuildTransport &&
            Rendering::UIRenderer::CommandHelp(sim, buttons[i]).find(tree::settings().m_build) != std::string::npos)
          hasButton = true;
      if (!hasButton) return "villager has no Trojan horse button and help";
      auto command = build(sim, 1, at);
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
      if (job == entt::null || ECS::Players::Stock(player, Core::ResourceType::Wood) != wood - tree::settings().m_woodCost ||
          ECS::Players::Stock(player, Core::ResourceType::Gold) != gold - tree::settings().m_goldCost)
        return "growth charged the wrong resources";
      if (ECS::world_jobs::canPlant(registry, grid, 1, at.m_x, at.m_y)) return "growth did not reserve its tile";
      ECS::world_jobs::cancel(registry, grid, job, 1);
      if (ECS::Players::Stock(player, Core::ResourceType::Wood) != wood ||
          ECS::Players::Stock(player, Core::ResourceType::Gold) != gold) return "cancel failed to refund both resources";
      if (!ECS::world_jobs::order(registry, grid, command)) return "growth could not restart";
      job = jobAt(registry, at);
      for (int tick = 0; tick < 7000; ++tick) sim.Update(16);
      if (!registry.valid(job) || !registry.all_of<c::WorldJob>(job) ||
          registry.get<c::WorldJob>(job).workedMs != Fixed32(tree::settings().m_workMs) ||
          livingHorse(registry, 1) != entt::null) return "growth failed to wait at the population limit";
      std::vector<uint8_t> bytes;
      ECS::Simulation restored;
      std::string error;
      if (!Save::Write(sim, bytes) || !Save::Read(restored, bytes, &error) ||
          ECS::Digest::Of(sim) != ECS::Digest::Of(restored)) return "population-wait save did not restore";
      const int used = ECS::Population::Used(registry, 1);
      ECS::Players::SetResource(player, Core::Rules::Res::PopulationHeadroom, Fixed32(10));
      sim.Update(16);
      const auto unit = livingHorse(registry, 1);
      if (unit == entt::null || registry.valid(job) || !grid.IsWalkable(at.m_x, at.m_y) ||
          ECS::Population::Used(registry, 1) != used + tree::settings().m_population ||
          (!registry.all_of<c::Animation>(unit) || registry.any_of<c::Combat>(unit)) || registry.any_of<c::Resource, c::WorldJob>(unit))
        return "finished tree did not become a mobile population-counted transport";
      if (registry.get<c::Health>(unit).max_hp != Core::Rules::Unit(tree::unitId)->hp)
        return "Trojan horse has the wrong hit points";
      return nullptr;
    }


    bool snapshot(ECS::Simulation& sim, ECS::Simulation& restored)
    {
      std::vector<uint8_t> bytes;
      std::string error;
      return Save::Write(sim, bytes) && Save::Read(restored, bytes, &error) &&
        ECS::Digest::Of(sim) == ECS::Digest::Of(restored);
    }

    const char* passengers()
    {
      ECS::Simulation sim, restored;
      sim.Init(setup());
      auto& registry = sim.GetRegistry();
      auto& grid = sim.GetGrid();
      Site site;
      if (!findSite(sim, 1, site)) return "no transport site";
      const c::Vector2D at{Fixed32(site.m_x), Fixed32(site.m_y)};
      const auto horse = ECS::Units::Spawn(registry, tree::unitId, 1, at);
      if (!ECS::Garrisons::IsTransport(registry, horse)) return "horse is not a transport";
      const auto* rules = Core::Rules::Unit(tree::unitId);
      const auto* foot = Core::Rules::Unit(83);
      if (rules->hp != 400 || rules->garrison != 12 || rules->speed != Fixed32(8) / Fixed32(5) ||
          rules->speed <= foot->speed || rules->attack_count != 0 || registry.any_of<c::Combat>(horse))
        return "transport parameters differ";
      HAL::DrsArchive archive;
      if (!HAL::DrsOpen(tree::archivePath, archive)) return "transport archive missing";
      for (int graphic = tree::idleGraphic; graphic <= tree::corpseGraphic; ++graphic)
      {
        const auto art = Core::ConfigManager::Get().GetGraphic(graphic);
        if (art.angle_count != 8 || HAL::SlpReadHeaders(archive, graphic).size() != static_cast<size_t>(art.frame_count * 5))
          return "transport art directions differ";
      }
      for (int id : {38, 35, Core::treeman::unitId, tree::unitId})
      {
        const auto excluded = ECS::Units::Spawn(registry, id, 1, at);
        if (ECS::Garrisons::Accepts(registry, horse, excluded)) return "excluded unit entered transport";
        ECS::Mortality::Kill(registry, grid, excluded);
      }
      const auto enemy = ECS::Units::Spawn(registry, 74, 2, at);
      if (ECS::Garrisons::Accepts(registry, horse, enemy)) return "enemy entered transport";
      ECS::Mortality::Kill(registry, grid, enemy);
      std::vector<entt::entity> party;
      for (int i = 0; i < 13; ++i)
      {
        const auto unit = ECS::Units::Spawn(registry, i % 2 ? 74 : 83, 1, at);
        party.push_back(unit);
        ECS::Orders::Execute(registry, grid, Core::GarrisonCommand{unit, horse, 1});
      }
      const int population = ECS::Population::Used(registry, 1);
      sim.Update(16);
      if (ECS::Garrisons::Count(registry, horse) != 12 || registry.all_of<c::Garrisoned>(party.back()) ||
          ECS::Population::Used(registry, 1) != population) return "capacity or population changed during boarding";
      for (int i = 0; i < 12; ++i)
        if (!registry.all_of<c::Garrisoned>(party[i]) || registry.all_of<c::Transform>(party[i]))
          return "passenger stayed outside";
      if (!snapshot(sim, restored)) return "loaded transport save failed";
      const auto destination = ECS::Navigation::Toward(registry, grid, horse, {at.x + Fixed32(6), at.y});
      ECS::Orders::Execute(registry, grid, Core::MoveCommand{horse, destination.x, destination.y, 1});
      for (int tick = 0; tick < 600; ++tick) sim.Update(16);
      const auto moved = registry.get<c::Transform>(horse).position;
      if (Core::FixedMath::Distance(moved.x - at.x, moved.y - at.y) < Fixed32(2) ||
          ECS::Garrisons::Count(registry, horse) != 12) return "loaded transport failed to move on land";
      ECS::Orders::Execute(registry, grid, Core::UnloadCommand{horse, moved.x, moved.y, 2});
      if (registry.all_of<c::Landing>(horse)) return "enemy commanded unload";
      ECS::Orders::Execute(registry, grid, Core::UnloadCommand{horse, moved.x, moved.y, 1});
      if (!snapshot(sim, restored)) return "pending unload save failed";
      for (int tick = 0; tick < 100; ++tick)
      {
        sim.Update(16); restored.Update(16);
      }
      if (ECS::Digest::Of(sim) != ECS::Digest::Of(restored) || ECS::Garrisons::Count(registry, horse) != 0)
        return "land unload did not finish identically after save";
      for (int i = 0; i < 12; ++i)
        if (!registry.all_of<c::Transform>(party[i]) || registry.all_of<c::Garrisoned>(party[i]) ||
            !ECS::Navigation::CanStand(registry, grid, party[i], registry.get<c::Transform>(party[i]).position))
          return "passenger unloaded on blocked ground";
      for (int i = 0; i < 12; ++i) ECS::Garrisons::PutInside(registry, party[i], horse);
      const int x = Physics::TileOf(moved.x), y = Physics::TileOf(moved.y);
      std::vector<std::pair<int,int>> blocked;
      for (int dy = -4; dy <= 4; ++dy)
        for (int dx = -4; dx <= 4; ++dx)
          if ((dx != 0 || dy != 0) && grid.InBounds(x + dx, y + dy) && grid.IsWalkable(x + dx, y + dy))
          {
            blocked.emplace_back(x + dx, y + dy);
            grid.SetWalkable(x + dx, y + dy, false);
          }
      ECS::Orders::Execute(registry, grid, Core::UnloadCommand{horse, moved.x, moved.y, 1});
      for (int tick = 0; tick < 12; ++tick) sim.Update(16);
      if (ECS::Garrisons::Count(registry, horse) != 12) return "unload escaped blocked exits";
      for (const auto& [bx, by] : blocked) grid.SetWalkable(bx, by, true);
      const auto before = registry.get<c::Transform>(horse).position;
      grid.SetEarthwork(x + 3, y, -2, Physics::Earthwork::River, 1);
      ECS::Orders::Execute(registry, grid, Core::UnloadCommand{horse, Fixed32(x + 3), Fixed32(y), 1});
      for (int tick = 0; tick < 15; ++tick) sim.Update(16);
      if (ECS::Garrisons::Count(registry, horse) != 12 || registry.get<c::Transform>(horse).position.x != before.x)
        return "transport unloaded into a river";
      const int used = ECS::Population::Used(registry, 1);
      ECS::Mortality::Kill(registry, grid, horse);
      for (int i = 0; i < 12; ++i)
        if (registry.valid(party[i]) && !registry.all_of<c::Dying>(party[i])) return "passenger survived transport destruction";
      if (ECS::Population::Used(registry, 1) != used - 14) return "destroyed cargo population was not released";
      return nullptr;
    }

    const char* firing()
    {
      ECS::Simulation sim, restored;
      sim.Init(setup());
      Site site;
      if (!findSite(sim, 1, site)) return "no firing site";
      auto& registry = sim.GetRegistry();
      auto& grid = sim.GetGrid();
      for (const auto unit : registry.view<c::Combat>()) registry.get<c::Combat>(unit).stance = c::Stance::NoAttack;
      const c::Vector2D at{Fixed32(site.m_x), Fixed32(site.m_y)};
      const auto horse = ECS::Units::Spawn(registry, tree::unitId, 1, at);
      const auto archer = ECS::Units::Spawn(registry, 4, 1, at);
      const auto spearman = ECS::Units::Spawn(registry, 93, 1, at);
      const auto villager = ECS::Units::Spawn(registry, 83, 1, at);
      const auto infantry = ECS::Units::Spawn(registry, 74, 1, at);
      const auto enemy = ECS::Units::Spawn(registry, 38, 2, {at.x + Fixed32(3), at.y});
      for (const auto unit : {archer, spearman, villager, infantry, enemy})
        registry.get<c::Combat>(unit).stance = c::Stance::NoAttack;
      registry.get<c::Health>(enemy) = {1000, 1000};
      ECS::Garrisons::PutInside(registry, villager, horse);
      ECS::Garrisons::PutInside(registry, infantry, horse);
      if (ECS::Missiles::VolleyOf(registry, horse).Total() != 0) return "unarmed passengers enabled firing";
      ECS::Garrisons::PutInside(registry, archer, horse);
      ECS::Garrisons::PutInside(registry, spearman, horse);
      if (ECS::Missiles::VolleyOf(registry, horse).Total() != 2 ||
          !registry.all_of<c::Combat>(horse)) return "ranged passengers did not arm transport";
      for (const auto unit : registry.view<c::Selectable>())
        registry.get<c::Selectable>(unit).is_selected = unit == horse;
      Rendering::UIRenderer::Command buttons[Rendering::UIRenderer::kCommandSlots];
      const int count = Rendering::UIRenderer::BuildCommands(sim, 1024, 768, buttons, Rendering::UIRenderer::kCommandSlots);
      bool hasUnload = false;
      for (int i = 0; i < count; ++i)
        hasUnload = hasUnload || buttons[i].kind == Rendering::UIRenderer::CommandKind::Unload;
      if (!hasUnload) return "armed transport lost its unload button";
      ECS::Missiles::Fire(registry, grid, horse, enemy);
      int arrows = 0, spears = 0;
      for (const auto missile : registry.view<const c::Projectile>())
      {
        const auto& shot = registry.get<c::Projectile>(missile);
        if (shot.shooter != horse) continue;
        if (shot.unit_id == tree::settings().m_spearProjectile)
        {
          ++spears;
          if (shot.attack_count != 1 || shot.attacks[0].cls != 3 || shot.attacks[0].amount != 3)
            return "thrown spear retained cavalry bonus or wrong damage";
        }
        else ++arrows;
      }
      if (arrows != 1 || spears != 1) return "mixed volley did not contain one arrow and one spear";
      registry.get<c::Combat>(horse).reload_ms = 3000;
      if (!snapshot(sim, restored) || ECS::transport_combat::fightingRules(restored.GetRegistry(), horse) == nullptr)
        return "armed in-flight save lost its battery";
      for (int tick = 0; tick < 10; ++tick) { sim.Update(16); restored.Update(16); }
      if (const auto* directory = std::getenv("AOE_TRANSPORT_ARTIFACTS"))
      {
        const std::filesystem::path output(directory);
        std::filesystem::create_directories(output);
        if (!Save::WriteFile(sim, (output / "transport-fire.sav").string())) return "firing capture save failed";
      }
      for (auto* world : {&sim, &restored})
        ECS::Orders::Execute(world->GetRegistry(), world->GetGrid(), Core::UnloadCommand{horse, at.x, at.y, 1});
      for (int tick = 0; tick < 180; ++tick) { sim.Update(16); restored.Update(16); }
      const int remaining = registry.get<c::Health>(enemy).current_hp;
      if (remaining >= 1000 || remaining < 990 || ECS::Digest::Of(sim) != ECS::Digest::Of(restored) ||
          ECS::Garrisons::Count(registry, horse) != 0 || ECS::Missiles::VolleyOf(registry, horse).Total() != 0)
        return "shots changed damage after unload, gained cavalry bonus, or diverged after save";
      if (!Save::Uncovered(registry).empty()) return "transport introduced an unsaved component";
      ECS::Garrisons::PutInside(registry, spearman, horse);
      const auto near = registry.get<c::Transform>(enemy).position;
      registry.get<c::Transform>(enemy).position = {at.x + Fixed32(6), at.y};
      ECS::Missiles::Fire(registry, grid, horse, enemy);
      for (const auto missile : registry.view<const c::Projectile>())
        if (registry.get<c::Projectile>(missile).shooter == horse) return "spear exceeded four tile range";
      registry.get<c::Transform>(enemy).position = near;
      ECS::Garrisons::PutInside(registry, archer, horse);
      const int beforeAutomatic = registry.get<c::Health>(enemy).current_hp;
      for (int tick = 0; tick < 420; ++tick) sim.Update(16);
      if (registry.get<c::Health>(enemy).current_hp >= beforeAutomatic) return "loaded transport did not auto-fire";
      std::vector<entt::entity> missiles;
      for (const auto missile : registry.view<const c::Projectile>()) missiles.push_back(missile);
      for (const auto missile : missiles) registry.destroy(missile);
      auto destination = ECS::Navigation::Toward(registry, grid, horse, {at.x - Fixed32(8), at.y});
      ECS::Orders::Execute(registry, grid, Core::MoveCommand{horse, destination.x, destination.y, 1});
      if (!ECS::transport_combat::isMoving(registry, horse)) return "no movement route for firing check";
      ECS::Missiles::Fire(registry, grid, horse, enemy);
      for (const auto missile : registry.view<const c::Projectile>())
        if (registry.get<c::Projectile>(missile).shooter == horse) return "transport fired while moving";
      for (int tick = 0; tick < 20; ++tick)
      {
        sim.Update(16);
        if (!ECS::transport_combat::isMoving(registry, horse)) break;
        for (const auto missile : registry.view<const c::Projectile>())
          if (registry.get<c::Projectile>(missile).shooter == horse) return "moving transport auto-fired";
      }
      ECS::Orders::Execute(registry, grid, Core::AttackCommand{horse, enemy, 1});
      const int beforeOrder = registry.get<c::Health>(enemy).current_hp;
      for (int tick = 0; tick < 500; ++tick) sim.Update(16);
      if (registry.get<c::Health>(enemy).current_hp >= beforeOrder) return "transport ignored explicit attack";
      return nullptr;
    }

    entt::entity findUnit(const entt::registry& registry, int id, Core::PlayerId player)
    {
      for (const auto entity : registry.view<const c::Identity, const c::Owner>())
        if (registry.get<c::Identity>(entity).unit_id == id && registry.get<c::Owner>(entity).player_id == player &&
            ECS::Combat::Alive(registry, entity)) return entity;
      return entt::null;
    }

    bool findBarracks(const ECS::Simulation& sim, Site& site)
    {
      const auto crew = workers(sim, 1);
      const auto at = sim.GetRegistry().get<c::Transform>(crew.front()).position;
      for (int radius = 2; radius < 20; ++radius)
        for (int dy = -radius; dy <= radius; ++dy)
          for (int dx = -radius; dx <= radius; ++dx)
            if (ECS::Buildings::Check(sim.GetRegistry(), sim.GetGrid(), 1, 12,
                Physics::TileOf(at.x) + dx, Physics::TileOf(at.y) + dy, ECS::Buildings::Ask::Beginning) == ECS::Buildings::Site::Clear)
            {
              site = {Physics::TileOf(at.x) + dx, Physics::TileOf(at.y) + dy};
              return true;
            }
      return false;
    }

    const char* persistence()
    {
      ECS::Simulation sim, restored;
      auto game = setup();
      game.starting_age = 2;
      sim.Init(game);
      Site site, house, barracks;
      if (!findSite(sim, 1, site)) return "no recorded construction site";
      const auto crew = workers(sim, 1);
      Save::Recording recording;
      recording.setup = sim.Setup();
      auto& queue = Core::CommandQueue::Get();
      queue.PopAllCommands();
      constexpr int ticks = 16000;
      bool hasBoarded = false, hasUnloaded = false, hasFired = false;
      for (int tick = 0; tick < ticks; ++tick)
      {
        if (tick == 1) queue.PushCommand(build(sim, 1, site));
        if (tick == 2)
        {
          if (!findSite(sim, 1, house, true)) return "no recorded house site";
          queue.PushCommand(Core::BuildCommand{crew[1], 70, house.m_x, house.m_y, 1});
        }
        if (tick == 3)
        {
          if (!findBarracks(sim, barracks)) return "no recorded barracks site";
          queue.PushCommand(Core::BuildCommand{crew[2], 12, barracks.m_x, barracks.m_y, 1});
        }
        if (tick == 5000)
        {
          const auto building = findUnit(sim.GetRegistry(), 12, 1);
          if (building == entt::null) return "recorded barracks was not built";
          queue.PushCommand(Core::TrainCommand{building, 93, 1});
        }
        const auto horse = livingHorse(sim.GetRegistry(), 1);
        if (tick == 7500 && horse != entt::null)
        {
          const auto spearman = findUnit(sim.GetRegistry(), 93, 1);
          if (spearman == entt::null) return "recorded spearman was not trained";
          queue.PushCommand(Core::GarrisonCommand{crew[2], horse, 1});
          queue.PushCommand(Core::GarrisonCommand{spearman, horse, 1});
        }
        if (tick == 8500 && horse != entt::null)
        {
          hasBoarded = ECS::Garrisons::Count(sim.GetRegistry(), horse) == 2;
          const auto enemy = findUnit(sim.GetRegistry(), 448, 2);
          if (enemy == entt::null) return "no recorded enemy scout";
          queue.PushCommand(Core::AttackCommand{horse, enemy, 1});
        }
        if (tick == 13500 && horse != entt::null)
        {
          const auto at = sim.GetRegistry().get<c::Transform>(horse).position;
          const auto to = ECS::Navigation::Toward(sim.GetRegistry(), sim.GetGrid(), horse, {at.x + Fixed32(4), at.y});
          queue.PushCommand(Core::UnloadCommand{horse, to.x, to.y, 1});
        }
        Save::TakeTurn(recording, sim.Tick() + 1);
        sim.Update(16);
        Save::Witness(recording, sim);
        if (tick == 1000 || tick == 8400 || tick == 13501)
          if (!snapshot(sim, restored)) return "construction, armed cargo or landing save failed";
        if (!hasFired)
          for (const auto projectile : sim.GetRegistry().view<const c::Projectile>())
            if (sim.GetRegistry().get<c::Projectile>(projectile).shooter == horse)
            {
              if (!snapshot(sim, restored)) return "recorded in-flight save failed";
              hasFired = true;
              break;
            }
        if (tick == ticks - 1 && horse != entt::null) hasUnloaded = ECS::Garrisons::Count(sim.GetRegistry(), horse) == 0;
      }
      if (!hasBoarded || !hasUnloaded || !hasFired) return "recorded transport did not complete its armed trip";
      const auto file = std::filesystem::temp_directory_path() / "openaok-transport-verify.mgl";
      Save::Recording read;
      const bool hasFile = Save::WriteRecording(recording, file.string()) && Save::ReadRecording(file.string(), read);
      std::filesystem::remove(file);
      if (!hasFile) return "transport recording file failed";
      ECS::Simulation replay;
      replay.Init(read.setup);
      size_t cursor = 0, witness = 0;
      for (int tick = 0; tick < ticks; ++tick)
      {
        Save::Replay(read, cursor, replay.Tick() + 1);
        replay.Update(16);
        if (!Save::StillTheGame(read, witness, replay)) return "armed transport replay diverged";
      }
      if (ECS::Digest::Of(sim) != ECS::Digest::Of(replay)) return "armed transport replay ended differently";
      if (const auto* directory = std::getenv("AOE_TRANSPORT_ARTIFACTS"))
      {
        const std::filesystem::path output(directory);
        std::filesystem::create_directories(output);
        if (!Save::WriteFile(sim, (output / "transport.sav").string()) ||
            !Save::WriteRecording(recording, (output / "transport.mgl").string())) return "transport artifacts failed";
        std::printf("  transport capture centre=%d,%d\n", site.m_x, site.m_y);
      }
      return nullptr;
    }

    // Закрывает оба TCP-соединения после проверки синхронного матча.
    struct Peers
    {
      Net::Session m_host, m_client;
      ~Peers() { Net::Leave(m_client); Net::Leave(m_host); }
    };

    const char* network()
    {
      Peers peers;
      if (!Net::Host(peers.m_host, 0, setup(), 2) ||
          !Net::Join(peers.m_client, "127.0.0.1", HAL::Net::BoundPort(peers.m_host.listener))) return "transport TCP failed";
      for (int frame = 0; frame < 500 && !(peers.m_host.started && peers.m_client.started); ++frame)
      {
        Net::Pump(peers.m_host); Net::Pump(peers.m_client);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      }
      if (!(peers.m_host.started && peers.m_client.started)) return "transport TCP match did not start";
      ECS::Simulation host, client;
      host.Init(peers.m_host.setup); client.Init(peers.m_client.setup);
      std::array<entt::entity, 2> archers, spearmen, targets;
      std::array<c::Vector2D, 2> destinations;
      for (Core::PlayerId player : {1, 2})
      {
        Site site;
        if (!findSite(host, player, site)) return "no network transport site";
        Net::Issue(player == 1 ? peers.m_host : peers.m_client, build(host, player, site));
        for (auto* sim : {&host, &client})
        {
          auto& registry = sim->GetRegistry();
          const c::Vector2D at{Fixed32(site.m_x), Fixed32(site.m_y)};
          archers[player - 1] = ECS::Units::Spawn(registry, 4, player, {at.x + Fixed32(1), at.y});
          spearmen[player - 1] = ECS::Units::Spawn(registry, 93, player, {at.x, at.y + Fixed32(1)});
          targets[player - 1] = ECS::Units::Spawn(registry, 38, player == 1 ? 2 : 1, {at.x + Fixed32(3), at.y});
          for (const auto entity : registry.view<c::Combat>()) registry.get<c::Combat>(entity).stance = c::Stance::NoAttack;
          registry.get<c::Health>(targets[player - 1]) = {1000, 1000};
          ECS::Players::SetResource(*ECS::Players::Find(registry, player),
            Core::Rules::Res::PopulationHeadroom, Fixed32(10));
        }
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
      bool sentBoard = false, sentAttack = false, sentUnload = false;
      for (int frame = 0; frame < 30000 && (host.Tick() < ticks || client.Tick() < ticks); ++frame)
      {
        Net::Pump(peers.m_host); Net::Pump(peers.m_client);
        advance(peers.m_host, host); advance(peers.m_client, client);
        if (!peers.m_host.failure.empty() || !peers.m_client.failure.empty() || peers.m_host.desynced || peers.m_client.desynced)
          return "transport TCP peers diverged";
        if (!sentBoard && std::min(host.Tick(), client.Tick()) >= 6500)
        {
          for (Core::PlayerId player : {1, 2})
          {
            const auto horse = livingHorse(host.GetRegistry(), player);
            if (horse == entt::null) return "network villager did not build transport";
            auto& session = player == 1 ? peers.m_host : peers.m_client;
            for (const auto passenger : {workers(host, player).back(), archers[player - 1], spearmen[player - 1]})
              Net::Issue(session, Core::GarrisonCommand{passenger, horse, player});
          }
          sentBoard = true;
        }
        if (!sentAttack && std::min(host.Tick(), client.Tick()) >= 7500)
        {
          for (Core::PlayerId player : {1, 2})
            Net::Issue(player == 1 ? peers.m_host : peers.m_client,
              Core::AttackCommand{livingHorse(host.GetRegistry(), player), targets[player - 1], player});
          sentAttack = true;
        }
        if (!sentUnload && std::min(host.Tick(), client.Tick()) >= 8500)
        {
          for (Core::PlayerId player : {1, 2})
          {
            const auto horse = livingHorse(host.GetRegistry(), player);
            if (ECS::Garrisons::Count(host.GetRegistry(), horse) != 3) return "network passenger did not board";
            if (host.GetRegistry().get<c::Health>(targets[player - 1]).current_hp >= 1000)
              return "network transport did not fire";
            const auto at = host.GetRegistry().get<c::Transform>(horse).position;
            const auto to = ECS::Navigation::Toward(host.GetRegistry(), host.GetGrid(), horse, {at.x + Fixed32(4), at.y});
            destinations[player - 1] = to;
            Net::Issue(player == 1 ? peers.m_host : peers.m_client, Core::UnloadCommand{horse, to.x, to.y, player});
          }
          sentUnload = true;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(100));
      }
      if (!sentUnload || host.Tick() != ticks || client.Tick() != ticks || ECS::Digest::Of(host) != ECS::Digest::Of(client))
        return "network transport match did not finish identically";
      for (Core::PlayerId player : {1, 2})
      {
        const auto horse = livingHorse(host.GetRegistry(), player);
        if (ECS::Garrisons::Count(host.GetRegistry(), horse) != 0)
        {
          const auto& registry = host.GetRegistry();
          const auto at = registry.get<c::Transform>(horse).position;
          const auto to = destinations[player - 1];
          const auto* landing = registry.try_get<c::Landing>(horse);
          std::printf("  transport unload: player=%d cargo=%d at=%d,%d goal=%d,%d landing=%d attempts=%d moving=%d\n",
            player, ECS::Garrisons::Count(registry, horse), at.x.ToInt(), at.y.ToInt(), to.x.ToInt(), to.y.ToInt(),
            landing != nullptr, landing != nullptr ? landing->attempts : -1, ECS::transport_combat::isMoving(registry, horse));
          if (const auto* directory = std::getenv("AOE_TRANSPORT_ARTIFACTS"))
            Save::WriteFile(host, (std::filesystem::path(directory) / "transport-network-failed.sav").string());
          return "network passenger did not unload";
        }
      }
      return nullptr;
    }
  }

  bool CheckTransport()
  {
    const char* failure = nullptr;
    const char* (*checks[])() = {construction, passengers, persistence, network, firing};
    for (const auto check : checks)
    {
      if (const auto* error = check())
      {
        std::printf("  transport check: %s\n", error);
        if (failure == nullptr) failure = error;
      }
      Core::CommandQueue::Get().PopAllCommands();
    }
    std::printf("transport=%s\n", failure != nullptr ? failure : "ok");
    return failure == nullptr;
  }
}
