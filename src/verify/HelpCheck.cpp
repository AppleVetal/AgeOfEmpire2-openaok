#include "verify/Harness.h"

#include <cstdio>
#include <string>

#include "core/ConfigManager.h"
#include "core/Hotkeys.h"
#include "core/Strings.h"
#include "ecs/ScenarioWorld.h"
#include "ecs/Simulation.h"
#include "ecs/components/Selectable.h"
#include "rendering/UIRenderer.h"

namespace Verify {

namespace {

using UI = Rendering::UIRenderer;
constexpr int kBuildingsHelp = 41061;   // "Buildings": the villager's economic build menu
constexpr int kHouse = 70;

bool EndsWith(const std::string& text, const std::string& end) {
    return text.size() >= end.size() && text.compare(text.size() - end.size(), end.size(), end) == 0;
}

// Проверяет локализованный шаблон подсказки и фактическую привязку кнопки.
bool hasHotkey(const UI::Command& command, int group, int item, SDL_Keycode key)
{
  SDL_KeyboardEvent event{};
  event.keysym.sym = key;
  return command.hotkey_group == group && command.hotkey_item == item &&
    Core::Hotkeys::Is(event, command.hotkey_group, command.hotkey_item);
}

std::string hotkeySuffix(const char* letter)
{
  auto text = Core::Strings::Get(19732);
  const auto at = text.find("%s");
  if (at == std::string::npos) return {};
  text.replace(at, 2, letter);
  return text;
}

} // namespace

// The rollover help over a command button ends with the key that presses it,
// as the original's does: a villager's Buildings "(Hotkey: B)", and in that
// menu the House "(Hotkey: E)".
bool CheckHelp() {
    const char* verdict = nullptr;
    ECS::Simulation sim;
    sim.Init();
    auto& reg = sim.GetRegistry();
    auto& cfg = Core::ConfigManager::Get();
    sim.Update(16);
    const auto [sx, sy] = sim.player_starts[0];
    const entt::entity villager = ECS::ScenarioWorld::Place(
        reg, sim.GetGrid(), cfg.GetUnitIdByName("VMBAS"), 1,
        {Core::Fixed32(sx + 4), Core::Fixed32(sy + 4)}, {}, 0);
    std::string buildings, house;
    if (villager == entt::null) {
        verdict = "no villager placed";
    } else {
        auto all = reg.view<ECS::Components::Selectable>();
        for (auto e : all) all.get<ECS::Components::Selectable>(e).is_selected = e == villager;
        auto help = [&](auto match) {
            UI::Command cmds[UI::kCommandSlots];
            const int n = UI::BuildCommands(sim, 1024, 768, cmds, UI::kCommandSlots);
            for (int i = 0; i < n; ++i) {
                if (match(cmds[i])) return UI::CommandHelp(sim, cmds[i]);
            }
            return std::string();
        };
        buildings = help([](const UI::Command& c) { return c.name_string == kBuildingsHelp; });
        UI::OpenBuildMenu(sim, Core::ConfigManager::kInterfaceEconomic);
        house = help([](const UI::Command& c) {
            return c.kind == UI::CommandKind::Produce && c.places_building && c.unit_id == kHouse;
        });
        if (buildings.empty() || house.empty()) {
            verdict = "no help for the Buildings or the House button";
        } else if (hotkeySuffix("B").empty() || hotkeySuffix("E").empty() ||
                   !EndsWith(buildings, hotkeySuffix("B")) || !EndsWith(house, hotkeySuffix("E"))) {
            verdict = "a button's help does not end with its hotkey";
        }
    }
    if (verdict == nullptr)
    {
      const auto townCenter = cfg.GetUnitIdByName("RTWC");
      const auto building = ECS::ScenarioWorld::Place(reg, sim.GetGrid(), townCenter, 1,
        {Core::Fixed32(sx + 15), Core::Fixed32(sy + 15)}, {}, 0);
      if (building == entt::null) verdict = "no Town Center placed";
      else
      {
        for (auto entity : reg.view<ECS::Components::Selectable>())
          reg.get<ECS::Components::Selectable>(entity).is_selected = entity == building;
        UI::Command commands[UI::kCommandSlots];
        const auto count = UI::BuildCommands(sim, 1024, 768, commands, UI::kCommandSlots);
        bool hasVillagerKey = false;
        for (int i = 0; i < count; ++i)
          if (commands[i].kind == UI::CommandKind::Produce && commands[i].unit_id == 83)
            hasVillagerKey = hasHotkey(commands[i], Core::Hotkeys::kTownCenter,
              Core::Hotkeys::kVillager, SDLK_c);
        if (!hasVillagerKey) verdict = "the Town Center cannot train a villager by hotkey";
      }
    }
    std::printf("help=%s\n", verdict == nullptr ? "ok" : verdict);
    return verdict == nullptr;
}

} // namespace Verify
