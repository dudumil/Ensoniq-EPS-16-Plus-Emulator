# Ensoniq EPS-16 Plus Emulator

![The emulator starting up: ENSONIQ EPS-16 PLUS on the display](docs/screenshot.png)

A hardware-level emulator of the **Ensoniq EPS-16 Plus** (rack) sampler. It runs the original ROM and
boots the original operating system from a floppy image, on an emulated 68000 CPU, ES5505 (OTIS)
sound chip, ES5510 (ESP) effects DSP, DMA controller, floppy controller and SCSI. Nothing of the OS is
faked, so it behaves like the real instrument.

Available as an app for **Windows** and **macOS** (Apple Silicon and Intel), and as a **VST3 instrument plug-in**
for both.

## What works

- Boot from the OS floppy (tested with OS 1.30), loading and saving instruments, banks and sequences.
- 20-voice sound engine with the ESP effects.
- Sampling from your audio interface (or a WAV file) at 44.6 / 29.8 / 22.3 / 17.9 kHz, with the input
  level meter.
- Sequencer with correct tempo, MIDI In/Out.
- SCSI option (ZuluSCSI-style images): detection at boot, CHANGE STORAGE DEVICE, FORMAT SCSI DRIVE.
- **FB-2 Flash Bank** (1 MB of flash for the OS and instruments): format, save, instant loads, boot
  from the flash.
- Import of **Ensoniq Mirage** sounds from Mirage disk images (440 KB `.img`): Command › System/MIDI ›
  LOAD MIRAGE-DSK SOUND.
- OEX-6 Output Expander outputs (AUX 1, BUS 2, BUS 3) on separate outputs of your audio interface.
- Patch Select buttons, Volume and Data Entry sliders, the full rack front panel and an on-screen
  keyboard.

## Download

Get the latest release from the [Releases](../../releases) page. Each system has an **installer**
(with an uninstaller) and a **portable** package (a folder to unzip anywhere, nothing installed).
The installer zips hold the installer (`Setup.exe` / `.dmg`), a README and the licences.

| | Installer | Portable |
|---|---|---|
| **Windows** (64-bit) | `EPS16-Plus-Emulator-Windows-Setup.zip` | `EPS16-Plus-Emulator-Windows-Portable.zip` |
| **macOS** (Apple Silicon and Intel, 11 or later) | `EPS16-Plus-Emulator-macOS-Installer.zip` | `EPS16-Plus-Emulator-macOS-Portable.zip` |

Both installers ask for **your own ROMs and OS disk**: the low and high firmware (`.bin`) and the
operating system disk image (`.img`, `.hfe`, `.ede` or `.eda`). They copy them into the program's `roms`
folder, where the app and the plug-ins find them, and do not go on without them.

**Windows installer:** the program goes in `C:\Program Files\EPS-16 Plus Emulator` (with its `roms` and
`scsi` folders, which the installer makes writable for you), the VST3 plug-in in
`C:\Program Files\Common Files\VST3` (optional), Start menu shortcuts and a desktop shortcut.
Remove it with Settings › Apps or the Start menu's *Uninstall EPS-16 Plus Emulator*: it asks whether to
keep your ROMs, disk images and settings.

**macOS installer:** the app goes in Applications › **EPS-16 Plus Emulator** (with its `roms` and `scsi`
folders and an uninstaller), the VST3 plug-in in `/Library/Audio/Plug-Ins/VST3` and the **Audio Unit**
(Logic Pro, GarageBand) in `/Library/Audio/Plug-Ins/Components`; *Customize* lets you leave the plug-ins
out. It comes in a disk image (`.dmg`): open it, then *Install {prod}*. The installer is not signed by Apple: if macOS refuses to open it, right-click it › **Open** (or System
Settings › Privacy & Security › **Open Anyway**). To remove everything, open *Uninstall EPS-16 Plus
Emulator* in the same folder (it can keep your ROMs, disk images and settings).

**Portable packages:**

| File | What it is |
|---|---|
| `eps16.exe` / `EPS-16-Plus.app` | the emulator |
| `eps16-cli(.exe)` | command-line version (tests, rendering to WAV) |
| `VST3/EPS16.vst3` | the VST3 instrument plug-in |
| `AU/EPS16.component` (macOS) | the Audio Unit (Logic Pro, GarageBand) |
| `roms/`, `scsi/` | where your ROMs/OS disk and SCSI disk images go |

