Проверенный рабочий каталог Windows

Здесь сохранены использованные локальные скрипты без игровых файлов,
архивов зависимостей, установленных шрифтов, сборок, снимков и сохранений.
Скрипты рассчитаны на соседние каталоги OpenAoK-source и OpenAoK-build-support.
Их можно восстановить в любом выбранном рабочем каталоге следующими командами
PowerShell (выполнять только для нового клона, не поверх действующего проекта):

  git clone https://github.com/AppleVetal/AgeOfEmpire2-openaok.git OpenAoK-source
  New-Item -ItemType Directory OpenAoK-build-support
  Copy-Item OpenAoK-source\local-support\* OpenAoK-build-support
  Copy-Item OpenAoK-source\local-support\Play-OpenAoK.cmd .
  New-Item -ItemType Directory OpenAoK-build-support\downloads, OpenAoK-build-support\logs, OpenAoK-build-support\captures

Сборка
  Использовались PowerShell 7, Python 3.11, CMake 3.26.4, Ninja 1.12.1,
  GCC 12.1.0 MinGW-w64 и zlib из MinGW. Нужен C++20.
  По умолчанию MinGW находится в C:\msys64\mingw64, а Ninja — в составе
  Visual Studio 2022 Community. При другом расположении передайте параметры
  -MingwRoot и -NinjaPath скрипту Build-Native.ps1. CMake и Python нужны в PATH.

  В OpenAoK-build-support\downloads поместите три архива:
  SDL2-devel-2.32.10-mingw.tar.gz
    https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-devel-2.32.10-mingw.tar.gz
  freetype-2.13.3.tar.xz
    https://download.savannah.gnu.org/releases/freetype/freetype-2.13.3.tar.xz
  win-iconv-0.0.10.tar.gz
    https://github.com/win-iconv/win-iconv/archive/refs/tags/v0.0.10.tar.gz
    Последний файл сохраните именно под указанным именем.
  Скрипт проверяет их SHA-256 перед распаковкой; значения находятся в скрипте.

  .\OpenAoK-build-support\Build-Native.ps1 -Jobs 8

Данные и запуск
  Собственные файлы установленной The Age of Kings (1999, VER 5.7, 13 цивилизаций)
  поместите в OpenAoK-source\original_game_1999\GAME, включая Data,
  LANGUAGE.dll и empires2.exe. Утилита проверяет нужные файлы и использует
  имеющиеся шрифты Windows. Это путь подготовки установленной версии;
  общий upstream-процесс подготовки CD описан в tools/data/prepare_data.py.

  python OpenAoK-build-support\Prepare-Installed.py
  .\Play-OpenAoK.cmd

  Исполняемый файл запускается из OpenAoK-source\build. Соседняя папка
  OpenAoK-source\mods\worldcraft обязательна. Установщик Windows с игровыми
  данными и дополнением собирается по windows/installer/README.txt; на игровом
  компьютере средства разработки не нужны. Доставка дополнения через
  upstream-упаковщики Android/iOS/Switch пока не добавлена.

Проверки
  python OpenAoK-build-support\Verify-Worldcraft.py
  python OpenAoK-source\local-support\Verify-Earthworks.py
  python OpenAoK-build-support\Verify-Baseline.py
  python OpenAoK-build-support\Capture-Worldcraft-Network.py

  Verify-Worldcraft.py проверяет посадку, сеть, сохранения и повторы и создаёт
  демонстрационные сохранения в OpenAoK-build-support\captures\worldcraft.
  Verify-Earthworks.py проверяет земляные работы вместе с лесом, сетью,
  сохранениями, повторами и туманом войны. Сохранения трёх высот горы, рва, озера и очереди
  остаются в OpenAoK-build-support\captures\earthworks. Параметр --only
  позволяет выбрать сценарии, например --only earthworks,earthnet.
  Verify-Baseline.py возвращает ненулевой код, пока сохраняются три известные
  ошибки, перечисленные в CHECKPOINT.txt. Это не скрывается и не ослабляет пороги.
  Capture-Worldcraft.py проверяет окно игры; дополнительные параметры передаются
  движку. Пример предпросмотра области:

  python OpenAoK-build-support\Capture-Worldcraft.py area-preview --reveal all --clicks "97,686;down:620,300;move:620,372"

  Аналогичная проверка кнопки горы и её области при панели 1366x768:
  python OpenAoK-source\local-support\Capture-Earthworks.py mountain-preview --reveal all --clicks "179,686;220,645;down:800,280;move:800,350"

  Для высокой горы после открытия панели нажмите третью кнопку первого ряда:
  python OpenAoK-source\local-support\Capture-Earthworks.py high-preview --reveal all --clicks "179,686;138,645;220,645;move:800,280"

  Скрипты создают локальные журналы и снимки. В Git их добавлять не нужно.
