#!/bin/sh
# Puts the Windows packages together from a cross-compiled build (see .github/workflows/windows.yml):
#   <dist>/<SHORT>-Windows/                          the portable folder
#   <dist>/<ZIPBASE>-Windows-Portable.zip           that folder, zipped
#   <dist>/<ZIPBASE>-Windows-Setup.exe              the installer (when makensis is there)
#   <dist>/<ZIPBASE>-Windows-Setup.zip              the installer with a README and the licences
#
#   installer/windows/package.sh <app build dir> <VST3 bundle> <llvm-mingw dir> <dist dir> [makensis]
# The ROMs, OS disks and disk images are never put in them.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
. "$ROOT/installer/product.conf"
APPB=${1:?usage: $0 <app build dir> <VST3 bundle> <llvm-mingw dir> <dist dir> [makensis]}
VST=${2:?}; MINGW=${3:?}; DIST=${4:?}; MAKENSIS=${5:-makensis}
P="$DIST/$SHORT-Windows"
rm -rf "$P"
mkdir -p "$P/VST3" "$P/licenses" "$P/roms" "$P/scsi"
"$MINGW/bin/llvm-strip" -o "$P/$EXE.exe" "$APPB/$EXE.exe"
"$MINGW/bin/llvm-strip" -o "$P/$EXE-cli.exe" "$APPB/$EXE-cli.exe"
cp -R "$VST" "$P/VST3/"
cp "$ROOT/roms/README.txt" "$P/roms/"
cp "$ROOT/scsi/README.txt" "$P/scsi/"
cp "$ROOT/README-WINDOWS.txt" "$P/README.txt"
cp "$ROOT"/LICENSES/*.txt "$ROOT/musashi/LICENSE-Musashi.txt" "$ROOT/third_party/RtMidi-LICENSE.txt" \
   "$ROOT/third_party/rtaudio/RtAudio-LICENSE.txt" "$P/licenses/"
rm -f "$P/licenses/NSIS-LICENSE.txt" "$P/licenses/AudioUnitSDK-LICENSE.txt"   # (the installer's / macOS only)
[ -f "$MINGW/LICENSE.TXT" ] && cp "$MINGW/LICENSE.TXT" "$P/licenses/llvm-mingw-runtime-LICENSE.txt"
if find "$P" -iname "*.bin" -o -iname "*.img" -o -iname "*.iso" | grep .; then echo "ROM or disk image in the package"; exit 1; fi
( cd "$DIST" && rm -f "$ZIPBASE-Windows-Portable.zip" && zip -qr9 "$ZIPBASE-Windows-Portable.zip" "$SHORT-Windows" )
echo "Portable package: $DIST/$ZIPBASE-Windows-Portable.zip"
if command -v "$MAKENSIS" >/dev/null 2>&1; then
    "$HERE/make-setup.sh" "$P" "$DIST/$ZIPBASE-Windows-Setup.exe" "$MAKENSIS"
    # the installer, its README and the licences (the program's, and NSIS's for the installer itself)
    Z="$DIST/$ZIPBASE-Windows-Setup"
    rm -rf "$Z" "$Z.zip"; mkdir -p "$Z/licenses"
    cp "$DIST/$ZIPBASE-Windows-Setup.exe" "$Z/"
    "$ROOT/installer/fill-readme.sh" "$HERE/README-SETUP.txt" "$Z/README.txt"
    cp "$P"/licenses/*.txt "$ROOT/LICENSES/NSIS-LICENSE.txt" "$Z/licenses/"
    ( cd "$DIST" && zip -qr9 "$ZIPBASE-Windows-Setup.zip" "$ZIPBASE-Windows-Setup" )
    echo "Installer package: $Z.zip"
else
    echo "(no makensis: no installer)"
fi
