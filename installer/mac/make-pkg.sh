#!/bin/bash
# Makes the macOS installer (a .pkg with an uninstaller) from the portable folder that build-mac.sh
# puts together (the app, VST3/, AU/, roms/, scsi/, READMEs, licenses).
#
#   installer/mac/make-pkg.sh <portable folder> <output .pkg>
#
# What it installs (each part can be left out in the installer's "Customize" step):
#   - the app, with its roms/ and scsi/ folders, the READMEs and an uninstaller, in
#     /Applications/<product>/ (the folders the user writes to are made writable for everyone)
#   - the VST3 plug-in in /Library/Audio/Plug-Ins/VST3
#   - the Audio Unit (Logic Pro, GarageBand...) in /Library/Audio/Plug-Ins/Components
# The ROMs, OS disks and disk images are never in it.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
. "$ROOT/installer/product.conf"
VERSION=$(head -1 "$ROOT/VERSION")
SRC=${1:?usage: $0 <portable folder> <output .pkg>}
OUT=${2:?usage: $0 <portable folder> <output .pkg>}
SRC=$(cd "$SRC" && pwd)
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
DEST="/Applications/$PRODUCT"

# ---------------------------------------------------------------- payloads
mkdir -p "$W/app/$PRODUCT" "$W/vst3" "$W/au" "$W/scripts-app" "$W/scripts-au" "$W/res"
D="$W/app/$PRODUCT"
cp -R "$SRC/$APP" "$D/"
cp "$SRC/$EXE-cli" "$D/"
cp -R "$SRC/roms" "$SRC/scsi" "$SRC/licenses" "$D/"
cp "$SRC/README.txt" "$SRC/README.md" "$D/"
cp -R "$SRC/VST3/$SHORT.vst3" "$W/vst3/"
cp -R "$SRC/AU/$SHORT.component" "$W/au/"