### macOS portable package: what you need to know

1. **Keep the folder together.** Unzip and keep `EPS-16-Plus.app` with the `roms` and `scsi` folders next to
   it (the folder can go anywhere, e.g. in Applications or Documents): the app looks for them there.
2. **Allow the app to open, once.** The app is not notarized by Apple, so macOS refuses to open it
   ("cannot verify ..."). In Terminal, type `xattr -dr com.apple.quarantine ` (with a space at the end),
   drag the unzipped folder onto the window and press Enter. This also covers the plug-in.
   (Alternatively open the app once, then System Settings › Privacy & Security › **Open Anyway**.)
3. **ROMs and OS disk** go in `roms/` (see below). If they are not found, the app lists the folders it
   searched and offers **Choose folder…**; the folder you pick is remembered.
4. **Audio input** (sampling, Audio Tracks): macOS asks for microphone access the first time; allow it
   (or later in System Settings › Privacy & Security › Microphone).
5. **Menus:** File and View are in the macOS menu bar (⌘O open disk, ⌘E eject, ⌘S save disk changes,
   ⌘R reset, ⌘K keyboard, ⌘, options). The Ctrl shortcuts listed below work too.
6. **Plug-ins:** copy `VST3/EPS16.vst3` to `~/Library/Audio/Plug-Ins/VST3/` and, for Logic Pro,
   `AU/EPS16.component` to `~/Library/Audio/Plug-Ins/Components/` (Finder: Go › Go to Folder…), and open
   the app once first. More details: `README.txt` in the package.

## You need your own ROM and OS disk

The ROM and the operating system belong to Ensoniq/E-mu and are **not included**. Put them in the
`roms` folder next to `eps16.exe` (Windows) or `EPS-16-Plus.app` (macOS):

- the two EPROMs: `eps16plus-u28.bin` (U28, even bytes) and `eps16plus-u27.bin` (U27, odd bytes);
- the OS floppy image `os130.img` (or any single `.img` in that folder).

## Using it

- **Disks:** drag a floppy image (`.img`, a Giebler `.ede` / `.eda` image or an HxC / Gotek `.hfe` image) onto the window, or one or more instruments (`.efe`): they
  are put on a temporary floppy in the drive, then use LOAD as usual. The drive's eject button ejects.
  File › Save Disk Changes (Ctrl+S) writes the floppy back to its image.
- **Record a song to WAV:** File › Record Song to WAV… (Ctrl+W; ⇧⌘R on the Mac). Choose the folder; the
  recording then waits and starts when you press **Play** on the sequencer, and ends when the song
  ends or you press Stop (it goes on until the sound has died away). The file is 24-bit stereo at the
  audio device's sample rate and gets the name of the song / sequence, as shown on the Seq•Song page
  (open that page once so that the name is known; otherwise the file gets a generic name).
  Ctrl+W again cancels or stops.
- **Power:** hold the Power button for about 1 s to restart the machine (or File › Reset).
- **Boot from SCSI:** Options › SCSI drives › *Boot from* › "No floppy: SCSI drive or Flash Bank" starts the
  machine with an empty floppy drive, booting from a SCSI drive that has the OS on it (command line:
  `--boot scsi` / `--boot floppy`, this run only). The OS disk image is only ever taken from the `roms`
  folder next to the program: nothing is fetched from `%APPDATA%`, so deleting it there removes it.
- **Mirage disks:** a 440 KB Ensoniq Mirage disk image (80 tracks, 1 side, 5 x 1024 + 512-byte sectors) can go in the floppy drive: Command › System/MIDI › *LOAD MIRAGE-DSK SOUND* imports its sounds (the OS asks for the OS disk first, then for the Mirage disk).
- **SCSI activity LED:** the red LED above the floppy drive blinks while the SCSI bus is in use: slowly while reading, fast while writing.
- **ROMs not found:** the program lists the folders it searched and offers "Choose folder..."; the chosen folder is remembered (the plug-in uses it too).
- **Up + Down together** jumps to the middle of the value's range (0 for -99..+99, 50 for 0..99), as
  on the real unit: right-click an arrow, or press both arrow keys.
- **MIDI:** the supplied OS disk is set to MIDI MULTI (instruments 1-8 on channels 9-16); by default
  MIDI In is sent to the "Keyboard channel" (9) set in the options.
