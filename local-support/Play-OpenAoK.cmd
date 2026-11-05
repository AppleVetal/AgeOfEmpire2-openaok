@echo off
setlocal
if not exist "%~dp0OpenAoK-source\build\OpenAoK.exe" (
  echo OpenAoK.exe is missing. Build the engine first.
  pause
  exit /b 1
)
start "" /D "%~dp0OpenAoK-source\build" "%~dp0OpenAoK-source\build\OpenAoK.exe" --windowed