# the uninstaller: a small AppleScript app that runs uninstall.sh as administrator
cat > "$W/uninstall.sh" <<EOF
#!/bin/sh
# Removes the $PRODUCT. \$1 = 1: keep the user's files (ROMs, disk images, the settings);
# \$2 = the user's home folder.
KEEP="\$1"; USERHOME="\$2"
D="$DEST"
rm -rf "/Library/Audio/Plug-Ins/VST3/$SHORT.vst3" "/Library/Audio/Plug-Ins/Components/$SHORT.component"
if [ "\$KEEP" = 1 ]; then
    for f in "\$D"/* "\$D"/.[!.]*; do
        [ -e "\$f" ] || continue
        case "\$(basename "\$f")" in $(echo $DATA_DIRS | tr ' ' '|')) ;; *) rm -rf "\$f" ;; esac
    done
else
    rm -rf "\$D"
    [ -n "\$USERHOME" ] && rm -rf "\$USERHOME/Library/Application Support/Ensoniq/$CFGDIR"
fi
for p in app vst3 au; do pkgutil --forget "$ID.\$p" >/dev/null 2>&1; done
killall -9 AudioComponentRegistrar >/dev/null 2>&1
exit 0
EOF
cat > "$W/uninstall.applescript" <<EOF
set p to "$PRODUCT"
set r to display dialog "Remove the " & p & "?" & return & return & "This removes the app, the VST3 plug-in and the Audio Unit. Your ROMs, disk images and settings can be kept." buttons {"Cancel", "Remove everything", "Remove, keep my files"} default button 3 cancel button 1 with title "Uninstall " & p with icon caution
set keep to "0"
if button returned of r is "Remove, keep my files" then set keep to "1"
set sh to quoted form of POSIX path of (path to resource "uninstall.sh")
set home to quoted form of POSIX path of (path to home folder)
do shell script "/bin/sh " & sh & " " & keep & " " & home with administrator privileges
display dialog p & " was removed." buttons {"OK"} default button 1 with title "Uninstall " & p
EOF
UNINST="$D/Uninstall $PRODUCT.app"
osacompile -o "$UNINST" "$W/uninstall.applescript"
cp "$W/uninstall.sh" "$UNINST/Contents/Resources/uninstall.sh"
codesign --force --deep -s - "$UNINST"

# writable data folders, and a fresh AU list after installing the Audio Unit
{
    echo '#!/bin/sh'
    for d in $DATA_DIRS; do echo "mkdir -p \"$DEST/$d\"; chmod 777 \"$DEST/$d\""; done
    echo 'exit 0'
} > "$W/scripts-app/postinstall"
printf '#!/bin/sh\nkillall -9 AudioComponentRegistrar >/dev/null 2>&1\nexit 0\n' > "$W/scripts-au/postinstall"
chmod +x "$W/scripts-app/postinstall" "$W/scripts-au/postinstall"

# ---------------------------------------------------------------- component packages
# (bundles are not "relocatable": the installer always puts them where they belong)
component() {   # name root install-location [scripts]
    local name=$1 root=$2 loc=$3 scripts=$4
    pkgbuild --analyze --root "$root" "$W/$name.plist" >/dev/null
    local i=0
    while /usr/libexec/PlistBuddy -c "Print :$i" "$W/$name.plist" >/dev/null 2>&1; do
        /usr/libexec/PlistBuddy -c "Set :$i:BundleIsRelocatable false" "$W/$name.plist"
        i=$((i + 1))
    done
    pkgbuild --root "$root" --component-plist "$W/$name.plist" --identifier "$ID.$name" --version "$VERSION" \
        --install-location "$loc" ${scripts:+--scripts "$scripts"} "$W/pkgs/$name.pkg"
}
mkdir -p "$W/pkgs"
component app "$W/app" /Applications "$W/scripts-app"
component vst3 "$W/vst3" /Library/Audio/Plug-Ins/VST3
component au "$W/au" /Library/Audio/Plug-Ins/Components "$W/scripts-au"

# ---------------------------------------------------------------- the installer
cp "$ROOT/LICENSE" "$W/res/LICENSE.txt"
cat > "$W/res/welcome.txt" <<EOF
$PRODUCT $VERSION

This installs:
  - the $PRODUCT app in Applications > $PRODUCT, with its roms and scsi folders and an uninstaller;
  - the VST3 plug-in (/Library/Audio/Plug-Ins/VST3);
  - the Audio Unit for Logic Pro and GarageBand (/Library/Audio/Plug-Ins/Components).
"Customize" lets you leave parts out.

The ROMs and the operating system are not included: after installing, copy them into
Applications > $PRODUCT > roms (see the README there), then open the app once so that the
plug-ins find the same files.

To remove everything later, open "Uninstall $PRODUCT" in the same folder.
EOF
cat > "$W/distribution.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>$PRODUCT $VERSION</title>
    <welcome file="welcome.txt" mime-type="text/plain"/>
    <license file="LICENSE.txt" mime-type="text/plain"/>
    <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <domains enable_anywhere="false" enable_currentUserHome="false" enable_localSystem="true"/>
    <os-version min="11.0"/>
    <choices-outline>
        <line choice="app"/>
        <line choice="vst3"/>
        <line choice="au"/>
    </choices-outline>
    <choice id="app" title="$PRODUCT app" description="The app, its roms and scsi folders, the READMEs and the uninstaller, in Applications > $PRODUCT.">
        <pkg-ref id="$ID.app"/>
    </choice>
    <choice id="vst3" title="VST3 plug-in" description="For VST3 hosts (Cubase, Studio One, Reaper, Bitwig, Ableton Live...): /Library/Audio/Plug-Ins/VST3/$SHORT.vst3">
        <pkg-ref id="$ID.vst3"/>
    </choice>
    <choice id="au" title="Audio Unit" description="For Logic Pro, GarageBand and other Audio Unit hosts: /Library/Audio/Plug-Ins/Components/$SHORT.component">
        <pkg-ref id="$ID.au"/>
    </choice>
    <pkg-ref id="$ID.app" version="$VERSION">app.pkg</pkg-ref>
    <pkg-ref id="$ID.vst3" version="$VERSION">vst3.pkg</pkg-ref>
    <pkg-ref id="$ID.au" version="$VERSION">au.pkg</pkg-ref>
</installer-gui-script>
EOF
mkdir -p "$(dirname "$OUT")"
productbuild --distribution "$W/distribution.xml" --resources "$W/res" --package-path "$W/pkgs" "$OUT"
echo "Installer: $OUT"