- **Options** (View › Options, Ctrl+O): audio driver (Windows: WASAPI, DirectSound, ASIO...; macOS: Core Audio), outputs, sample
  rate, buffer, OEX-6 outputs, sampler input, MIDI ports and channel, and
  the SCSI drives folder. Settings are kept in `%APPDATA%\Ensoniq\EPS16emu\eps16.cfg` (macOS: `~/Library/Application Support/Ensoniq/EPS16emu/eps16.cfg`).

**Computer keyboard** (it does not play notes: use a MIDI keyboard or the on-screen keyboard):

| Key | Function |
|---|---|
| 1–8, F1–F8 | instrument buttons 1–8 |
| numeric keypad | page buttons as printed: 1 Env 1 … 4 Pitch … 9 Layer, 0 Track |
| R / Space | RECORD / PLAY |
| Arrows | panel arrows |
| Enter / Esc | YES•ENTER / NO•CANCEL |
| F11 / F12 | left / right Patch Select |
| F9 | show/hide the on-screen keyboard |
| Ctrl+E / Ctrl+S / Ctrl+R | eject / save the floppy / reset |
| Ctrl+F / Ctrl+I | effects on/off / audio input on/off |

## SCSI drives

Put disk images named like on a ZuluSCSI in a folder: `HD0.img` is a hard disk on SCSI ID 0,
`CD5.iso` a CD-ROM on ID 5, and so on. By default the emulator uses a `scsi` folder next to it;
choose another one in Options › SCSI drives.

**New formatted drives:** in Options › SCSI drives pick a size (50 MB to 2 GB) and click
**Create HDn.img**. The image is created on the first free SCSI ID, already formatted as the EPS's
FORMAT SCSI DRIVE does it, so it is ready to use. Reset the EPS so that the OS sees new drives.

## Fast loading and saving

While a file is loaded or saved (floppy, SCSI or writing to the FB-2) the emulated EPS-16 Plus runs
as fast as the computer allows instead of at its real speed, so loads, saves, formatting and booting
take a fraction of the time (booting: ~1.5 s instead of ~7 s). The sound is muted meanwhile (it fades
out and back in). It stays at real speed while the sequencer plays or records, so songs keep their
timing. Options › Hardware › **Loading / saving**: *Fast* (default) or *Real speed*. The VST3 plug-in
follows the app's choice; in a host it runs ahead with up to ~40% of each audio block's time, so the
speed-up there is smaller.

## FB-2 Flash Bank

The FB-2 was Ensoniq's flash memory board for the EPS-16 Plus: four Intel 28F020 chips (1 MB) on the
sound memory expansion connector. Turn it on in Options › Hardware › *FB-2 Flash Bank*; the EPS
restarts with the board fitted. Its contents are kept in `flashbank/fb2.bin`, a folder next to the
program (next to its `roms` folder; on macOS next to the .app). The VST3 plug-in uses the same file.
If that folder cannot be written (e.g. the program is under Program Files) the file goes in the
per-user folder, with `eps16.cfg`.

It works as on the real unit, because the OS (boot ROM) itself drives the flash chips:

- **Format:** Command › System/MIDI › FORMAT FLASH BANK. A new board must be formatted first. This
  is the only way to free space: files on the flash cannot be deleted one by one, the whole flash is
  erased (it takes about 45 seconds, as the chips are erased and checked byte by byte).
- **Choose the flash:** hold **Load** and press **Instrument** to switch between the Flash Bank and the
  last storage device (floppy / SCSI). The display then lists the flash's files.
- **Save:** with the flash chosen, SAVE INSTRUMENT stores the instrument there and COPY OS TO DISK
  writes the OS to an empty (just formatted) flash. Only the OS and instruments go on the flash: a
  bank or a sequence gets DISK TYPE ERROR, as on the real board.
- **Load:** instant. The instruments are played straight from the flash, nothing is copied into the
  sound memory (they cannot be edited: CANT EDIT FLASH SOUND).
- **Boot:** with the OS in the flash and no floppy in the drive (Options › SCSI drives › *Boot from* ›
  "No floppy"), the EPS starts from the flash in about a second after the floppy check.

## D/A converters

