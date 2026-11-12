#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Core::trojan_horse
{
  inline constexpr int unitId = 1003;
  inline constexpr int corpseId = 1004;
  inline constexpr int idleGraphic = 59010;
  inline constexpr int walkGraphic = 59011;
  inline constexpr int dieGraphic = 59012;
  inline constexpr int corpseGraphic = 59013;
  inline constexpr const char* archivePath = "../mods/worldcraft/transport/trojan-horse.drs";

  // Хранит параметры строительства и подписи наземного транспорта.
  struct Settings
  {
    int m_woodCost = 0;
    int m_goldCost = 0;
    int m_population = 0;
    int m_workMs = 0;
    int m_seats = 0;
    int m_spearRange = 0;
    int m_spearProjectile = -1;
    std::vector<int> m_spearUnits;
    std::string m_build;
    std::string m_help;
    std::string m_populationWait;
    std::string m_unloadHelp;
  };

  bool load(const std::filesystem::path& root, std::string& error);
  const Settings& settings();
  bool canCarry(int unitId, int unitClass);
}
