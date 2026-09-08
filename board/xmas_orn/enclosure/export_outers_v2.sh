#!/bin/sh
# V2 only. Originals in stl/outers/ are never written by this script.
set -eu
cd "$(dirname "$0")"
if [ -z "${OPENSCAD:-}" ]; then
    OPENSCAD=$(command -v openscad || true)
    if [ -z "$OPENSCAD" ]; then
        for candidate in /Applications/OpenSCAD*.app/Contents/MacOS/OpenSCAD; do
            if [ -x "$candidate" ]; then OPENSCAD=$candidate; break; fi
        done
    fi
fi
[ -n "$OPENSCAD" ] && [ -x "$OPENSCAD" ] || { echo "OpenSCAD not found; set OPENSCAD to its executable path." >&2; exit 1; }
for spec in "cap body white blue" "spidey_nwh body black gold white" "ironman body gold black white" "widow body silver red" "spidey_classic body blue black white"; do
    set -- $spec
    theme=$1
    shift
    mkdir -p "stl/outers_v2/$theme"
    for colour in "$@"; do
        target="stl/outers_v2/$theme/${theme}_${colour}.stl"
        "$OPENSCAD" --hardwarnings --export-format binstl -q \
            -D "part=\"${theme}_${colour}_print\"" -o "$target" xmas_orn_outers_v2.scad
        echo "$target"
    done
done
mkdir -p docs
"$OPENSCAD" --hardwarnings -q --projection=o --colorscheme=Tomorrow \
    --imgsize=2600,700 --camera=223.75,30,8.8,0,0,0,390 \
    -o docs/outers_v2_front.png xmas_orn_outers_v2.scad
"$OPENSCAD" --hardwarnings -q --projection=o --colorscheme=Tomorrow \
    --imgsize=2600,1000 --camera=223.75,30,8.8,28,0,12,560 \
    -o docs/outers_v2_showcase.png xmas_orn_outers_v2.scad
echo "docs/outers_v2_front.png"
echo "docs/outers_v2_showcase.png"
