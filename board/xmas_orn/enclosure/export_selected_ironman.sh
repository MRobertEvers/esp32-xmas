#!/bin/sh
# Selected image concepts only: 02 Trapezoid Armor, 03 Stark Core, 05 Reactor Medallion.
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
for theme in trapezoid_armor stark_core reactor_medallion; do
    source="ironman_${theme}.scad"
    mkdir -p "stl/$theme" docs
    for colour in body gold black cyan; do
        target="stl/$theme/${theme}_${colour}.stl"
        "$OPENSCAD" --hardwarnings -q --export-format binstl \
            -D "part=\"${colour}_print\"" -o "$target" "$source"
        echo "$target"
    done
    case "$theme" in
        stark_core) front='13.75,37.6,8.8,0,0,0,305'; angled='13.75,37.6,8.8,23,16,0,320' ;;
        trapezoid_armor) front='13.75,27.1,8.8,0,0,0,365'; angled='13.75,27.1,8.8,23,16,0,390' ;;
        reactor_medallion) front='13.75,30.6,8.8,0,0,0,350'; angled='13.75,30.6,8.8,23,16,0,365' ;;
    esac
    "$OPENSCAD" --hardwarnings -q --projection=o --colorscheme=Tomorrow \
        --imgsize=1100,1200 --camera="$front" -o "docs/${theme}_front.png" "$source"
    "$OPENSCAD" --hardwarnings -q --projection=o --colorscheme=Tomorrow \
        --imgsize=1100,1200 --camera="$angled" -o "docs/${theme}_iso.png" "$source"
done
