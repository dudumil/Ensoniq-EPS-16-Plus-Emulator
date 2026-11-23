; Windows installer (NSIS 3) for the emulator, with an uninstaller. Made by make-setup.sh, which
; writes product.nsh (names, version, the portable folder to take the files from, the data folders).
;
; Installs, in "Program Files\<product>": the program, the command-line version, roms\ and scsi\
; (made writable for the users: the ROMs, disk images and other files the user adds go there), the
; READMEs and the licences; optionally the VST3 plug-in (Common Files\VST3), Start menu and desktop
; shortcuts. It asks for the user's ROMs (low and high, .bin) and OS disk (.img / .hfe / .ede / .eda) and
; copies them into roms\ (where the program and the VST3 plug-in find them); it does not go on without
; them. Silent install (/S): give them as /ROMLO=<file> /ROMHI=<file> /OS=<file>.
; The uninstaller asks whether to keep the user's files.
Unicode true
ManifestDPIAware true
SetCompressor /SOLID lzma
!include "product.nsh"
!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "nsDialogs.nsh"
!include "FileFunc.nsh"

Name "${PRODUCT}"
Caption "${PRODUCT} ${VERSION} Setup"
OutFile "${OUTFILE}"
InstallDir "$PROGRAMFILES64\${PRODUCT}"
InstallDirRegKey HKLM "${UNINST_KEY}" "InstallLocation"
RequestExecutionLevel admin
BrandingText "${PRODUCT} ${VERSION}"
VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "${PRODUCT}"
VIAddVersionKey "FileDescription" "${PRODUCT} Setup"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "LegalCopyright" "GNU GPL v3"

!define MUI_ICON "${SETUP_ICON}"
!define MUI_UNICON "${ICON}"
!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TEXT "This installs the ${PRODUCT} ${VERSION}: the program and, if you want, the VST3 plug-in.$\r$\n$\r$\nThe ROMs and the operating system are not included: have your own copies ready (the low and high firmware .bin files and the OS disk image). The installer asks for them and puts them in the program's roms folder, where the program and the VST3 plug-in find them.$\r$\n$\r$\nClose any music program that uses the plug-in before you go on."
!define MUI_COMPONENTSPAGE_SMALLDESC
!define MUI_FINISHPAGE_RUN "$INSTDIR\${EXE}.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Open the ${PRODUCT}"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${SRC}\licenses\GPL-3.0.txt"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
Page custom RomsPageCreate RomsPageLeave
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

; ------------------------------------------------------------------ the ROMs and the OS disk
Var RomLo
Var RomHi
Var OsDisk
Var hLo
Var hHi
Var hOs

; $0 = the file, $1 = what it is; Abort (with a message) unless it exists and has ROM_SIZE bytes
Function CheckRom
    ${If} $0 == ""
        MessageBox MB_ICONEXCLAMATION "Choose the $1." /SD IDOK
        Abort
    ${EndIf}
    ClearErrors
    FileOpen $2 "$0" r
    ${If} ${Errors}
        MessageBox MB_ICONEXCLAMATION "Cannot open the $1:$\r$\n$0" /SD IDOK
        Abort
    ${EndIf}
    FileSeek $2 0 END $3
    FileClose $2
    ${If} $3 != ${ROM_SIZE}
        MessageBox MB_ICONEXCLAMATION "The $1 should be a ${ROM_SIZE}-byte .bin file; this one has $3 bytes:$\r$\n$0" /SD IDOK
        Abort
    ${EndIf}
FunctionEnd

Function CheckOs
    ${If} $OsDisk == ""
        MessageBox MB_ICONEXCLAMATION "Choose the operating system disk image." /SD IDOK
        Abort
    ${EndIf}
    ${GetFileExt} "$OsDisk" $1
    ${If} $1 != "img"
    ${AndIf} $1 != "hfe"
    ${AndIf} $1 != "ede"
    ${AndIf} $1 != "eda"
        MessageBox MB_ICONEXCLAMATION "The OS disk should be a .img, .hfe, .ede or .eda disk image:$\r$\n$OsDisk" /SD IDOK
        Abort
    ${EndIf}
    ClearErrors
    FileOpen $2 "$OsDisk" r
    ${If} ${Errors}
        MessageBox MB_ICONEXCLAMATION "Cannot open the OS disk image:$\r$\n$OsDisk" /SD IDOK
        Abort
    ${EndIf}
    FileSeek $2 0 END $3
    FileClose $2
    ${If} $1 == "img"
    ${AndIf} $3 != 819200
    ${AndIf} $3 != 1638400
        MessageBox MB_ICONEXCLAMATION "That .img is not an Ensoniq floppy image (819200 or 1638400 bytes; it has $3):$\r$\n$OsDisk" /SD IDOK
        Abort
    ${EndIf}
    ${If} $3 < 1024
        MessageBox MB_ICONEXCLAMATION "That disk image is too small ($3 bytes):$\r$\n$OsDisk" /SD IDOK
        Abort
    ${EndIf}
