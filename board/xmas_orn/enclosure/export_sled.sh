#!/bin/sh
# Renders the one-piece sled and its display bridge, print-oriented, into stl/.
set -e
cd "$(dirname "$0")"
OPENSCAD=${OPENSCAD:-$(ls /Applications/OpenSCAD*.app/Contents/MacOS/OpenSCAD 2>/dev/null | head -1)}
[ -x "$OPENSCAD" ] || { echo "openscad not found; set OPENSCAD=" >&2; exit 1; }
mkdir -p stl
for p in sled bridge; do
    "$OPENSCAD" -q --export-format binstl -D "part=\"${p}_print\"" -o "stl/xmas_orn_${p}.stl" xmas_orn_sled.scad
    echo "stl/xmas_orn_${p}.stl"
done