The outputs go through a model of the EPS-16 Plus's Philips TDA1541A converters (16-bit words with
each chip's small bit-weight errors, no oversampling filter so each sample is held for a whole period)
and of the main board's analog section after them (I/V converter, low-pass filter, coupling
capacitor; the OEX-6 has the same circuit), then are resampled to the computer's rate.
Options › Hardware › **D/A converters**: *TDA1541A (as the EPS-16 Plus)* (default) or *Ideal (perfect
converter)*, which only resamples.

## VST3 plug-in and Audio Unit

1. The installers put the plug-ins in place. From a portable package, copy the whole `VST3\EPS16.vst3`
   folder to `C:\Program Files\Common Files\VST3` (Windows) or to `~/Library/Audio/Plug-Ins/VST3/`
   (macOS), and on macOS `AU/EPS16.component` to `~/Library/Audio/Plug-Ins/Components/` (Logic Pro,
   GarageBand and other Audio Unit hosts).
2. Run the app (`eps16.exe` / `EPS-16-Plus.app`) once: the plug-in then uses the same ROMs, OS disk, SCSI folder and settings as
   the app (including the MIDI channel: host notes go to channel 9 by default).
3. Rescan plug-ins in your host and load **EPS-16 Plus Emulator** as an instrument (in Logic Pro:
   Instrument › AU Instruments › EPS16emu › EPS-16 Plus Emulator; Logic lists a *Multi-Output* version
   too when it can use the extra outputs).
- The Audio Unit is the VST3 inside Steinberg's AU wrapper (`Contents/Resources/plugin.vst3`): it works
  the same way.

- Outputs: Main plus AUX 1, BUS 2 and BUS 3 (enable them in the host to get them separately); a
  stereo input for sampling; MIDI In and MIDI Out.
- The editor is just the panel: click the drive slot to choose a disk image (`.img`, `.ede`, `.eda`, `.hfe`) or
  instrument files, or drag them onto the panel (where the host passes drops on to plug-ins), use the drive's eject
  button and the Power button, as in the app.
- The plug-in has no settings of its own: change them in the app and the plug-in follows.
- The project stores the volume and the disks in use; on reopening the EPS boots again with them
  (RAM contents are not saved: load your sounds, as on the real unit).

## Building

Needs a C++17 compiler, CMake and SDL2 (or `-DEPS16_BUNDLED_SDL=ON` to build it from source).

```sh
cmake -S . -B build && cmake --build build -j          # app and command-line version
cmake -S plugin -B build-vst && cmake --build build-vst # VST3 plug-in (downloads the VST3 SDK)
```

On macOS, `./build-mac.sh` builds the app, the VST3 and the Audio Unit and makes both packages (the
installer with `installer/mac/make-pkg.sh`). The Windows packages are made by `installer/windows/package.sh`
(NSIS for the installer); `.github/workflows/windows.yml` shows the whole cross-compiled build.

macOS (universal: Apple Silicon and Intel): `./build-mac.sh` builds `EPS-16-Plus.app` and the VST3 plug-in into `dist/` (see
`README-MAC.txt`); the `macOS` GitHub Actions workflow does the same on a Mac runner.

Cross-compiling for Windows from Linux: see `mingw64.cmake.example` and `build-vst-win-mingw.sh`.
Technical notes (emulated hardware, memory map, code layout), in Portuguese:
[docs/TECHNICAL-pt.md](docs/TECHNICAL-pt.md).

## Credits and licences

- This program is free software under the **GNU GPL v3** (`LICENSE`).
- **Musashi** 68000 core by Karl Stenerud; **ES5505/ES5510** algorithms derived from MAME
  (BSD-3-Clause); **RtMidi/RtAudio** by Gary P. Scavone; **Steinberg ASIO SDK** (GPLv3);
  **SDL2** (zlib); **VST3 SDK** (MIT) and **VSTGUI** (BSD-3-Clause) by Steinberg; the macOS Audio Unit
  is built with Apple's **AudioUnitSDK** (Apache 2.0); the Windows installer is made with **NSIS**
  (zlib/libpng, bzip2 and CPL licences); panel and
  options page lettering rendered from **GNU FreeFont** FreeSans (GPLv3 with the font exception);
  the 8x8 bitmap font by Daniel Hepper (public domain). The licence texts are in `LICENSES/` (and
  next to Musashi, RtMidi and RtAudio in their folders).
- VST and ASIO are trademarks of Steinberg Media Technologies GmbH.
- Ensoniq and EPS-16 Plus are trademarks of their owners. This project is not affiliated with them;
  the ROM and OS are not included.