FunctionEnd

!macro _OSPREFILL EXT
    ${If} $OsDisk == ""
        FindFirst $5 $6 "$INSTDIR\roms\*.${EXT}"
        ${If} $6 != ""
            StrCpy $OsDisk "$INSTDIR\roms\$6"
        ${EndIf}
        FindClose $5
    ${EndIf}
!macroend

; files already in the roms folder (installing again) are offered as they are
Function PrefillRoms
    ${If} $RomLo == ""
    ${AndIf} ${FileExists} "$INSTDIR\roms\${ROM_LO_NAME}"
        StrCpy $RomLo "$INSTDIR\roms\${ROM_LO_NAME}"
    ${EndIf}
    ${If} $RomHi == ""
    ${AndIf} ${FileExists} "$INSTDIR\roms\${ROM_HI_NAME}"
        StrCpy $RomHi "$INSTDIR\roms\${ROM_HI_NAME}"
    ${EndIf}
    ${If} $OsDisk == ""
        !insertmacro _OSPREFILL img
        !insertmacro _OSPREFILL hfe
        !insertmacro _OSPREFILL ede
        !insertmacro _OSPREFILL eda
    ${EndIf}
FunctionEnd

Function RomsPageCreate
    Call PrefillRoms
    !insertmacro MUI_HEADER_TEXT "ROMs and operating system" "Your own copies of the ${MACHINE} firmware and OS disk (they are not included)."
    nsDialogs::Create 1018
    Pop $0
    ${If} $0 == error
        Abort
    ${EndIf}
    ${NSD_CreateLabel} 0 0 100% 20u "Choose the files. They are copied into the program's roms folder, where the program and the VST3 plug-in find them. The installation cannot go on without them."
    Pop $0
    ${NSD_CreateLabel} 0 26u 100% 10u "${ROM_LO_LABEL}"
    Pop $0
    ${NSD_CreateText} 0 37u 83% 12u "$RomLo"
    Pop $hLo
    ${NSD_CreateBrowseButton} 85% 36u 15% 14u "Browse..."
    Pop $0
    ${NSD_OnClick} $0 BrowseLo
    ${NSD_CreateLabel} 0 58u 100% 10u "${ROM_HI_LABEL}"
    Pop $0
    ${NSD_CreateText} 0 69u 83% 12u "$RomHi"
    Pop $hHi
    ${NSD_CreateBrowseButton} 85% 68u 15% 14u "Browse..."
    Pop $0
    ${NSD_OnClick} $0 BrowseHi
    ${NSD_CreateLabel} 0 90u 100% 10u "${OS_LABEL}"
    Pop $0
    ${NSD_CreateText} 0 101u 83% 12u "$OsDisk"
    Pop $hOs
    ${NSD_CreateBrowseButton} 85% 100u 15% 14u "Browse..."
    Pop $0
    ${NSD_OnClick} $0 BrowseOs
    nsDialogs::Show
FunctionEnd

Function BrowseLo
    ${NSD_GetText} $hLo $1
    nsDialogs::SelectFileDialog open "$1" "ROM files (*.bin)|*.bin|All files (*.*)|*.*"
    Pop $1
    ${If} $1 != ""
        ${NSD_SetText} $hLo "$1"
    ${EndIf}
FunctionEnd

Function BrowseHi
    ${NSD_GetText} $hHi $1
    nsDialogs::SelectFileDialog open "$1" "ROM files (*.bin)|*.bin|All files (*.*)|*.*"
    Pop $1
    ${If} $1 != ""
        ${NSD_SetText} $hHi "$1"
    ${EndIf}
FunctionEnd

