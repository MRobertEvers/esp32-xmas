#!/bin/sh
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
mkdir -p stl/ironman_v2b docs
for colour in body gold black white; do
    target="stl/ironman_v2b/ironman_${colour}.stl"
    "$OPENSCAD" --hardwarnings -q --export-format binstl \
        -D "part=\"${colour}_print\"" -o "$target" ironman_v2b.scad
    echo "$target"
done
"$OPENSCAD" --hardwarnings -q --projection=o --colorscheme=Tomorrow \
    --imgsize=1000,1100 --camera=13.75,30,8.8,0,0,0,290 \
    -o docs/ironman_v2b_front.png ironman_v2b.scad
"$OPENSCAD" --hardwarnings -q --projection=o --colorscheme=Tomorrow \
    --imgsize=1100,1100 --camera=13.75,30,8.8,23,16,0,305 \
    -o docs/ironman_v2b_iso.png ironman_v2b.scad
