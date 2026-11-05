@echo off
setlocal
start "OpenAoK Worldcraft" /D "%~dp0build" "%~dp0build\OpenAoK.exe" --windowed %*
