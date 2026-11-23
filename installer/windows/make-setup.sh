#!/bin/sh
# Makes the Windows installer (Setup .exe, with an uninstaller) with NSIS 3, from the portable folder
# (the program, its command-line version, VST3\, roms\, scsi\, README.txt, licenses\).
#
#   installer/windows/make-setup.sh <portable folder> <output Setup.exe> [makensis]
#
# makensis: the NSIS compiler (Linux: the "nsis" package; Windows: makensis.exe from nsis.sourceforge.io).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
. "$ROOT/installer/product.conf"
VERSION=$(head -1 "$ROOT/VERSION")
SRC=$(cd "${1:?usage: $0 <portable folder> <output .exe> [makensis]}" && pwd)
OUT=${2:?usage: $0 <portable folder> <output .exe> [makensis]}
MAKENSIS=${3:-makensis}
mkdir -p "$(dirname "$OUT")"
OUT=$(cd "$(dirname "$OUT")" && pwd)/$(basename "$OUT")
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT

SIZE_KB=$(du -sk "$SRC" | cut -f1)
BS='\'
{
    printf '!define PRODUCT "%s"\n' "$PRODUCT"
    printf '!define SHORT "%s"\n' "$SHORT"
    printf '!define EXE "%s"\n' "$EXE"
    printf '!define VERSION "%s"\n' "$VERSION"
    printf '!define COMPANY "%semu"\n' "$SHORT"
    printf '!define CFGDIR "%s"\n' "$CFGDIR"
    printf '!define SRC "%s"\n' "$SRC"
    printf '!define OUTFILE "%s"\n' "$OUT"
    printf '!define ICON "%s"\n' "$ROOT/src/win/$EXE.ico"
    printf '!define UNINST_KEY "Software%sMicrosoft%sWindows%sCurrentVersion%sUninstall%s%sEmulator"\n' "$BS" "$BS" "$BS" "$BS" "$BS" "$SHORT"
    printf '!define SIZE_KB %s\n' "$SIZE_KB"
    printf '!macro DATA_DIRS_CREATE\n'
    for d in $DATA_DIRS; do
        printf '    CreateDirectory "$INSTDIR%s%s"\n' "$BS" "$d"
        printf "    nsExec::ExecToLog 'icacls \"\$INSTDIR%s%s\" /grant *S-1-5-32-545:(OI)(CI)M'\n" "$BS" "$d"
        printf '    Pop $0\n'
    done
    printf '!macroend\n!macro DATA_DIRS_REMOVE\n'
    for d in $DATA_DIRS; do printf '    RMDir /r "$INSTDIR%s%s"\n' "$BS" "$d"; done
    printf '!macroend\n!macro DATA_DIRS_RMDIR\n'
    for d in $DATA_DIRS; do printf '    RMDir "$INSTDIR%s%s"\n' "$BS" "$d"; done
    printf '!macroend\n'
} > "$W/product.nsh"
cp "$HERE/setup.nsi" "$W/"
[ -n "$SHOW_NSH" ] && cat "$W/product.nsh"
"$MAKENSIS" -V2 -NOCD "-X!addincludedir $W" "$W/setup.nsi"
echo "Installer: $OUT"
