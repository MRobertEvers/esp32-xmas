#!/usr/bin/env python3
"""Validate the selected Iron Man concepts after export_selected_ironman.sh.

Requires numpy, trimesh and networkx. Writes hashed results, never changes models or STLs.
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

import numpy as np
import trimesh

ROOT = Path(__file__).resolve().parent
THEMES = ("trapezoid_armor", "stark_core", "reactor_medallion")
COLOURS = ("body", "gold", "black", "cyan")
TOLERANCE = 0.0001


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def find_openscad():
    executable = os.environ.get("OPENSCAD") or shutil.which("openscad")
    if executable:
        return executable
    candidates = sorted(Path("/Applications").glob("OpenSCAD*.app/Contents/MacOS/OpenSCAD"))
    require(candidates, "OpenSCAD not found; set OPENSCAD to its executable path")
    return str(candidates[0])


def geometry_check(openscad, theme, kind, temporary):
    output = temporary / f"{theme}_{kind}.stl"
    result = subprocess.run(
        [openscad, "--hardwarnings", "-D", f'part="check_{kind}"', "-o", str(output),
         str(ROOT / f"ironman_{theme}.scad")], capture_output=True, text=True, check=False,
    )
    log = result.stdout + result.stderr
    require("WARNING" not in log and "ERROR" not in log, log)
    empty = "Current top level object is empty." in log
    if empty:
        require(result.returncode in (0, 1), log)
        return {"empty": True, "residual_volume_mm3": 0}
    require(result.returncode == 0 and output.exists(), log)
    mesh = trimesh.load_mesh(output)
    triangles = mesh.triangles
    # Measure the oriented Boolean result as a whole; splitting numerical boundary
    # fragments creates open surfaces with undefined individual volumes.
    volume = abs(float(np.einsum("ij,ij->i", triangles[:, 0],
                                np.cross(triangles[:, 1], triangles[:, 2])).sum() / 6))
    require(np.isfinite(volume) and volume < TOLERANCE, f"{theme} {kind}: {volume} mm^3")
    require(kind not in ("core", "window"), f"{theme} {kind} must be empty")
    return {"empty": False, "residual_volume_mm3": volume}


def main():
    openscad = find_openscad()
    report = {
        "openscad": subprocess.check_output([openscad, "--version"], stderr=subprocess.STDOUT, text=True).strip(),
        "volume_tolerance_mm3": TOLERANCE,
        "themes": {},
    }
    with tempfile.TemporaryDirectory(prefix="selected-ironman-") as temporary:
        for theme in THEMES:
            directory = ROOT / "stl" / theme
            require({p.stem for p in directory.glob("*.stl")} ==
                    {f"{theme}_{c}" for c in COLOURS}, f"Unexpected or missing STLs: {theme}")
            entry = {"parts": {}}
            meshes = []
            for colour in COLOURS:
                path = directory / f"{theme}_{colour}.stl"
                mesh = trimesh.load_mesh(path)
                require(mesh.is_watertight and mesh.is_winding_consistent and mesh.volume > 0,
                        f"Invalid mesh: {path}")
                components = mesh.split()
                require(all(m.volume > 0 for m in components), f"Inverted component: {path}")
                if colour == "body":
                    require(len(components) == 1, f"Disconnected body: {theme}")
                    require(abs(mesh.bounds[0, 2]) < 1e-5, f"Body not on bed: {theme}")
                entry["parts"][colour] = {
                    "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                    "watertight": True, "winding_consistent": True,
                    "components": len(components), "volume_mm3": round(float(mesh.volume), 4),
                }
                meshes.append(mesh)
            combined = trimesh.util.concatenate(meshes)
            require(np.all(combined.extents < 180), f"Exceeds A1 mini build volume: {theme}")
            require(combined.bounds[0, 2] >= -1e-5, f"Geometry below print bed: {theme}")
            entry["print_dimensions_mm"] = combined.extents.round(3).tolist()
            entry["geometry_checks"] = {
                kind: geometry_check(openscad, theme, kind, Path(temporary))
                for kind in ("core", "colours", "backing", "window")
            }
            report["themes"][theme] = entry
            print(f"PASS {theme}: four watertight parts; connected body; core, window, colours and backing checked")
    sources = ["xmas_orn_case.scad", "ironman_selected_parts.scad"] + [f"ironman_{t}.scad" for t in THEMES]
    report["source_sha256"] = {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in sources}
    destination = ROOT / "docs/selected_ironman_validation.json"
    destination.write_text(json.dumps(report, indent=2) + "\n")
    print(destination)


if __name__ == "__main__":
    main()
