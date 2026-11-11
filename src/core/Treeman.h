#pragma once

#include <filesystem>
#include <string>

namespace Core::treeman
{
  inline constexpr int unitId = 1000;
  inline constexpr int corpseId = 1001;
  inline constexpr int wildUnitId = 1002;
  inline constexpr int idleGraphic = 59000;
  inline constexpr int walkGraphic = 59001;
  inline constexpr int attackGraphic = 59002;
  inline constexpr int dieGraphic = 59003;
  inline constexpr int corpseGraphic = 59004;
  inline constexpr const char* archivePath = "../mods/worldcraft/treeman/walking-tree.drs";

  // Хранит проверенные параметры выращивания, дикой популяции, приручения и подписи древня.
  struct Settings
  {
    int woodCost = 0;
    int goldCost = 0;
    int population = 0;
    int workMs = 0;
    int m_wildCount = 0;
    int m_wildStartDistance = 0;
    int m_tameRadius = 0;
    std::string grow;
    std::string help;
    std::string populationWait;
    std::string m_claimedNotice;
  };

  // Called once after the base rules and art tables, before any player is made.
  bool load(const std::filesystem::path& root, std::string& error);
  const Settings& settings();
}
