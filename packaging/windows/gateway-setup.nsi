Unicode true

!ifndef PRODUCT_VERSION
  !error "PRODUCT_VERSION is required"
!endif
!ifndef STAGE_DIR
  !error "STAGE_DIR is required"
!endif
!ifndef OUTPUT_FILE
  !error "OUTPUT_FILE is required"
!endif

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"
!include "FileFunc.nsh"
!include "nsDialogs.nsh"
!include "WinMessages.nsh"

!define PRODUCT_NAME "Vision Realtime"
!define SERVICE_ID "VisionRealtime"
!define COMPANY "EN-CO OHG"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\VisionRealtime"
!define LEGACY_SERVICE_ID "VisionOneIec104Gateway"
!define LEGACY_INSTALL_DIR "$PROGRAMFILES64\EN-CO\Vision One IEC-104 Gateway"
!define LEGACY_UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\VisionOneIec104Gateway"

!macro ShowFatal MESSAGE
  IfSilent +2 0
  MessageBox MB_ICONSTOP "${MESSAGE}"
!macroend

Name "${PRODUCT_NAME} ${PRODUCT_VERSION}"
OutFile "${OUTPUT_FILE}"
InstallDir "$PROGRAMFILES64\EN-CO OHG\Vision Realtime"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
ShowInstDetails show
ShowUninstDetails show

Var ProgramDataDir
Var LegacyProgramDataDir
Var PurgeCheckbox
Var PurgeProgramData
Var GeneratedToken
Var TokenField
Var NewConfigCreated
Var BackupDir
Var HadCurrentInstall
Var CurrentServiceTouched
Var LegacyServiceTouched
Var BackupCreated
Var NewInstallWritten
Var InstallCommitted

!define MUI_ABORTWARNING
!define MUI_CUSTOMFUNCTION_ABORT InstallerAbort
!define MUI_ICON "${STAGE_DIR}\icon.ico"
!define MUI_UNICON "${STAGE_DIR}\icon.ico"
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_INSTFILES
Page custom TokenPageCreate
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_WELCOME
UninstPage custom un.PurgePageCreate un.PurgePageLeave
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "German"

Function .onInit
  ${IfNot} ${RunningX64}
    !insertmacro ShowFatal "${PRODUCT_NAME} requires 64-bit Windows."
    Abort
  ${EndIf}
  SetRegView 64
  SetShellVarContext all
  StrCpy $ProgramDataDir "$APPDATA\EN-CO OHG\Vision Realtime"
  StrCpy $LegacyProgramDataDir "$APPDATA\EN-CO\Vision One IEC-104 Gateway"
  StrCpy $GeneratedToken ""
  StrCpy $NewConfigCreated "0"
  StrCpy $BackupDir "$INSTDIR.rollback"
  StrCpy $HadCurrentInstall "0"
  StrCpy $CurrentServiceTouched "0"
  StrCpy $LegacyServiceTouched "0"
  StrCpy $BackupCreated "0"
  StrCpy $NewInstallWritten "0"
  StrCpy $InstallCommitted "0"
FunctionEnd

