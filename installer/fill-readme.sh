#!/bin/sh
# Fills in a README template of the packages (%PRODUCT%, %VERSION%... from product.conf and VERSION).
#   installer/fill-readme.sh <template> <output>
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/installer/product.conf"
VERSION=$(head -1 "$ROOT/VERSION")
VERSION=${VERSION%.0}                               # 1.0.0 -> 1.0
lo=$(echo "$ROM_LO_LABEL" | sed 's/^[^(]*(\(.*\)).*$/\1/')
hi=$(echo "$ROM_HI_LABEL" | sed 's/^[^(]*(\(.*\)).*$/\1/')
title=$(head -1 "$1" | sed "s/%PRODUCT%/$PRODUCT/g; s/%VERSION%/$VERSION/g")
line=$(printf '%s' "$title" | sed 's/./=/g')
sed -e "s|%PRODUCT%|$PRODUCT|g; s|%VERSION%|$VERSION|g; s|%MACHINE%|$MACHINE|g; s|%SHORT%|$SHORT|g" \
    -e "s|%ZIPBASE%|$ZIPBASE|g; s|%REPO_URL%|$REPO_URL|g; s|%ROM_LO%|$lo; a .bin file|g; s|%ROM_HI%|$hi; a .bin file|g" \
    -e "s|%LINE%|$line|g" "$1" > "$2"
