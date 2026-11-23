; Windows installer (NSIS 3) for the emulator, with an uninstaller. Made by make-setup.sh, which
; writes product.nsh (names, version, the portable folder to take the files from, the data folders).
;
; Installs, in "Program Files\<product>": the program, the command-line version, roms\ and scsi\
; (made writable for the users: the ROMs, disk images and other files the user adds go there), the
; READMEs and the licences; optionally the VST3 plug-in (Common Files\VST3), Start menu and desktop
; shortcuts. The uninstaller asks whether to keep the user's files.
Unicode true
ManifestDPIAware true
SetCompressor /SOLID lzma
!include "product.nsh"
!include "MUI2.nsh"
!include "LogicLib.nsh"

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

!define MUI_ICON "${ICON}"
!define MUI_UNICON "${ICON}"
!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TEXT "This installs the ${PRODUCT} ${VERSION}: the program and, if you want, the VST3 plug-in.$\r$\n$\r$\nThe ROMs and the operating system are not included: after installing, copy them into the roms folder of the program (the Start menu has a shortcut to it), then open the program once so that the plug-in finds the same files.$\r$\n$\r$\nClose any music program that uses the plug-in before you go on."
!define MUI_COMPONENTSPAGE_SMALLDESC
!define MUI_FINISHPAGE_RUN "$INSTDIR\${EXE}.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Open the ${PRODUCT}"
!define MUI_FINISHPAGE_SHOWREADME "$INSTDIR\roms"
!define MUI_FINISHPAGE_SHOWREADME_TEXT "Open the roms folder (to copy the ROMs and OS disk into)"
!define MUI_FINISHPAGE_SHOWREADME_NOTCHECKED

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${SRC}\licenses\GPL-3.0.txt"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

; ------------------------------------------------------------------ sections
Section "${PRODUCT} (program)" SecApp
    SectionIn RO
    SetOutPath "$INSTDIR"
    File "${SRC}\${EXE}.exe"
    File "${SRC}\${EXE}-cli.exe"
    File "/oname=README.txt" "${SRC}\README.txt"
    SetOutPath "$INSTDIR\licenses"
    File "${SRC}\licenses\*.*"
    SetOutPath "$INSTDIR\roms"
    File "${SRC}\roms\README.txt"
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

Section /o "Desktop shortcut" SecDesk
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