Function RollbackInstall
  ${If} $InstallCommitted == "1"
    Return
  ${EndIf}

  ; Remove a partially registered replacement before restoring either wrapper.
  ${If} $NewInstallWritten == "1"
    SetOutPath "$TEMP"
    IfFileExists "$INSTDIR\VisionRealtime.exe" 0 rollback_current_sc
      nsExec::ExecToLog '"$INSTDIR\VisionRealtime.exe" stop'
      Pop $R0
      nsExec::ExecToLog '"$INSTDIR\VisionRealtime.exe" uninstall'
      Pop $R0
    rollback_current_sc:
    nsExec::ExecToLog 'sc.exe query "${SERVICE_ID}"'
    Pop $R0
    ${If} $R0 == 0
      nsExec::ExecToLog 'sc.exe stop "${SERVICE_ID}"'
      Pop $R0
      nsExec::ExecToLog 'sc.exe delete "${SERVICE_ID}"'
      Pop $R0
    ${EndIf}

    StrCpy $R1 0
    rollback_service_wait:
      nsExec::ExecToLog 'sc.exe query "${SERVICE_ID}"'
      Pop $R0
      ${If} $R0 == 0
        IntOp $R1 $R1 + 1
        ${If} $R1 < 120
          Sleep 500
          Goto rollback_service_wait
        ${EndIf}
        SetErrorLevel 1
        !insertmacro ShowFatal "The replacement service is still pending removal. The previous installation remains preserved at $BackupDir."
        Return
      ${EndIf}

    StrCpy $R1 0
    rollback_new_files_retry:
      ClearErrors
      RMDir /r "$INSTDIR"
      ${If} ${Errors}
        IntOp $R1 $R1 + 1
        ${If} $R1 < 20
          Sleep 500
          Goto rollback_new_files_retry
        ${EndIf}
        SetErrorLevel 1
        !insertmacro ShowFatal "The replacement files could not be removed. The previous installation remains preserved at $BackupDir."
        Return
      ${EndIf}
    StrCpy $NewInstallWritten "0"
  ${EndIf}

  ${If} $BackupCreated == "1"
    ClearErrors
    Rename "$BackupDir" "$INSTDIR"
    ${If} ${Errors}
      SetErrorLevel 1
      !insertmacro ShowFatal "The previous installation could not be restored from $BackupDir."
      Return
    ${EndIf}
    StrCpy $BackupCreated "0"
  ${EndIf}

  ${If} $CurrentServiceTouched == "1"
    IfFileExists "$INSTDIR\VisionRealtime.exe" 0 rollback_legacy
      nsExec::ExecToLog '"$INSTDIR\VisionRealtime.exe" install'
      Pop $R0
      ${If} $R0 != 0
        SetErrorLevel 1
        !insertmacro ShowFatal "The previous Vision Realtime service could not be reinstalled (WinSW exit $R0)."
        Return
      ${EndIf}
      nsExec::ExecToLog '"$INSTDIR\VisionRealtime.exe" start'
      Pop $R0
      ${If} $R0 != 0
        SetErrorLevel 1
        !insertmacro ShowFatal "The previous Vision Realtime service could not be restarted (WinSW exit $R0)."
        Return
      ${EndIf}
    StrCpy $CurrentServiceTouched "0"
  ${EndIf}

  rollback_legacy:
  ${If} $LegacyServiceTouched == "1"
    ; These identifiers intentionally refer to the package replaced by Vision Realtime.
    IfFileExists "${LEGACY_INSTALL_DIR}\VisionOneIec104Gateway.exe" 0 rollback_config
      nsExec::ExecToLog '"${LEGACY_INSTALL_DIR}\VisionOneIec104Gateway.exe" install'
      Pop $R0
      ${If} $R0 != 0
        SetErrorLevel 1
        !insertmacro ShowFatal "The legacy service could not be reinstalled (WinSW exit $R0)."
        Return
      ${EndIf}
      nsExec::ExecToLog '"${LEGACY_INSTALL_DIR}\VisionOneIec104Gateway.exe" start'
      Pop $R0
      ${If} $R0 != 0
        SetErrorLevel 1
        !insertmacro ShowFatal "The legacy service could not be restarted (WinSW exit $R0)."
        Return
      ${EndIf}
    StrCpy $LegacyServiceTouched "0"
  ${EndIf}

  rollback_config:
  ${If} $NewConfigCreated == "1"
    Delete "$ProgramDataDir\config\gateway.json"
    StrCpy $NewConfigCreated "0"
  ${EndIf}
FunctionEnd

Function .onInstFailed
  Call RollbackInstall
FunctionEnd

Function InstallerAbort
  Call RollbackInstall
FunctionEnd