Function BrowseOs
    ${NSD_GetText} $hOs $1
    nsDialogs::SelectFileDialog open "$1" "Disk images (*.img;*.hfe;*.ede;*.eda)|*.img;*.hfe;*.ede;*.eda|All files (*.*)|*.*"
    Pop $1
    ${If} $1 != ""
        ${NSD_SetText} $hOs "$1"
    ${EndIf}
FunctionEnd

Function RomsPageLeave
    ${NSD_GetText} $hLo $RomLo
    ${NSD_GetText} $hHi $RomHi
    ${NSD_GetText} $hOs $OsDisk
    StrCpy $0 $RomLo
    StrCpy $1 "low firmware file"
    Call CheckRom
    StrCpy $0 $RomHi
    StrCpy $1 "high firmware file"
    Call CheckRom
    ${If} $RomLo == $RomHi
        MessageBox MB_ICONEXCLAMATION "The low and the high firmware must be two different files."
        Abort
    ${EndIf}
    Call CheckOs
FunctionEnd

; ------------------------------------------------------------------ sections
Section "${PRODUCT} (program)" SecApp
    SectionIn RO
    SetOutPath "$INSTDIR"
    File "${SRC}\${EXE}.exe"
    File "${SRC}\${EXE}-cli.exe"
    File "/oname=README.txt" "${SRC}\README.txt"
    SetOutPath "$INSTDIR\licenses"
    File "${SRC}\licenses\*.*"
    File "${NSIS_LICENSE}"                  ; (the uninstaller is made with NSIS)
    SetOutPath "$INSTDIR\roms"
    File "${SRC}\roms\README.txt"
    ; the user's ROMs and OS disk
    ${If} $RomLo != "$INSTDIR\roms\${ROM_LO_NAME}"
        CopyFiles /SILENT "$RomLo" "$INSTDIR\roms\${ROM_LO_NAME}"
    ${EndIf}
    ${If} $RomHi != "$INSTDIR\roms\${ROM_HI_NAME}"
        CopyFiles /SILENT "$RomHi" "$INSTDIR\roms\${ROM_HI_NAME}"
    ${EndIf}
    ${GetFileName} "$OsDisk" $0
    ${If} $OsDisk != "$INSTDIR\roms\$0"
        CopyFiles /SILENT "$OsDisk" "$INSTDIR\roms\$0"
    ${EndIf}
    ${IfNot} ${FileExists} "$INSTDIR\roms\${ROM_LO_NAME}"
    ${OrIfNot} ${FileExists} "$INSTDIR\roms\${ROM_HI_NAME}"
    ${OrIfNot} ${FileExists} "$INSTDIR\roms\$0"
        MessageBox MB_ICONSTOP "The ROMs or the OS disk could not be copied into $INSTDIR\roms." /SD IDOK
        Abort
    ${EndIf}
    SetOutPath "$INSTDIR\scsi"
    File "${SRC}\scsi\README.txt"
    SetOutPath "$INSTDIR"
    ; the folders the user writes to (ROMs, disk images, the FB-2 file...) are writable by the users
    !insertmacro DATA_DIRS_CREATE
    WriteUninstaller "$INSTDIR\Uninstall.exe"
    WriteRegStr HKLM "${UNINST_KEY}" "DisplayName" "${PRODUCT}"
    WriteRegStr HKLM "${UNINST_KEY}" "DisplayVersion" "${VERSION}"
    WriteRegStr HKLM "${UNINST_KEY}" "Publisher" "${COMPANY}"
    WriteRegStr HKLM "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\${EXE}.exe"
    WriteRegStr HKLM "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKLM "${UNINST_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
    WriteRegStr HKLM "${UNINST_KEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
    WriteRegDWORD HKLM "${UNINST_KEY}" "NoModify" 1
    WriteRegDWORD HKLM "${UNINST_KEY}" "NoRepair" 1
    WriteRegDWORD HKLM "${UNINST_KEY}" "EstimatedSize" ${SIZE_KB}
SectionEnd

Section "VST3 plug-in" SecVst
    SetOutPath "$COMMONFILES64\VST3\${SHORT}.vst3"
    File /r "${SRC}\VST3\${SHORT}.vst3\*.*"
    WriteRegDWORD HKLM "${UNINST_KEY}" "VST3" 1
SectionEnd

Section "Start menu shortcuts" SecMenu
    SetShellVarContext all
    CreateDirectory "$SMPROGRAMS\${PRODUCT}"
    CreateShortCut "$SMPROGRAMS\${PRODUCT}\${PRODUCT}.lnk" "$INSTDIR\${EXE}.exe"
    CreateShortCut "$SMPROGRAMS\${PRODUCT}\ROMs folder.lnk" "$INSTDIR\roms"
    CreateShortCut "$SMPROGRAMS\${PRODUCT}\SCSI drives folder.lnk" "$INSTDIR\scsi"
    CreateShortCut "$SMPROGRAMS\${PRODUCT}\README.lnk" "$INSTDIR\README.txt"
    CreateShortCut "$SMPROGRAMS\${PRODUCT}\Uninstall ${PRODUCT}.lnk" "$INSTDIR\Uninstall.exe"
SectionEnd

Section "Desktop shortcut" SecDesk
    SetShellVarContext all
    CreateShortCut "$DESKTOP\${PRODUCT}.lnk" "$INSTDIR\${EXE}.exe"
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SecApp} "The program, its command-line version, the roms and scsi folders, the READMEs and the licences."
    !insertmacro MUI_DESCRIPTION_TEXT ${SecVst} "The VST3 instrument, in Common Files\VST3 (where VST3 hosts look for plug-ins)."
    !insertmacro MUI_DESCRIPTION_TEXT ${SecMenu} "A Start menu folder with the program, the ROMs and SCSI folders, the README and the uninstaller."
    !insertmacro MUI_DESCRIPTION_TEXT ${SecDesk} "A shortcut to the program on the desktop."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Function .onInit
    SetRegView 64
    ; an earlier installation in another folder: install over it
    ReadRegStr $0 HKLM "${UNINST_KEY}" "InstallLocation"
    ${If} $0 != ""
        StrCpy $INSTDIR $0
    ${EndIf}
    ; silent install: the ROMs and the OS disk come from the command line, and are checked the same way
    ${GetParameters} $9
    ClearErrors
    ${GetOptions} $9 "/ROMLO=" $RomLo
    ${GetOptions} $9 "/ROMHI=" $RomHi
    ${GetOptions} $9 "/OS=" $OsDisk
    ${If} ${Silent}
        Call PrefillRoms
        StrCpy $0 $RomLo
        StrCpy $1 "low firmware file"
        Call CheckRom
        StrCpy $0 $RomHi
        StrCpy $1 "high firmware file"
        Call CheckRom
        Call CheckOs
    ${EndIf}
