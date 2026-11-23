%PRODUCT% %VERSION% - macOS INSTALLER
%LINE%

macOS 11 or later, Apple Silicon or Intel.

BEFORE YOU START
   The %MACHINE% ROMs and operating system are NOT included. Have your own copies ready:
     - the low firmware  (%ROM_LO%)
     - the high firmware (%ROM_HI%)
     - the operating system disk image: .img, .hfe, .ede or .eda
   The installer asks for them and does not go on without them.

INSTALLING
   1. Open %ZIPBASE%-macOS-Installer.dmg, then "Install %PRODUCT%".
      The installer is not signed by Apple: if macOS refuses to open it, right-click it > Open
      (or System Settings > Privacy & Security > "Open Anyway").
   2. Choose the ROMs and the OS disk when asked. They go in Applications > %PRODUCT% > roms, where
      the app and the plug-ins find them.
   3. "Customize" lets you leave out the VST3 plug-in or the Audio Unit.
   Installed in:
     Applications > %PRODUCT%                            the app, roms, scsi, README, uninstaller
     /Library/Audio/Plug-Ins/VST3/%SHORT%.vst3            the VST3 plug-in
     /Library/Audio/Plug-Ins/Components/%SHORT%.component the Audio Unit (Logic Pro, GarageBand)

USING IT
   Open the app in Applications > %PRODUCT%. In a music program, rescan the plug-ins and load
   "%PRODUCT%" as an instrument (Logic Pro: Instrument > AU Instruments > %SHORT%emu). The full
   manual is README.txt in the same folder.

UNINSTALLING
   Open "Uninstall %PRODUCT%" in Applications > %PRODUCT%. It can keep your files (ROMs, disk
   images, settings).

NO INSTALLER?
   %ZIPBASE%-macOS-Portable.zip has the same app and plug-ins in a folder to unzip anywhere.

LICENCES
   The %PRODUCT% is free software under the GNU GPL v3 (licenses/GPL-3.0.txt). The licences of the
   components it uses are in the licenses folder. Source code: %REPO_URL%
   Ensoniq and %MACHINE% are trademarks of their owners; this project is not affiliated with them.
   VST is a trademark of Steinberg Media Technologies GmbH. Audio Units, Logic Pro and GarageBand
   are trademarks of Apple Inc.
