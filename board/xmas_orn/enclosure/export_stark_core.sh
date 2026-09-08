#!/bin/sh
# Export the selected Stark Core concept; leave earlier designs untouched.
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
mkdir -p stl/stark_core docs
for colour in body gold black cyan; do
    target="stl/stark_core/stark_core_${colour}.stl"
    "$OPENSCAD" --hardwarnings -q --export-format binstl \
        -D "part=\"${colour}_print\"" -o "$target" ironman_stark_core.scad
    echo "$target"
done
"$OPENSCAD" --hardwarnings -q --projection=o --colorscheme=Tomorrow \
    --imgsize=1000,1200 --camera=13.75,37.6,8.8,0,0,0,305 \
    -o docs/stark_core_front.png ironman_stark_core.scad
"$OPENSCAD" --hardwarnings -q --projection=o --colorscheme=Tomorrow \
    --imgsize=1100,1200 --camera=13.75,37.6,8.8,23,16,0,320 \
    -o docs/stark_core_iso.png ironman_stark_core.scad
