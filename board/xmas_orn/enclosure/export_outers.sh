#!/bin/sh
# Renders the themed outers, one STL per colour, standing on their flat bottoms, into stl/outers/<theme>/.
set -e
cd "$(dirname "$0")"
OPENSCAD=${OPENSCAD:-$(ls /Applications/OpenSCAD*.app/Contents/MacOS/OpenSCAD 2>/dev/null | head -1)}
[ -x "$OPENSCAD" ] || { echo "openscad not found; set OPENSCAD=" >&2; exit 1; }
for spec in "cap body white blue" "spidey_nwh body black white" "ironman body gold" "widow body red" "spidey_classic body black blue"; do
    set -- $spec; theme=$1; shift
    mkdir -p "stl/outers/$theme"
    for c in "$@"; do
        "$OPENSCAD" -q -D "part=\"${theme}_${c}_print\"" -o "stl/outers/$theme/${theme}_${c}.stl" xmas_orn_outers.scad
        echo "stl/outers/$theme/${theme}_${c}.stl"
    done
done
