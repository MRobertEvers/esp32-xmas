#!/bin/sh
# Renders print-oriented STLs for the Bambu Lab A1 mini into stl/.
set -e
cd "$(dirname "$0")"
OPENSCAD=${OPENSCAD:-$(ls /Applications/OpenSCAD*.app/Contents/MacOS/OpenSCAD 2>/dev/null | head -1)}
[ -x "$OPENSCAD" ] || { echo "openscad not found; set OPENSCAD=" >&2; exit 1; }
mkdir -p stl
for p in tray bezel sleeve; do
    "$OPENSCAD" -q -D "part=\"${p}_print\"" -o "stl/xmas_orn_${p}.stl" xmas_orn_case.scad
    echo "stl/xmas_orn_${p}.stl"
done
