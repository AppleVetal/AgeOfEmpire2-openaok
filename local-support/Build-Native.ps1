param(
  [ValidateRange(1, 16)][int]$Jobs = 4,
  [string]$MingwRoot = 'C:\msys64\mingw64',
  [string]$NinjaPath = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
)

$ErrorActionPreference = 'Stop'
$supportRoot = $PSScriptRoot
$repoRoot = Join-Path (Split-Path $supportRoot -Parent) 'OpenAoK-source'
$downloadsRoot = Join-Path $supportRoot 'downloads'
$depsRoot = Join-Path $supportRoot 'deps'
$prefixRoot = Join-Path $supportRoot 'prefix'
$logsRoot = Join-Path $supportRoot 'logs'
$cmakePath = (Get-Command cmake -ErrorAction Stop).Source
$gccPath = Join-Path $MingwRoot 'bin\gcc.exe'
$gxxPath = Join-Path $MingwRoot 'bin\g++.exe'
$makePath = Join-Path $MingwRoot 'bin\mingw32-make.exe'
foreach ($toolPath in @($gccPath, $gxxPath, $makePath, $NinjaPath)) {
  if (-not (Test-Path -LiteralPath $toolPath)) { throw "Missing build tool: $toolPath" }
}

function Invoke-Logged {
  param([string]$Name, [string]$Program, [string[]]$Arguments)
  $logPath = Join-Path $logsRoot ($Name + '.log')
  & $Program @Arguments *> $logPath
  $result = $LASTEXITCODE
  Get-Content -LiteralPath $logPath -Tail 12
  if ($result -ne 0) { throw "$Name failed with exit code $result. Log: $logPath" }
}

$packages = @(
  @('SDL2-devel-2.32.10-mingw.tar.gz', '83a5d74012311edc3c0d40ea6faecbe57ad692aa033fa5dc273cc937e3938ff2', 'SDL2-2.32.10'),
  @('freetype-2.13.3.tar.xz', '0550350666d427c74daeb85d5ac7bb353acba5f76956395995311a9c6f063289', 'freetype-2.13.3'),
  @('win-iconv-0.0.10.tar.gz', '58493387c7c9c70d61e711ec2feec5db0a59d164556642d2b427dde4ef756bc1', 'win-iconv-0.0.10')
)

$previousPath = $env:PATH
$previousMake = $env:MAKE
try {
  $env:PATH = (Join-Path $MingwRoot 'bin') + ';' + $previousPath
  $env:MAKE = $makePath.Replace('\', '/')
  New-Item -ItemType Directory -Force -Path $depsRoot, $prefixRoot, $logsRoot | Out-Null
  foreach ($package in $packages) {
    $archivePath = Join-Path $downloadsRoot $package[0]
    if ((Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash -ine $package[1]) {
      throw "Archive checksum mismatch: $archivePath"
    }
    if (-not (Test-Path -LiteralPath (Join-Path $depsRoot $package[2]))) {
      Invoke-Logged -Name ('unpack-' + $package[2]) -Program $cmakePath -Arguments @('-E', 'chdir', $depsRoot, $cmakePath, '-E', 'tar', 'xf', $archivePath)
    }
  }

  $compilerArgs = @('-G', 'Ninja', "-DCMAKE_MAKE_PROGRAM=$NinjaPath", "-DCMAKE_C_COMPILER=$gccPath", "-DCMAKE_CXX_COMPILER=$gxxPath", '-DCMAKE_BUILD_TYPE=Release') | ForEach-Object { $_.Replace('\', '/') }
  $freetypeBuild = Join-Path $supportRoot 'freetype-build'
  Invoke-Logged -Name 'freetype-configure' -Program $cmakePath -Arguments (@('-S', (Join-Path $depsRoot 'freetype-2.13.3'), '-B', $freetypeBuild) + $compilerArgs + @("-DCMAKE_INSTALL_PREFIX=$prefixRoot", '-DBUILD_SHARED_LIBS=OFF', '-DFT_DISABLE_ZLIB=ON', '-DFT_DISABLE_BZIP2=ON', '-DFT_DISABLE_PNG=ON', '-DFT_DISABLE_HARFBUZZ=ON', '-DFT_DISABLE_BROTLI=ON'))
  Invoke-Logged -Name 'freetype-build' -Program $cmakePath -Arguments @('--build', $freetypeBuild, '--parallel', "$Jobs")
  Invoke-Logged -Name 'freetype-install' -Program $cmakePath -Arguments @('--install', $freetypeBuild)

  $genieRoot = Join-Path $depsRoot 'genieutils'
  if (-not (Test-Path -LiteralPath $genieRoot)) {
    Copy-Item -LiteralPath (Join-Path $repoRoot 'third_party\genieutils') -Destination $genieRoot -Recurse
  }
  $iconvRoot = Join-Path $genieRoot 'extern\win-iconv'
  if (-not (Test-Path -LiteralPath $iconvRoot)) {
    New-Item -ItemType Directory -Force -Path (Split-Path $iconvRoot -Parent) | Out-Null
    Copy-Item -LiteralPath (Join-Path $depsRoot 'win-iconv-0.0.10') -Destination $iconvRoot -Recurse
  }
  $dataBuild = Join-Path $repoRoot 'build\data-tools'
  Invoke-Logged -Name 'data-configure' -Program $cmakePath -Arguments (@('-S', (Join-Path $repoRoot 'tools\data'), '-B', $dataBuild) + $compilerArgs + @("-DGENIEUTILS_DIR=$genieRoot", "-DZLIB_INCLUDE_DIR=$MingwRoot/include", "-DZLIB_LIBRARY=$MingwRoot/lib/libz.a"))
  Invoke-Logged -Name 'data-build' -Program $cmakePath -Arguments @('--build', $dataBuild, '--target', 'dump_dat', '--parallel', "$Jobs")

  $sdlRoot = Join-Path $depsRoot 'SDL2-2.32.10\x86_64-w64-mingw32'
  $gameBuild = Join-Path $repoRoot 'build'
  Invoke-Logged -Name 'game-configure' -Program $cmakePath -Arguments (@('-S', $repoRoot, '-B', $gameBuild) + $compilerArgs + @("-DCMAKE_PREFIX_PATH=$sdlRoot;$prefixRoot", "-DCMAKE_EXE_LINKER_FLAGS=-flto=$Jobs"))
  Invoke-Logged -Name 'game-build' -Program $cmakePath -Arguments @('--build', $gameBuild, '--parallel', "$Jobs")
  Copy-Item -LiteralPath (Join-Path $sdlRoot 'bin\SDL2.dll') -Destination (Join-Path $gameBuild 'SDL2.dll') -Force
  Write-Output "Built: $(Join-Path $gameBuild 'OpenAoK.exe')"
  Write-Output 'Game data must be prepared separately from the original Age of Kings CD.'
}
finally {
  $env:PATH = $previousPath
  $env:MAKE = $previousMake
}
