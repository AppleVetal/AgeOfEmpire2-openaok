[Russian version](README_ru.txt)

Building a local Windows installer

Packaging uses the existing build/OpenAoK.exe and SDL2.dll, prepared assets,
fonts, music, and the mods/worldcraft package. The engine is not recompiled.
The recipient installs the game without Python, a compiler, or data preparation.

Compiler: portable NSIS 3.13.
  https://sourceforge.net/projects/nsis/files/NSIS%203/3.13/nsis-3.13.zip/download
  SHA-256: ba63dffc4410ee89193e1cb5a41989991bd77c61068da17e3156d136b7b0b3d8
Script reference:
  https://nsis.sourceforge.io/Docs/Chapter4.html

From the working directory root:
  python OpenAoK-source/windows/installer/Build-Installer.py --nsis OpenAoK-build-support/installer-tools/nsis-3.13/makensis.exe

Output: installers/OpenAoK-Worldcraft-<version>-win64-Setup.exe,
its checksum, and installer-manifest.json. Working files and the full hash list
are in OpenAoK-build-support/installer/work-<date-time>.
Repackaging creates a separate working directory without deleting the old one.

Verifying the finished installer:
  python OpenAoK-source/windows/installer/Verify-Installer.py
It requires that no copy is already installed, creates a separate test folder,
compares every file, runs 10 engine checks, the menu, and a planting match,
then checks uninstallation while preserving an unrelated file. Development
tools are removed from the launched game's PATH. The verification.json report,
screenshots, and logs are saved in the packaging working folder. This checks
the current computer; it does not replace testing on a separate clean Windows system.

NSIS /D= and _?= arguments must be last and unquoted, even when their paths
contain spaces. The verification script follows this rule.
  https://nsis.sourceforge.io/Docs/Chapter3.html

Installation: for the current user, by default in
%LOCALAPPDATA%/Programs/OpenAoK Worldcraft. Shortcuts use build as the working folder.
Uninstallation uses an exact list of installed files and preserves the profile,
saves, and unrelated files. The installation folder is not recursively deleted.

The package includes English PLAY.txt and Russian PLAY_ru.txt instructions,
plus both Worldcraft language dictionaries. The language page offers English
and Русский and writes the choice to build/worldcraft-language.txt. The engine
reads it when no --worldcraft-language en|ru override is supplied. The selected
language is also remembered in the uninstall registry key for reinstallation.
Shortcuts, Play-OpenAoK.cmd, the finish-page launch, and the EXE use that preference.
The installed EXE resolves its data from its own folder, even when launched
from another working directory. The controls shortcut opens PLAY.txt or
PLAY_ru.txt to match the choice.
The preference is outside content hashing and is removed during uninstallation.

For silent installation, use /GAME_LANGUAGE=en or /GAME_LANGUAGE=ru before
the final /D= path. Without the option, the previous selection is retained;
a first installation defaults to English. Invalid language values stop setup.
Verify-Installer.py accepts --game-language en|ru (default ru) to check the
persisted file, registry value, normal game launch, and removal of the preference.
Use an engine built from the same localization-aware source version.

The installer contains local game data and fonts. It is not intended for
public upload to GitHub; only these scripts are stored in the repository.
