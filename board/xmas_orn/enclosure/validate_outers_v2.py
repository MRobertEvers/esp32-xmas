#!/usr/bin/env python3
"""Validate exported V2 meshes and the CAD fit. Requires trimesh, numpy, networkx.

Run export_outers_v2.sh first. Writes a report with hashes of the checked STLs.
No original files are changed. No physical fit or slicer validation is implied.
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
PARTS = {
    "cap": ["body", "white", "blue"],
    "spidey_nwh": ["body", "black", "gold", "white"],
    "ironman": ["body", "gold", "black", "white"],
    "widow": ["body", "silver", "red"],
    "spidey_classic": ["body", "blue", "black", "white"],
}
TOLERANCE = 0.0001  # mm^3: shared CAD boundaries can leave numerical fragments


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


def check_geometry(openscad, theme, kind, directory):
    output = directory / f"{kind}_{theme}.stl"
    result = subprocess.run(
        [openscad, "--hardwarnings", "-D", f'part="check_{kind}_{theme}"',
         "-o", str(output), str(ROOT / "xmas_orn_outers_v2.scad")],
        capture_output=True, text=True, check=False,
    )
    log = result.stdout + result.stderr
    require("WARNING" not in log and "ERROR" not in log, log)
    if "Current top level object is empty." in log and result.returncode in (0, 1):
        return {"empty": True, "residual_volume_mm3": 0}
    require(result.returncode == 0 and output.exists(), log)
    mesh = trimesh.load_mesh(output)
    # Measure the oriented Boolean result as a whole. Splitting zero-thickness
    # boundary fragments creates open surfaces whose individual volumes are undefined.
    triangles = mesh.triangles
    volume = abs(float(np.einsum("ij,ij->i", triangles[:, 0],
                       np.cross(triangles[:, 1], triangles[:, 2])).sum() / 6))
    require(volume < TOLERANCE, f"{kind} / {theme}: intersection is {volume} mm^3")
    require(kind != "core", f"Core clearance must be empty: {theme}")
    return {"empty": False, "residual_volume_mm3": volume}


def main():
    openscad = find_openscad()
    report = {"openscad": subprocess.check_output([openscad, "--version"], stderr=subprocess.STDOUT, text=True).strip(),
              "volume_tolerance_mm3": TOLERANCE, "themes": {}}
    with tempfile.TemporaryDirectory(prefix="outers-v2-check-") as temporary:
        for theme, colours in PARTS.items():
            meshes = []
            entry = {"parts": {}}
            actual = {p.stem for p in (ROOT / "stl/outers_v2" / theme).glob("*.stl")}
            require(actual == {f"{theme}_{c}" for c in colours}, f"Unexpected/missing STLs: {theme}")
            for colour in colours:
                path = ROOT / "stl/outers_v2" / theme / f"{theme}_{colour}.stl"
                mesh = trimesh.load_mesh(path)
                require(mesh.is_watertight and mesh.is_winding_consistent and mesh.volume > 0,
                        f"Invalid mesh: {path}")
                components = mesh.split()
                require(all(p.volume > 0 for p in components), f"Inverted shell: {path}")
                if colour == "body":
                    require(len(components) == 1, f"Disconnected sleeve: {theme}")
                    require(abs(mesh.bounds[0, 2]) < 1e-5, f"Body is not on the bed: {theme}")
                meshes.append(mesh)
                entry["parts"][colour] = {
                    "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                    "watertight": True, "winding_consistent": True,
                    "components": len(components), "volume_mm3": round(float(mesh.volume), 4),
                }
            combined = trimesh.util.concatenate(meshes)
            require(np.all(combined.extents < 180), f"Exceeds A1 mini build volume: {theme}")
            require(combined.bounds[0, 2] >= -1e-5, f"Geometry below the bed: {theme}")
            entry["print_dimensions_mm"] = combined.extents.round(3).tolist()
            for kind in ("core", "colours", "backing"):
                entry[kind] = check_geometry(openscad, theme, kind, Path(temporary))
            report["themes"][theme] = entry
            print(f"PASS {theme}: {len(colours)} watertight parts; connected body; fit, colours and backing checked")
    report["source_sha256"] = {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
                               for name in ("xmas_orn_case.scad", "xmas_orn_outers_v2.scad")}
    destination = ROOT / "docs/outers_v2_validation.json"
    destination.write_text(json.dumps(report, indent=2) + "\n")
    print(destination)


if __name__ == "__main__":
    main()
