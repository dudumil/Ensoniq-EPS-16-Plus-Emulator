ENSONIQ EPS-16 PLUS (RACK) EMULATOR FOR macOS (Apple Silicon) - APP AND VST3 PLUG-IN
=====================================================================================

This package contains the source code; build it once on your Mac.

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
     the plug-in is installed in ~/Library/Audio/Plug-Ins/VST3/EPS16.vst3
   No Mac at hand? On GitHub, the "macOS" workflow (Actions tab > macOS > Run workflow) builds
   the app and the plug-in on a Mac runner; they are under "Artifacts" on the run's page.

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
     the floppy drive blinks on SCSI activity.
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
