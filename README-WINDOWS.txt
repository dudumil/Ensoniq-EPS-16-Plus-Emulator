ENSONIQ EPS-16 PLUS (RACK) EMULATOR FOR WINDOWS
===============================================

Requires Windows 10 or 11 (64-bit). No installation and no DLLs needed.

1. Copy into the "roms" folder (next to eps16.exe):
     eps16plus-u28.bin
     eps16plus-u27.bin
     os130.img            (OS 1.30 floppy disk image)

2. Double-click eps16.exe. With no other disk it uses roms\os130.img
   (or, if that is missing, the first .img in the roms folder).
   You see "ENSONIQ EPS-16 PLUS", "LOADING SYSTEM" and then "NO INSTRUMENTS".

LOADING AN INSTRUMENT
   Insert the disk (drag the .img onto the window), press LOAD, choose the
   file with the arrows, ENTER/YES and then an instrument button (1-8).

MIDI CHANNELS - IMPORTANT
   os130.img has the global parameters saved in MIDI MULTI mode, with
   instruments 1-8 on MIDI channels 9-16. Therefore:
   - the on-screen keyboard plays on the "Keyboard channel" (View > Options,
     default 9 = instrument 1; 10 = instrument 2, etc.);
   - "MIDI In -> channel" = "the on-screen keyboard one" (default) sends
     everything coming from your MIDI keyboard to that same channel, whatever
     channel it plays on. Choose "as received (all)" to use MULTI mode for real
     (e.g. a sequencer with 8 tracks on channels 9-16).
   To play the selected instrument on channel 1: Edit > System-MIDI,
   MIDI IN MODE=POLY (and Command > System-MIDI > SAVE GLOBAL PARAMETERS).

SEQUENCER
   1. Load an instrument and SELECT it by pressing its button (1-8): the
      display shows e.g. "CLASSIC PAD VOLUME=99". With no track selected,
      RECORD does nothing.
   2. SEQ, then RECORD and PLAY: on screen RECORD latches with one click
      (click again to release it); on the PC keyboard, Space = PLAY.
      The EPS creates SEQUENCE 01 and records (REC blinks until you play the
      first note).
   3. STOP asks "KEEP TRACK?": ENTER/YES keeps it. PLAY plays the sequence.
   Tempo (TEMPO=100), click, count-in etc. are in Edit > Seq.

SAMPLING
   Choose the audio input in View > Options and switch it on with Ctrl+I.
   Press SAMPLE, then the instrument button: the display shows the input level
   (bar). The arrows show SAMPLE RATE (44.6/29.8/22.3...), FILTER CUTOFF,
   PRE TRIGGER, INPUT LEVEL and SAMPLE TIME. ENTER/YES starts recording (REC
   lit), ENTER again stops; then the EPS asks for the root key (play a MIDI note).

WINDOW MENUS
   File: Open Disk Image (choose an .img), Eject Disk (Ctrl+E), Save Disk
         Changes (Ctrl+S), Reset (Ctrl+R), Exit.
   View: Show Keyboard (F9) shows/hides the on-screen keyboard; Options
         (Ctrl+O) opens the audio and MIDI options.
   Messages (disk inserted, ejected...) appear for a few seconds at the bottom;
   the window title shows the disk in the drive, with "*" if it has unsaved
   changes.

AUDIO AND MIDI (View > Options or Ctrl+O)
   Each field opens a list with all the choices (click, or Enter);
   Arrows/Tab move between fields, A applies, Esc closes.
   Driver (WASAPI, DirectSound, ASIO or SDL), output, sample rate and buffer.
   Main and the OEX-6 expander outputs (AUX 1, BUS 2, BUS 3) as pairs of
   sound card outputs.
   AUX 1: send an instrument there with Edit > Amp (arrows to OUT=), OUT=AUX1.
   No effects program reads this bus, so on an EPS without the expander it is
   not heard; here, "mixed into Main (dry)" (default) adds it, without effects,
   to the main outputs, "off" behaves like the real EPS, and an output pair
   sends it only there. If the sound card lacks those outputs, it goes back to
   Main.
   BUS 2 and BUS 3 are the internal effects buses: they reach Main through the
   ESP, as on the real EPS (which instruments use them is set with the OS);
   the OEX-6 outputs can also send them to a chosen pair of outputs.
   Sampler input: device and ONE channel (the EPS-16 Plus is mono).
   MIDI In / MIDI Out, keyboard channel and MIDI In routing.
   APPLY saves to %APPDATA%\Ensoniq\EPS16emu\eps16.cfg.
   If there are audio dropouts (underruns), increase the buffer.

FLOPPY DISKS
   Drag an .img file onto the window (= inserting the disk).
   Single instruments (.efe): drag one or several onto the window; they are put
   on a temporary disk that goes into the drive. Then LOAD as usual.
   Ctrl+E ejects, Ctrl+S saves the changes to the image (also on eject/exit).

SCSI
   Put images in the "scsi" folder (HD0.img = disk on ID 0, CD5.iso = CD on
   ID 5) or drag them onto the window. To use them: Command > System-MIDI >
   CHANGE STORAGE DEVICE (up arrow chooses SCSI 0...). An empty file is
   formatted with FORMAT SCSI DRIVE. Saves go straight to the image.

BOOTING FROM A SCSI DRIVE
   View > Options > SCSI DRIVES > Boot from: "SCSI drive (no floppy at start)".
   The EPS-16 Plus then starts with an empty floppy drive and boots from a
   SCSI drive that has an OS on it (e.g. HD0.img in the "scsi" folder); with the
   stock ROM and no such drive the display shows PLEASE INSERT DISK. APPLY restarts
   the EPS-16 Plus at once. "Floppy: the OS disk image" goes back to the usual way.
   Command line (this run only):  eps16.exe --boot scsi   or   --boot floppy
   The OS disk image (os130.img) is looked for ONLY in the "roms" folder next to
   the program: a copy left in %APPDATA%\Ensoniq\EPS16emu is not used, so
   deleting os130.img from roms really removes it.

