[Russian version](README_ru.txt)

Verified Windows working directory

This folder preserves the local scripts used, without game files,
dependency archives, installed fonts, builds, screenshots, or saves.
The scripts expect neighboring OpenAoK-source and OpenAoK-build-support folders.
Restore them in a working directory of your choice with these PowerShell
commands (only for a new clone, never over an active project):

  git clone https://github.com/AppleVetal/AgeOfEmpire2-openaok.git OpenAoK-source
  New-Item -ItemType Directory OpenAoK-build-support
  Copy-Item OpenAoK-source\local-support\* OpenAoK-build-support
  Copy-Item OpenAoK-source\local-support\Play-OpenAoK.cmd .
  New-Item -ItemType Directory OpenAoK-build-support\downloads, OpenAoK-build-support\logs, OpenAoK-build-support\captures

Building
  The verified tools were PowerShell 7, Python 3.11, CMake 3.26.4, Ninja 1.12.1,
  GCC 12.1.0 MinGW-w64, and zlib from MinGW. C++20 is required.
  MinGW defaults to C:\msys64\mingw64, and Ninja to the copy included with
  Visual Studio 2022 Community. For other locations, pass -MingwRoot
  and -NinjaPath to Build-Native.ps1. CMake and Python must be in PATH.

  Place these three archives in OpenAoK-build-support\downloads:
  SDL2-devel-2.32.10-mingw.tar.gz
    https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-devel-2.32.10-mingw.tar.gz
  freetype-2.13.3.tar.xz
    https://download.savannah.gnu.org/releases/freetype/freetype-2.13.3.tar.xz
  win-iconv-0.0.10.tar.gz
    https://github.com/win-iconv/win-iconv/archive/refs/tags/v0.0.10.tar.gz
    Save the last file under exactly the name shown.
  The script checks their SHA-256 hashes before extraction; the hashes are in it.

  .\OpenAoK-build-support\Build-Native.ps1 -Jobs 8

Data and launch
  Put your own installed The Age of Kings files (1999, VER 5.7, 13 civilizations)
  in OpenAoK-source\original_game_1999\GAME, including Data,
  LANGUAGE.dll, and empires2.exe. The tool checks the required files and uses
  available Windows fonts. This prepares an installed copy;
  the general upstream CD preparation process is in tools/data/prepare_data.py.

  python OpenAoK-build-support\Prepare-Installed.py
  .\Play-OpenAoK.cmd

  The executable runs from OpenAoK-source\build. The neighboring
  OpenAoK-source\mods\worldcraft folder is required. To package the Windows
  installer with game data and the mod, see windows/installer/README.txt;
  development tools are not needed on the gaming computer. Mod delivery
  through the upstream Android/iOS/Switch packagers has not been added.

  New Worldcraft objects use the installed language choice, or English if none is saved.
  Run from OpenAoK-source\build for Russian names, commands, and tooltips:
    .\OpenAoK.exe --worldcraft-language ru
  Use --worldcraft-language en for English. The original game's text still
  comes from your game data; this option changes only the Worldcraft additions.

Checks
  python OpenAoK-build-support\Verify-Worldcraft.py
  python OpenAoK-source\local-support\Verify-Earthworks.py
  python OpenAoK-build-support\Verify-Baseline.py
  python OpenAoK-build-support\Capture-Worldcraft-Network.py

  Verify-Worldcraft.py checks planting, networking, saves, and replays and creates
  demonstration saves in OpenAoK-build-support\captures\worldcraft.
  Verify-Earthworks.py checks earthworks together with forests, networking,
  saves, replays, and fog of war. Saves for the three mountain heights, moat,
  lake, and queue stay in OpenAoK-build-support\captures\earthworks. The --only
  option selects scenarios, for example --only earthworks,earthnet.
  Verify-Baseline.py returns a nonzero code while the three known failures
  listed in CHECKPOINT.txt remain. They are not hidden and thresholds are not relaxed.
  Capture-Worldcraft.py checks the game window; additional arguments are passed
  to the engine. Area preview example:

  python OpenAoK-build-support\Capture-Worldcraft.py area-preview --reveal all --clicks "97,686;down:620,300;move:620,372"

  A similar mountain button and area check with a 1366x768 panel:
  python OpenAoK-source\local-support\Capture-Earthworks.py mountain-preview --reveal all --clicks "179,686;220,645;down:800,280;move:800,350"

  For a tall mountain, click the third button in the first row after opening the panel:
  python OpenAoK-source\local-support\Capture-Earthworks.py high-preview --reveal all --clicks "179,686;138,645;220,645;move:800,280"

  The scripts create local logs and screenshots. Do not add them to Git.

Installed language selection
  The Windows installer saves the chosen English/Russian language for every launch.
  No extra launch option is needed. --worldcraft-language en|ru overrides the
  saved choice for one run; without a saved choice, English is used.