Function CleanupLegacyInstall
  ; These identity strings intentionally refer to the package replaced by Vision Realtime.
  nsExec::ExecToLog 'sc.exe query "${LEGACY_SERVICE_ID}"'
  Pop $R0
  ${If} $R0 == 0
    StrCpy $LegacyServiceTouched "1"
    IfFileExists "${LEGACY_INSTALL_DIR}\VisionOneIec104Gateway.exe" 0 legacy_wrapper_missing
      nsExec::ExecToLog '"${LEGACY_INSTALL_DIR}\VisionOneIec104Gateway.exe" stop'
      Pop $R0
      nsExec::ExecToLog '"${LEGACY_INSTALL_DIR}\VisionOneIec104Gateway.exe" uninstall'
      Pop $R0
    legacy_wrapper_missing:
    nsExec::ExecToLog 'sc.exe stop "${LEGACY_SERVICE_ID}"'
    Pop $R0
    nsExec::ExecToLog 'sc.exe delete "${LEGACY_SERVICE_ID}"'
    Pop $R0

    StrCpy $R1 0
    legacy_service_wait:
      nsExec::ExecToLog 'sc.exe query "${LEGACY_SERVICE_ID}"'
      Pop $R0
      ${If} $R0 == 0
        IntOp $R1 $R1 + 1
        ${If} $R1 >= 20
          Call RollbackInstall
          SetErrorLevel 1
          !insertmacro ShowFatal "The legacy service could not be removed. Installation cannot continue safely."
          Abort
        ${EndIf}
        Sleep 500
        Goto legacy_service_wait
      ${EndIf}
  ${EndIf}
FunctionEnd

Function FinalizeInstall
  ; The replacement is healthy; legacy data is retained unless it is already empty.
  IfFileExists "${LEGACY_INSTALL_DIR}" 0 finalize_legacy_registry
  StrCpy $R1 0
  finalize_legacy_files_retry:
    ClearErrors
    RMDir /r "${LEGACY_INSTALL_DIR}"
    ${If} ${Errors}
      IntOp $R1 $R1 + 1
      ${If} $R1 < 20
        Sleep 500
        Goto finalize_legacy_files_retry
      ${EndIf}
      ; A locked old file is safe to remove at reboot after the old service is gone.
      RMDir /r /REBOOTOK "${LEGACY_INSTALL_DIR}"
    ${EndIf}

  finalize_legacy_registry:
  SetRegView 64
  DeleteRegKey HKLM "${LEGACY_UNINSTALL_KEY}"
  SetRegView 32
  DeleteRegKey HKLM "${LEGACY_UNINSTALL_KEY}"
  SetRegView 64

  ; Non-recursive removal preserves any legacy configuration, state, or logs.
  RMDir "$LegacyProgramDataDir\config"
  RMDir "$LegacyProgramDataDir\state"
  RMDir "$LegacyProgramDataDir\logs\service"
  RMDir "$LegacyProgramDataDir\logs"
  RMDir "$LegacyProgramDataDir"

  ${If} $BackupCreated == "1"
    StrCpy $R1 0
    finalize_backup_retry:
      ClearErrors
      RMDir /r "$BackupDir"
      ${If} ${Errors}
        IntOp $R1 $R1 + 1
        ${If} $R1 < 20
          Sleep 500
          Goto finalize_backup_retry
        ${EndIf}
        RMDir /r /REBOOTOK "$BackupDir"
      ${EndIf}
    StrCpy $BackupCreated "0"
  ${EndIf}
  ClearErrors
FunctionEnd