FunctionEnd

; ------------------------------------------------------------------ uninstaller
Var KeepFiles

Function un.onInit
    SetRegView 64
    StrCpy $KeepFiles 1
    IfSilent done
    MessageBox MB_YESNOCANCEL|MB_ICONQUESTION "Keep your files?$\r$\n$\r$\nYes: the ROMs, disk images and other files you put in the program's roms and scsi folders (and the FB-2 contents) stay, with your settings.$\r$\nNo: they are deleted too." IDYES done IDNO del
    Abort
  del:
    StrCpy $KeepFiles 0
  done:
FunctionEnd

Section "Uninstall"
    SetShellVarContext all
    Delete "$INSTDIR\${EXE}.exe"
    Delete "$INSTDIR\${EXE}-cli.exe"
    Delete "$INSTDIR\README.txt"
    RMDir /r "$INSTDIR\licenses"
    ReadRegDWORD $0 HKLM "${UNINST_KEY}" "VST3"
    ${If} $0 == 1
        RMDir /r "$COMMONFILES64\VST3\${SHORT}.vst3"
    ${EndIf}
    RMDir /r "$SMPROGRAMS\${PRODUCT}"
    Delete "$DESKTOP\${PRODUCT}.lnk"
    ${If} $KeepFiles == 0
        !insertmacro DATA_DIRS_REMOVE
        SetShellVarContext current
        RMDir /r "$APPDATA\Ensoniq\${CFGDIR}"
        RMDir "$APPDATA\Ensoniq"
        SetShellVarContext all
    ${Else}
        ; only the READMEs the installer put there
        Delete "$INSTDIR\roms\README.txt"
        Delete "$INSTDIR\scsi\README.txt"
        !insertmacro DATA_DIRS_RMDIR
    ${EndIf}
    Delete "$INSTDIR\Uninstall.exe"
    RMDir "$INSTDIR"
    DeleteRegKey HKLM "${UNINST_KEY}"
SectionEnd
