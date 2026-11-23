%PRODUCT% %VERSION% - WINDOWS INSTALLER
%LINE%

Windows 10 or 11, 64-bit.

BEFORE YOU START
   The %MACHINE% ROMs and operating system are NOT included. Have your own copies ready:
     - the low firmware  (%ROM_LO%)
     - the high firmware (%ROM_HI%)
     - the operating system disk image: .img, .hfe, .ede or .eda
   The installer asks for them and does not go on without them.

INSTALLING
   1. Run %ZIPBASE%-Windows-Setup.exe.
      Windows SmartScreen may say that it protected your PC: the installer is not signed with a
      code-signing certificate. Click "More info", then "Run anyway".
   2. Choose the parts to install: the program (always), the VST3 plug-in, Start menu shortcuts and
      a desktop shortcut.
   3. Choose the ROMs and the OS disk when asked. They are copied into the program's roms folder,
      where the program and the VST3 plug-in find them.
   Installed in:
     C:\Program Files\%PRODUCT%                the program, its roms and scsi folders, README.txt
     C:\Program Files\Common Files\VST3\%SHORT%.vst3     the VST3 plug-in
   The roms and scsi folders can be written to without administrator rights (SCSI disk images go in
   scsi: HD0.img = hard disk on SCSI ID 0, CD5.iso = CD-ROM on ID 5...).

USING IT
   Open the %PRODUCT% from the desktop or the Start menu. In a music program, rescan the
   plug-ins and load "%PRODUCT%" as an instrument (VST3). The full manual is README.txt in the
   program folder (Start menu > %PRODUCT% > README).

UNINSTALLING
   Settings > Apps > %PRODUCT% > Uninstall, or the Start menu's "Uninstall %PRODUCT%". It asks
   whether to keep your files (ROMs, disk images, settings).

NO INSTALLER?
   %ZIPBASE%-Windows-Portable.zip has the same program in a folder to unzip anywhere.

LICENCES
   The %PRODUCT% is free software under the GNU GPL v3 (licenses\GPL-3.0.txt). The licences of the
   components it uses, and of NSIS, which makes the installer, are in the licenses folder.
   Source code: %REPO_URL%
   Ensoniq and %MACHINE% are trademarks of their owners; this project is not affiliated with them.
   VST and ASIO are trademarks of Steinberg Media Technologies GmbH.
