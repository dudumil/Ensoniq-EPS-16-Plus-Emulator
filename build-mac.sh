#!/bin/bash
# Builds the EPS-16 Plus emulator for macOS (universal: Apple Silicon and Intel): the "EPS-16-Plus.app" app, the
# "EPS16.vst3" plug-in and the "EPS16.component" Audio Unit (Logic Pro), and packages them twice:
#   dist/EPS16-Plus-Emulator-macOS-Portable.zip    a folder to unzip anywhere
#   dist/EPS16-Plus-Emulator-macOS-Installer.dmg   a disk image with the installer (.pkg, with an uninstaller)
#   dist/EPS16-Plus-Emulator-macOS-Installer.zip   that disk image with a README and the licences
#
# Requires:
#   - Apple Command Line Tools (free):          xcode-select --install
#   - CMake 3.25 or newer:                      brew install cmake   (or the .dmg from cmake.org)
#   - an internet connection (the script downloads the VST3 SDK and SDL2 the first time)
#
# Usage: ./build-mac.sh           builds everything and installs the plug-ins in ~/Library/Audio/Plug-Ins
#        ./build-mac.sh --no-install
set -e
cd "$(dirname "$0")"

INSTALL=1
[ "$1" = "--no-install" ] && INSTALL=0

if ! xcode-select -p >/dev/null 2>&1; then
    echo "Apple's Command Line Tools are missing. Opening the installer;"
    echo "when it finishes, run this script again."
    xcode-select --install || true
    exit 1
fi
if ! command -v cmake >/dev/null 2>&1; then
    if [ -x /Applications/CMake.app/Contents/bin/cmake ]; then
        export PATH="/Applications/CMake.app/Contents/bin:$PATH"
    else
        echo "CMake is missing. Install it with 'brew install cmake' or from https://cmake.org/download/"
        exit 1
    fi
fi
if ! ls roms/*.bin >/dev/null 2>&1; then
    echo "Warning: the roms/ folder does not have the EPS-16 Plus ROMs. The app and the plug-in will look for them in"
    echo "  ~/Library/Application Support/Ensoniq/EPS16emu/roms   (the ROM files and os130.img)"
fi

JOBS=$(sysctl -n hw.ncpu)

echo "=== 1/3  app EPS-16-Plus.app ==="
cmake -S . -B build-mac -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build-mac -j"$JOBS" --target eps16 eps16-cli

echo "=== 2/3  plug-ins EPS16.vst3 and EPS16.component ==="
cmake -S plugin -B build-vst -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build-vst -j"$JOBS" --target EPS16 EPS16_AU

mkdir -p dist
rm -rf dist/EPS-16-Plus.app dist/EPS16.vst3 dist/EPS16.component dist/eps16-cli
cp -R build-mac/EPS-16-Plus.app dist/
cp build-mac/eps16-cli dist/
VST=$(find build-vst -name "EPS16.vst3" -type d -maxdepth 4 | grep -v "/Resources/" | head -1)
cp -R "$VST" dist/
cp -R build-vst/AU/EPS16.component dist/

# "ad hoc" signature: required on Apple Silicon to run locally built code (the VST3 inside the
# component is signed first)
codesign --force --deep -s - dist/EPS-16-Plus.app
codesign --force --deep -s - dist/EPS16.vst3
codesign --force --deep -s - dist/EPS16.component/Contents/Resources/plugin.vst3
codesign --force -s - dist/EPS16.component
codesign --force -s - dist/eps16-cli

# Portable package, as for Windows: the app, VST3/EPS16.vst3, AU/EPS16.component, empty roms/ and scsi/
# folders (with their READMEs), the READMEs and the licenses. The ROMs, OS disks and disk images are
# never put in it.
PKG=dist/EPS16-macOS
rm -rf "$PKG" dist/EPS16-Plus-Emulator-macOS*.zip dist/EPS16-Plus-Emulator-macOS*.pkg dist/EPS16-Plus-Emulator-macOS*.dmg
mkdir -p "$PKG/VST3" "$PKG/AU" "$PKG/licenses" "$PKG/roms" "$PKG/scsi"
cp -R dist/EPS-16-Plus.app "$PKG/"
cp -R dist/EPS16.vst3 "$PKG/VST3/"
cp -R dist/EPS16.component "$PKG/AU/"
cp dist/eps16-cli "$PKG/"
cp roms/README.txt "$PKG/roms/"
cp scsi/README.txt "$PKG/scsi/"
cp README-MAC.txt "$PKG/README.txt"
cp README.md "$PKG/README.md"
cp LICENSES/*.txt musashi/LICENSE-Musashi.txt third_party/RtMidi-LICENSE.txt third_party/rtaudio/RtAudio-LICENSE.txt "$PKG/licenses/"
[ -f build-mac/_deps/sdl2-src/LICENSE.txt ] && cp build-mac/_deps/sdl2-src/LICENSE.txt "$PKG/licenses/SDL2-LICENSE.txt"
VSTSDK_LIC=$(find build-vst/_deps -maxdepth 2 -path "*vst3sdk-src*" -name LICENSE.txt 2>/dev/null | head -1)
[ -n "$VSTSDK_LIC" ] && cp "$VSTSDK_LIC" "$PKG/licenses/VST3-SDK-LICENSE.txt"
rm -f "$PKG/licenses/NSIS-LICENSE.txt"            # (the Windows installer's)
ditto -c -k --keepParent "$PKG" "dist/EPS16-Plus-Emulator-macOS-Portable.zip"
echo "Portable package: dist/EPS16-Plus-Emulator-macOS-Portable.zip"

echo "=== 3/3  installer ==="
installer/mac/make-pkg.sh "$PKG" "dist/EPS16-Plus-Emulator-macOS-Installer.pkg"
installer/mac/make-dmg.sh "dist/EPS16-Plus-Emulator-macOS-Installer.pkg" "dist/EPS16-Plus-Emulator-macOS-Installer.dmg"
# the disk image, its README and the licences
Z="dist/EPS16-Plus-Emulator-macOS-Installer"
rm -rf "$Z" "$Z.zip"; mkdir -p "$Z/licenses"
cp "dist/EPS16-Plus-Emulator-macOS-Installer.dmg" "$Z/"
installer/fill-readme.sh installer/mac/README-INSTALLER.txt "$Z/README.txt"
cp "$PKG"/licenses/*.txt "$Z/licenses/"
ditto -c -k --keepParent "$Z" "$Z.zip"
echo "Installer package: $Z.zip"

if [ $INSTALL = 1 ]; then
    mkdir -p ~/Library/Audio/Plug-Ins/VST3
    rm -rf ~/Library/Audio/Plug-Ins/VST3/EPS16.vst3
    cp -R dist/EPS16.vst3 ~/Library/Audio/Plug-Ins/VST3/
    mkdir -p ~/Library/Audio/Plug-Ins/Components
    rm -rf ~/Library/Audio/Plug-Ins/Components/EPS16.component
    cp -R dist/EPS16.component ~/Library/Audio/Plug-Ins/Components/
    killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true
    echo "Plug-ins installed in ~/Library/Audio/Plug-Ins/VST3/EPS16.vst3 and ~/Library/Audio/Plug-Ins/Components/EPS16.component"
fi
echo
echo "Done. The app is in dist/EPS-16-Plus.app (you can drag it to Applications)."
