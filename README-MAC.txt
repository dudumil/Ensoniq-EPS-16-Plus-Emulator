ENSONIQ EPS-16 PLUS (RACK) EMULATOR FOR macOS (Apple Silicon and Intel) - APP AND VST3 PLUG-IN
==============================================================================================

This file covers the ready-made package (EPS16-Plus-Emulator-macOS.zip) and building from the source code.
The app and the plug-in are universal: they run on Apple Silicon and on Intel Macs (macOS 11 or later).

OPTION A - ready-made package (no tools to install)
   1. Get EPS16-Plus-Emulator-macOS.zip: from the release page, or from GitHub Actions
      (https://github.com/dudumil/Ensoniq-EPS-16-Plus-Emulator/actions > "macOS" workflow > latest green run > "Artifacts" at the bottom:
      "EPS16-macOS" holds EPS16-Plus-Emulator-macOS.zip).
   2. Unzip it. The folder EPS16-macOS contains:
         EPS-16-Plus.app        the app
         roms/                  put the ROMs and the OS disk image here
         scsi/                  SCSI disk / CD-ROM images (ZuluSCSI names: HD0.img, CD5.iso, ...)
         VST3/EPS16.vst3        the plug-in
         README.txt, README.md, licenses/
      Keep the app, roms/ and scsi/ together in this folder (it can go anywhere, e.g. in Applications
      or in Documents): the app looks for roms/ and scsi/ next to itself.
   3. Remove the "downloaded from the internet" flag once (Terminal; drag the folder onto the window
      after typing the command and a space to fill in the path):
         xattr -dr com.apple.quarantine EPS16-macOS
      Without it macOS refuses to open the app ("cannot verify ..."), or runs it from a hidden copy
      where roms/ and scsi/ are not seen. (Instead you can open it once and choose
      System Settings > Privacy & Security > "Open Anyway", but xattr also covers the plug-in.)
   4. Put the ROM files (eps16plus-u28.bin and eps16plus-u27.bin) and os130.img in roms/, then open the app.
      If the ROMs are still not found, the app lists the folders it searched and offers
      "Choose folder...": pick the folder with the ROMs; it is remembered.
   5. Plug-in: copy VST3/EPS16.vst3 to ~/Library/Audio/Plug-Ins/VST3/ (Finder: Go > Go to Folder...).
      Open the app once first: the plug-in then uses the same ROMs, OS disk and scsi folder.
   Skip to section 4 below.

OPTION B - build it yourself on your Mac (sections 1 to 3).
   If "xcode-select --install" says the software is not available on the server, install the
   tools another way: download "Command Line Tools for Xcode" from
   https://developer.apple.com/download/all/ (free Apple ID), or install Xcode from the App Store;
   then, if needed,  sudo xcode-select --reset

1. Prepare (once)
   - Install Apple's Command Line Tools: open Terminal and type
         xcode-select --install
   - Install CMake: with Homebrew,  brew install cmake
     (or download the .dmg from https://cmake.org/download/ and copy CMake to Applications).

2. ROMs and OS disk
   Copy the EPS-16 Plus ROMs (eps16plus-u28.bin and eps16plus-u27.bin) and the OS floppy disk
   image (os130.img) into the "roms" folder (next to this file). They are bundled into the app
   and the plug-in.
   (The ROMs can alternatively go in ~/Library/Application Support/Ensoniq/EPS16emu/roms; the OS disk
   image is only taken from the "roms" folder, never from there.)

3. Build
   In Terminal, inside this folder:
         ./build-mac.sh
   It takes a few minutes the first time (it downloads the VST3 SDK and SDL2).
   When it finishes:
     dist/EPS-16-Plus.app -> drag it to Applications
     dist/eps16-cli       -> command line tool
     dist/EPS16-Plus-Emulator-macOS.zip -> the package of option A (the app, VST3/, roms/, scsi/, READMEs, licenses)
     the plug-in is installed in ~/Library/Audio/Plug-Ins/VST3/EPS16.vst3
   No Mac at hand? Option A: the GitHub "macOS" workflow builds and checks EPS16-Plus-Emulator-macOS.zip.

4. The app
   - The first time you use the audio input, macOS asks for microphone permission.
   - Floppy disks and SCSI disks are dragged onto the window; single instruments (.efe)
     too (they go on a temporary floppy). The OS disk is copied to
     ~/Library/Application Support/Ensoniq/EPS16emu and that copy is the one used/saved.
   - SCSI disks: by default the "scsi" folder inside that same folder (names HD0.img,
     CD5.iso, ...), or any image larger than 2 MB dragged onto the window. Another folder and
     new formatted disks (50 MB to 2 GB): View > Options > SCSI drives.
   - Boot from SCSI: Options > SCSI drives > Boot from > "SCSI drive (no floppy at start)" starts
     with an empty floppy drive and boots from a SCSI drive that has an OS on it. A red LED above
     the floppy drive blinks on SCSI activity (slowly while reading, fast while writing).
   - MIDI: the supplied OS disk is set to MIDI MULTI (instruments 1-8 on channels 9-16); the
     on-screen keyboard plays on the "Keyboard channel" (Options, default 9).

5. The VST3 plug-in (instrument)
   - It appears as "EPS-16 Plus Emulator" in any VST3 host (Cubase, Studio One, Reaper, Bitwig,
     Ableton Live 11+...). Logic does not use VST3.
   - Outputs: Main + AUX 1 / BUS 2 / BUS 3. Enable them in the host to get them separately.
   - Stereo input for sampling.
   - MIDI In and MIDI Out (the EPS's sequencer can play other instruments).
   - Panel only: drag disk images or .efe files onto the panel; the drive's eject button
     ejects; holding Power ~1 s resets.
   - The plug-in uses the app's settings and files (ROMs, SCSI folder...): to change them,
     open the app, change them in Options and the plug-in follows.

If macOS says the app "cannot be opened": right-click it > Open.

MENUS (Mac menu bar)
   File: Open Disk Image (Cmd+O), Eject Disk (Cmd+E), Save Disk Changes (Cmd+S),
         Reset (Cmd+R).
   View: Show Keyboard (Cmd+K) and Options (Cmd+,).

The EPS-16 Plus ROM and operating system are not included.