MIRAGE DISKS
   Ensoniq Mirage disk images (440 KB .img) can be dropped on the window like
   any floppy. Command > System/MIDI > LOAD MIRAGE-DSK SOUND: the OS first asks
   for the OS disk (keep os130.img in the drive and press Enter), then for the
   sound and for the Mirage disk: drop the Mirage image and press Enter.

DISK IMAGES
   Besides raw .img images, Giebler .EDE (800 KB) and .EDA (1600 KB) disk images
   can go in the drive: drop them on the window, or File > Open Disk Image.
   Changes are written back in the same format. In the VST3 plug-in, click the
   drive slot to choose a file (or drop it on the panel, if the host allows).

RECORD A SONG TO WAV
   File > Record Song to WAV (Ctrl+W): choose a folder. The recording waits,
   starts when you press Play on the sequencer and ends when the song ends or
   you press Stop (it goes on until the sound has died away). The 24-bit WAV
   gets the name of the song / sequence shown on the Seq/Song page (open that
   page once so that the name is known). Ctrl+W again cancels or stops.

ROMS NOT FOUND
   If the ROMs are not found, eps16.exe lists the folders it searched and offers
   "Choose folder...": pick the folder with the ROM files; it is remembered
   (the OS disk image is looked for there too, and the VST3 plug-in uses it).

SCSI ACTIVITY LED
   A red LED above the floppy drive blinks while the SCSI bus is in use: slowly
   while reading, fast while writing (loading, saving).

SCSI DRIVES FOLDER
   View > Options > SCSI DRIVES > Drive images folder > Choose folder...
   In the folder: HD0.img = disk on ID 0, CD5.iso = CD-ROM on ID 5, etc.
   If none is chosen, the "scsi" folder next to eps16.exe is used. After
   changing it, File > Reset so that the OS sees the new drives.
   New disk: choose "New drive size" (50 MB to 2 GB) and press
   "New drive  Create HDn.img". It is already formatted, on the first free ID.

COMPUTER KEYBOARD (the PC keyboard does not play notes: use a MIDI keyboard
or the on-screen keyboard)
   1-8 (and F1-F8) = instrument buttons 1-8
   numeric keypad = page buttons as printed: 1 Env 1, 2 Env 2, 3 Env 3,
     4 Pitch, 5 Filter, 6 Amp, 7 LFO, 8 Wave, 9 Layer, 0 Track
   R = RECORD (hold it and press Space to record),  Space = PLAY
   arrows = panel arrows,  Enter = YES/ENTER,  Esc/Backspace = NO/CANCEL
   up + down together = the value jumps to the middle of the range (0 in
     -99..+99, 50 in 0..99), as on the hardware. With the mouse: right-click
     one of the arrows.
   F11/F12 = Patch Select left/right (you can press both)
   F9 = hide/show the on-screen keyboard (View > Show Keyboard)
   Ctrl+F effects (ESP) on/off,  Ctrl+I input on/off,  Ctrl+R reset
   With the mouse: click the buttons and keys (lower = louder), drag Volume,
   Data Entry and the wheels (MIDI pitch bend and modulation).
   Hold Power for ~1 s to restart; the drive button ejects.

COMMAND LINE OPTIONS
   eps16.exe --list-audio        list audio drivers and devices
   eps16.exe --list-midi         list MIDI ports
   eps16.exe --mem 1             only 1 MB of sound memory (default 2 MB)
   eps16.exe --no-scsi           no SCSI option (boot is ~3 s faster)
   eps16.exe other.img           boot with another disk
   eps16.exe --real-disk-speed   floppy at real speed (fast by default)
   eps16-cli.exe                 version without a window (tests, WAV recording)

KNOWN LIMITATIONS
   - The pedals (SUSTAIN FT SW / AUX FT SW, on the rack's back panel) have no
     shortcut yet.
   - The floppy is fast: a 195 KB instrument loads in ~1.4 s (~7 s on the real
     EPS). Booting takes ~3.5 s (~6 s with SCSI: the ROM waits for the SCSI
     disks). Formatting a floppy takes ~15 s.

VST3 PLUG-IN (VST3 folder)
   1. Copy the whole VST3\EPS16.vst3 folder to
        C:\Program Files\Common Files\VST3
   2. Open eps16.exe once: it records its folder and the plug-in then uses the
      same files (ROMs and os130.img from the roms folder, scsi folder).
   3. In the host, rescan plug-ins and open "EPS-16 Plus Emulator".
   - Outputs Main + AUX 1 / BUS 2 / BUS 3 (enable them in the host to get them
     separately).
   - The plug-in uses the app's settings (SCSI folder, MIDI channel, BUS 2/3...):
     to change them, open eps16.exe, change them in Options and the plug-in
     follows. By default host notes go to channel 9 (instrument 1).
   - Panel only: drag .img or .efe files onto the panel; the drive's eject
     button ejects; holding Power ~1 s resets.

The EPS-16 Plus ROM and operating system are not included.
Component licenses are in the "licenses" folder. ASIO support uses the
Steinberg ASIO SDK (available under GPLv3), so this program is GPLv3.
ASIO and VST are registered trademarks of Steinberg Media Technologies GmbH.
The plug-in uses the VST3 SDK/VSTGUI (MIT license) and SDL2 (zlib license).