Section "Vision Realtime service" SEC_GATEWAY
  SectionIn RO
  SetRegView 64
  StrCpy $BackupDir "$INSTDIR.rollback"

  ; Refuse to overwrite an unresolved backup before changing either service.
  IfFileExists "$BackupDir" 0 backup_path_clear
    SetErrorLevel 1
    !insertmacro ShowFatal "A previous Vision Realtime rollback backup still exists at $BackupDir. Installation cannot continue safely."
    Abort
  backup_path_clear:

  IfFileExists "$INSTDIR\*" 0 current_install_checked
    StrCpy $HadCurrentInstall "1"
  current_install_checked:
  Call CleanupLegacyInstall

  nsExec::ExecToLog 'sc.exe query "${SERVICE_ID}"'
  Pop $R0
  ${If} $R0 == 0
    StrCpy $CurrentServiceTouched "1"
    IfFileExists "$INSTDIR\VisionRealtime.exe" 0 current_wrapper_missing
      nsExec::ExecToLog '"$INSTDIR\VisionRealtime.exe" stop'
      Pop $R0
      nsExec::ExecToLog '"$INSTDIR\VisionRealtime.exe" uninstall'
      Pop $R0
    current_wrapper_missing:
    nsExec::ExecToLog 'sc.exe stop "${SERVICE_ID}"'
    Pop $R0
    nsExec::ExecToLog 'sc.exe delete "${SERVICE_ID}"'
    Pop $R0

    StrCpy $R1 0
    current_service_wait:
      nsExec::ExecToLog 'sc.exe query "${SERVICE_ID}"'
      Pop $R0
      ${If} $R0 == 0
        IntOp $R1 $R1 + 1
        ${If} $R1 >= 20
          Call RollbackInstall
          SetErrorLevel 1
          !insertmacro ShowFatal "The existing service could not be unregistered. Installation cannot continue safely."
          Abort
        ${EndIf}
        Sleep 500
        Goto current_service_wait
      ${EndIf}
  ${EndIf}

  ${If} $HadCurrentInstall == "1"
    ClearErrors
    Rename "$INSTDIR" "$BackupDir"
    ${If} ${Errors}
      Call RollbackInstall
      SetErrorLevel 1
      !insertmacro ShowFatal "The previous Vision Realtime installation could not be preserved. Installation cannot continue safely."
      Abort
    ${EndIf}
    StrCpy $BackupCreated "1"
  ${EndIf}

  StrCpy $NewInstallWritten "1"
  ClearErrors
  SetOutPath "$INSTDIR\bin"
  File "${STAGE_DIR}\bin\vision-realtime.exe"
  File "${STAGE_DIR}\bin\lib60870.dll"
  SetOutPath "$INSTDIR"
  File "${STAGE_DIR}\VisionRealtime.exe"
  File "${STAGE_DIR}\VisionRealtime.xml"
  File "${STAGE_DIR}\QUICKSTART.txt"
  SetOutPath "$INSTDIR\licenses"
  File /r "${STAGE_DIR}\licenses\*"
  ${If} ${Errors}
      Call RollbackInstall
      SetErrorLevel 1
      !insertmacro ShowFatal "Vision Realtime program files could not be installed."
      Abort
  ${EndIf}
  ClearErrors
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  ${If} ${Errors}
      Call RollbackInstall
      SetErrorLevel 1
      !insertmacro ShowFatal "The Vision Realtime uninstaller could not be created."
      Abort
  ${EndIf}

  ClearErrors
  CreateDirectory "$ProgramDataDir\config"
  CreateDirectory "$ProgramDataDir\state"
  CreateDirectory "$ProgramDataDir\logs\service"
  ${If} ${Errors}
      Call RollbackInstall
      SetErrorLevel 1
      !insertmacro ShowFatal "Vision Realtime ProgramData directories could not be created."
      Abort
  ${EndIf}

  ; Protect the root, then reset existing children so they inherit only these restricted entries.
  nsExec::ExecToLog 'icacls.exe "$ProgramDataDir" /inheritance:r /grant:r "*S-1-5-18:(OI)(CI)F" "*S-1-5-32-544:(OI)(CI)F" "*S-1-5-19:(OI)(CI)M" /C /Q'
  Pop $0
  ${If} $0 != 0
    Call RollbackInstall
    SetErrorLevel 1
    !insertmacro ShowFatal "Could not protect $ProgramDataDir (icacls exit $0)."
    Abort
  ${EndIf}
  nsExec::ExecToLog 'icacls.exe "$ProgramDataDir\*" /reset /T /C /Q'
  Pop $0
  ${If} $0 != 0
    Call RollbackInstall
    SetErrorLevel 1
    !insertmacro ShowFatal "Could not reset permissions below $ProgramDataDir (icacls exit $0)."
    Abort
  ${EndIf}
  nsExec::ExecToLog 'icacls.exe "$ProgramDataDir\*" /inheritance:e /T /C /Q'
  Pop $0
  ${If} $0 != 0
    Call RollbackInstall
    SetErrorLevel 1
    !insertmacro ShowFatal "Could not enable inherited permissions below $ProgramDataDir (icacls exit $0)."
    Abort
  ${EndIf}
  nsExec::ExecToLog 'icacls.exe "$ProgramDataDir\*" /grant:r "*S-1-5-18:F" "*S-1-5-32-544:F" "*S-1-5-19:M" /T /C /Q'
  Pop $0
  ${If} $0 != 0
    Call RollbackInstall
    SetErrorLevel 1
    !insertmacro ShowFatal "Could not apply effective permissions below $ProgramDataDir (icacls exit $0)."
    Abort
  ${EndIf}

  IfFileExists "$ProgramDataDir\config\gateway.json" config_ready
    StrCpy $NewConfigCreated "1"
    File /oname=$PLUGINSDIR\gateway.json "${STAGE_DIR}\defaults\gateway.json"
    File /oname=$PLUGINSDIR\initialize-config.ps1 "${STAGE_DIR}\initialize-config.ps1"
    nsExec::ExecToStack 'powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$PLUGINSDIR\initialize-config.ps1" -TemplatePath "$PLUGINSDIR\gateway.json" -ConfigPath "$ProgramDataDir\config\gateway.json" -LegacyConfigPath "$LegacyProgramDataDir\config\gateway.json" -EmitToken'
    Pop $0
    Pop $GeneratedToken
    StrCpy $GeneratedToken $GeneratedToken 44
    ${If} $0 != 0
      Call RollbackInstall
      SetErrorLevel 1
      !insertmacro ShowFatal "Could not create the Vision Realtime configuration (PowerShell exit $0)."
      Abort
    ${EndIf}
  config_ready:

  nsExec::ExecToLog '"$INSTDIR\VisionRealtime.exe" install'
  Pop $0
  ${If} $0 != 0
    Call RollbackInstall
    SetErrorLevel 1
    !insertmacro ShowFatal "Service registration failed (WinSW exit $0)."
    Abort
  ${EndIf}
  nsExec::ExecToLog '"$INSTDIR\VisionRealtime.exe" start'
  Pop $0
  ${If} $0 != 0
    Call RollbackInstall
    SetErrorLevel 1
    !insertmacro ShowFatal "The service was installed but could not be started (WinSW exit $0)."
    Abort
  ${EndIf}

  File /oname=$PLUGINSDIR\verify-service.ps1 "${STAGE_DIR}\verify-service.ps1"
  nsExec::ExecToLog 'powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$PLUGINSDIR\verify-service.ps1" -ServiceName "${SERVICE_ID}" -ConfigPath "$ProgramDataDir\config\gateway.json" -ExpectedVersion "${PRODUCT_VERSION}"'
  Pop $0
  ${If} $0 != 0
    Call RollbackInstall
    SetErrorLevel 1
    !insertmacro ShowFatal "The Vision Realtime service failed its health/version check and was unregistered."
    Abort
  ${EndIf}

  Call FinalizeInstall
  WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayName" "${PRODUCT_NAME}"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayVersion" "${PRODUCT_VERSION}"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "Publisher" "${COMPANY}"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\bin\vision-realtime.exe"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKLM "${UNINSTALL_KEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
  WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoRepair" 1
  StrCpy $InstallCommitted "1"
SectionEnd

Function TokenPageCreate
  ${If} $GeneratedToken == ""
    Abort
  ${EndIf}

  !insertmacro MUI_HEADER_TEXT "Vision Realtime authentication token" "Copy this token into the Vision One IEC104 device settings."
  nsDialogs::Create 1018
  Pop $0
  ${If} $0 == error
    Abort
  ${EndIf}

  ${NSD_CreateLabel} 0 0 100% 36u "This token is shown only once. Store it securely and enter it as the authentication token in Vision One.$\r$\nDieser Token wird nur einmal angezeigt. Sicher speichern und in Vision One als Authentifizierungs-Token eintragen."
  Pop $0
  ${NSD_CreateText} 0 45u 100% 14u "$GeneratedToken"
  Pop $TokenField
  SendMessage $TokenField ${EM_SETREADONLY} 1 0
  ${NSD_CreateLabel} 0 70u 100% 28u "Vision Realtime URL: http://127.0.0.1:24104$\r$\nThe token is not written to installer logs."
  Pop $0

  nsDialogs::Show
FunctionEnd

Function un.onInit
  SetRegView 64
  SetShellVarContext all
  StrCpy $ProgramDataDir "$APPDATA\EN-CO OHG\Vision Realtime"
  StrCpy $LegacyProgramDataDir "$APPDATA\EN-CO\Vision One IEC-104 Gateway"
  StrCpy $PurgeProgramData "0"
  ${GetParameters} $0
  ClearErrors
  ${GetOptions} $0 "/PURGE" $1
  ${IfNot} ${Errors}
    StrCpy $PurgeProgramData "1"
  ${EndIf}
FunctionEnd

Function un.PurgePageCreate
  nsDialogs::Create 1018
  Pop $0
  ${If} $0 == error
    Abort
  ${EndIf}
  ${NSD_CreateLabel} 0 0 100% 26u "Configuration, state, and logs are retained by default for later upgrades or reinstalls."
  Pop $0
  ${NSD_CreateCheckbox} 0 34u 100% 16u "Purge all Vision Realtime data from ProgramData"
  Pop $PurgeCheckbox
  ${If} $PurgeProgramData == "1"
    ${NSD_Check} $PurgeCheckbox
  ${EndIf}
  nsDialogs::Show
FunctionEnd

Function un.PurgePageLeave
  ${NSD_GetState} $PurgeCheckbox $0
  ${If} $0 == ${BST_CHECKED}
    StrCpy $PurgeProgramData "1"
  ${Else}
    StrCpy $PurgeProgramData "0"
  ${EndIf}
FunctionEnd

Section "Uninstall"
  IfFileExists "$INSTDIR\VisionRealtime.exe" service_wrapper_found service_wrapper_missing
  service_wrapper_found:
    nsExec::ExecToLog '"$INSTDIR\VisionRealtime.exe" stop'
    Pop $0
    nsExec::ExecToLog '"$INSTDIR\VisionRealtime.exe" uninstall'
    Pop $0
    ${If} $0 != 0
      nsExec::ExecToStack 'sc.exe query "${SERVICE_ID}"'
      Pop $1
      ${If} $1 == 0
        !insertmacro ShowFatal "The Vision Realtime service could not be removed. Uninstall cannot continue safely."
        Abort
      ${EndIf}
    ${EndIf}
    Goto service_removed
  service_wrapper_missing:
    nsExec::ExecToStack 'sc.exe query "${SERVICE_ID}"'
    Pop $0
    ${If} $0 == 0
      nsExec::ExecToLog 'sc.exe stop "${SERVICE_ID}"'
      Pop $1
      nsExec::ExecToLog 'sc.exe delete "${SERVICE_ID}"'
      Pop $1
      ${If} $1 != 0
        !insertmacro ShowFatal "The Vision Realtime service is still registered and could not be removed."
        Abort
      ${EndIf}
  ${EndIf}
  service_removed:

  StrCpy $1 0
  uninstall_service_wait:
    nsExec::ExecToStack 'sc.exe query "${SERVICE_ID}"'
    Pop $0
    ${If} $0 == 0
      IntOp $1 $1 + 1
      ${If} $1 >= 20
        !insertmacro ShowFatal "The Vision Realtime service is still pending removal. Uninstall cannot continue safely."
        Abort
      ${EndIf}
      Sleep 500
      Goto uninstall_service_wait
    ${EndIf}

  SetOutPath "$TEMP"
  StrCpy $1 0
  uninstall_files_retry:
    ClearErrors
    RMDir /r "$INSTDIR"
    ${If} ${Errors}
      IntOp $1 $1 + 1
      ${If} $1 >= 20
        !insertmacro ShowFatal "Not all Vision Realtime program files could be removed from $INSTDIR."
        Abort
      ${EndIf}
      Sleep 500
      Goto uninstall_files_retry
    ${EndIf}
  ${If} $PurgeProgramData == "1"
    ClearErrors
    RMDir /r "$ProgramDataDir"
    ${If} ${Errors}
      !insertmacro ShowFatal "Vision Realtime ProgramData could not be purged completely from $ProgramDataDir."
      Abort
    ${EndIf}
    ClearErrors
    RMDir /r "$LegacyProgramDataDir"
    ${If} ${Errors}
      !insertmacro ShowFatal "Legacy ProgramData could not be purged completely from $LegacyProgramDataDir."
      Abort
    ${EndIf}
  ${EndIf}
  DeleteRegKey HKLM "${UNINSTALL_KEY}"
SectionEnd
