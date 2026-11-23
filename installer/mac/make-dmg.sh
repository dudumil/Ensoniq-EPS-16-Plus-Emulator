#!/bin/bash
# Puts the macOS installer (.pkg) in a disk image, with the installer icon (the app's icon with a
# download badge) on the package and on the disk (a .pkg's own icon would not survive a download).
#
#   installer/mac/make-dmg.sh <installer .pkg> <output .dmg>
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
. "$ROOT/installer/product.conf"
PKG=${1:?usage: $0 <installer .pkg> <output .dmg>}
OUT=${2:?usage: $0 <installer .pkg> <output .dmg>}
ICNS="$ROOT/installer/icons/setup.icns"
T=$(mktemp -d)
trap 'hdiutil detach "$T/mnt" -quiet 2>/dev/null || true; rm -rf "$T"' EXIT
seticon() {   # icns file
    osascript -l JavaScript -e "ObjC.import('AppKit'); var i = \$.NSImage.alloc.initWithContentsOfFile('$1'); \$.NSWorkspace.sharedWorkspace.setIconForFileOptions(i, '$2', 0) ? 'ok' : 'failed'"
}
MB=$(( $(du -sm "$PKG" | cut -f1) + 8 ))
hdiutil create -size ${MB}m -fs HFS+ -volname "$PRODUCT" -layout NONE "$T/rw.dmg" -quiet
mkdir -p "$T/mnt"
hdiutil attach "$T/rw.dmg" -mountpoint "$T/mnt" -nobrowse -quiet
NAME="Install $PRODUCT.pkg"
ditto "$PKG" "$T/mnt/$NAME"
seticon "$ICNS" "$T/mnt/$NAME"
cp "$ICNS" "$T/mnt/.VolumeIcon.icns"
xcrun SetFile -a C "$T/mnt" 2>/dev/null || SetFile -a C "$T/mnt" 2>/dev/null || echo "(no SetFile: the disk keeps the standard icon)"
hdiutil detach "$T/mnt" -quiet
rm -f "$OUT"
hdiutil convert "$T/rw.dmg" -format UDZO -imagekey zlib-level=9 -o "$OUT" -quiet
echo "Disk image: $OUT"
