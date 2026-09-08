#!/bin/sh
# Export only the new Iron Man revision; all previous versions are preserved.
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
mkdir -p stl/ironman_v3 docs
for colour in body gold black white; do
    target="stl/ironman_v3/ironman_${colour}.stl"
    "$OPENSCAD" --hardwarnings -q --export-format binstl \
        -D "part=\"${colour}_print\"" -o "$target" ironman_v3.scad
    echo "$target"
done
"$OPENSCAD" --hardwarnings -q --projection=o --colorscheme=Tomorrow \
    --imgsize=900,1200 --camera=13.75,43,8.8,0,0,0,350 \
    -o docs/ironman_v3_front.png ironman_v3.scad
"$OPENSCAD" --hardwarnings -q --projection=o --colorscheme=Tomorrow \
    --imgsize=1000,1200 --camera=13.75,43,8.8,23,16,0,370 \
    -o docs/ironman_v3_iso.png ironman_v3.scad
echo "docs/ironman_v3_front.png"
echo "docs/ironman_v3_iso.png"
