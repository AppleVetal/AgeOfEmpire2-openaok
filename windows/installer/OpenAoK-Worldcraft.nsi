Unicode true
!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"
!include "WinVer.nsh"

!define APP_NAME "OpenAoK Worldcraft"
!define REG_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenAoKWorldcraft"
Name "${APP_NAME}"
OutFile "${OUTPUT}"
InstallDir "$LOCALAPPDATA\Programs\OpenAoK Worldcraft"
InstallDirRegKey HKCU "${REG_KEY}" "InstallLocation"
RequestExecutionLevel user
ManifestSupportedOS Win10
SetCompressor /SOLID lzma
SetCompressorDictSize 32
SetDatablockOptimize on
ShowInstDetails show
ShowUninstDetails show
VIProductVersion "${APP_VERSION}.0"
VIAddVersionKey /LANG=1049 "ProductName" "${APP_NAME}"
VIAddVersionKey /LANG=1049 "FileDescription" "Установка OpenAoK с дополнением Worldcraft"
VIAddVersionKey /LANG=1049 "FileVersion" "${APP_VERSION}"
VIAddVersionKey /LANG=1049 "LegalCopyright" "OpenAoK contributors; game data belongs to its owners"

!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TITLE "Установка OpenAoK Worldcraft"
!define MUI_WELCOMEPAGE_TEXT "Жители могут выращивать лес, поднимать горы, рыть сухие рвы и восстанавливать землю: линией или областью.$\r$\n$\r$\nИгровые данные включены. После установки запускайте игру с ярлыка.$\r$\n$\r$\nWindows 10/11, 64 бита. Средства разработки и интернет для одиночной игры не требуются."
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_TEXT "Запустить OpenAoK Worldcraft"
!define MUI_FINISHPAGE_RUN_FUNCTION LaunchGame
!insertmacro MUI_PAGE_FINISH
!define MUI_UNCONFIRMPAGE_TEXT_TOP "Будут удалены установленные файлы OpenAoK Worldcraft.$\r$\n$\r$\nСохранения и профиль игрока останутся в папке пользователя."
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "Russian"

Function .onInit
  SetShellVarContext current
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "Для игры нужна 64-разрядная Windows 10 или 11."
    SetErrorLevel 2
    Abort
  ${EndIf}
  ${IfNot} ${AtLeastWin10}
    MessageBox MB_ICONSTOP "Для игры нужна Windows 10 или 11."
    SetErrorLevel 2
    Abort
  ${EndIf}
FunctionEnd

Function LaunchGame
  SetOutPath "$INSTDIR\build"
  Exec '"$INSTDIR\build\OpenAoK.exe" --windowed'
FunctionEnd

Section "Игра"
  SetOutPath "$INSTDIR"
  File /r "${PAYLOAD}\*.*"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKCU "${REG_KEY}" "DisplayName" "${APP_NAME}"
  WriteRegStr HKCU "${REG_KEY}" "DisplayVersion" "${APP_VERSION}"
  WriteRegStr HKCU "${REG_KEY}" "Publisher" "OpenAoK contributors / Worldcraft"
  WriteRegStr HKCU "${REG_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${REG_KEY}" "DisplayIcon" "$INSTDIR\build\OpenAoK.exe"
  WriteRegStr HKCU "${REG_KEY}" "UninstallString" '$\"$INSTDIR\Uninstall.exe$\"'
  WriteRegStr HKCU "${REG_KEY}" "QuietUninstallString" '$\"$INSTDIR\Uninstall.exe$\" /S'
  WriteRegStr HKCU "${REG_KEY}" "URLInfoAbout" "https://github.com/AppleVetal/AgeOfEmpire2-openaok"
  WriteRegDWORD HKCU "${REG_KEY}" "EstimatedSize" ${INSTALL_KB}
  WriteRegDWORD HKCU "${REG_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${REG_KEY}" "NoRepair" 1
  CreateDirectory "$SMPROGRAMS\${APP_NAME}"
  SetOutPath "$INSTDIR\build"
  CreateShortCut "$DESKTOP\${APP_NAME}.lnk" "$INSTDIR\build\OpenAoK.exe" "--windowed"
  CreateShortCut "$SMPROGRAMS\${APP_NAME}\Играть.lnk" "$INSTDIR\build\OpenAoK.exe" "--windowed"
  CreateShortCut "$SMPROGRAMS\${APP_NAME}\Удалить.lnk" "$INSTDIR\Uninstall.exe"
  CreateShortCut "$SMPROGRAMS\${APP_NAME}\Управление.lnk" "$INSTDIR\PLAY.txt"
SectionEnd

Function un.onInit
  SetShellVarContext current
  IfFileExists "$INSTDIR\.openaok-worldcraft" valid 0
    MessageBox MB_ICONSTOP "Не найдена установленная копия OpenAoK Worldcraft. Удаление остановлено."
    SetErrorLevel 2
    Abort
  valid:
FunctionEnd

Section "Uninstall"
  ; Stop before removing any data when the executable is still in use.
  ClearErrors
  Delete "$INSTDIR\build\OpenAoK.exe"
  ${If} ${Errors}
    MessageBox MB_ICONSTOP "Закройте игру и повторите удаление."
    SetErrorLevel 2
    Abort
  ${EndIf}
  ; Generated exact file list: no recursive deletion of user-selected folders.
  !include "${UNINSTALL_FILES}"
  Delete "$DESKTOP\${APP_NAME}.lnk"
  Delete "$SMPROGRAMS\${APP_NAME}\Играть.lnk"
  Delete "$SMPROGRAMS\${APP_NAME}\Удалить.lnk"
  Delete "$SMPROGRAMS\${APP_NAME}\Управление.lnk"
  RMDir "$SMPROGRAMS\${APP_NAME}"
  DeleteRegKey HKCU "${REG_KEY}"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
SectionEnd
