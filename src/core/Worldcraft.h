#pragma once

#include <filesystem>
#include <string>
#include "core/Command.h"

namespace Core::worldcraft
{
  constexpr int siteLimit = 256;
  // Хранит проверенные правила посадки и ссылки на графику оригинального комплекта.
  struct PlantRules
  {
    int woodCost = 0;
    int workMs = 0;
    int growthMs = 0;
    int treeUnit = -1;
    int siteUnit = -1;
    int growingUnit = -1;
    int buttonIcon = -1;
  };

  // Задаёт стоимость и трудозатраты одного уровня земляных работ.
  struct WorkRules
  {
    int woodCost = 0;
    int workMs = 0;
  };

  bool load(const std::filesystem::path& root, std::string& error);
  bool validateArt(std::string& error);
  const PlantRules& plantRules();
  WorkRules workRules(WorldJobKind kind);
  const std::string& jobName(WorldJobKind kind);
  std::string earthworkHelp(WorldJobKind kind);
  const std::string& text(const std::string& key);
  std::string plantHelp();
}
