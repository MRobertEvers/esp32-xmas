#!/usr/bin/env python3
"""JLCPCB production files for xmas_orn / xmas_hub, via Fabrication Toolkit.

    KPY=/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3
    $KPY tools/jlc_export.py xmas_orn      # -> board/xmas_orn/production/rev2/JLCPCB_xmas_orn_rev2_{.zip,_bom.csv,_positions.csv}
    $KPY tools/jlc_export.py xmas_hub      # -> board/xmas_hub/production/JLCPCB_xmas_hub_{.zip,_bom.csv,_positions.csv}

JLCPCB places ITS library part at the CPL point and angle. Its footprint can
differ from KiCad's in where zero rotation points, where the origin is, and
which point of a through-hole part counts as the centre; the difference is
per part, not per package. Fabrication Toolkit (bennymeg; the plugin
JLCPCB's own KiCad guide recommends) carries that as fields on the footprint
('FT Rotation Offset', 'FT Position Offset', 'FT Origin') and applies them
at export, plotting the gerbers and the CPL from one origin.

This script writes those fields from the tables below, saves the board, runs
the plugin's command line with KiCad's Python, renames the outputs, merges
BOM lines per part, and prints every polarised part with what it got.
UNVERIFIED means a footprint that has never been checked on JLCPCB's
placement preview: look at it there (tools/jlc_preview_check.py draws
KiCad's pads over their rendering) before saving to the cart, then add it.

Every value in the tables was checked that way on order W2026090908054344
lines Y13 (ornament, SMT026091060371) and Y14 (hub, SMT026091060380),
2026-09-10/11. The plugin's own --autoTranslate table is NOT used: it says
SOT-23-6 +180, and the TPS54302s here need +270.
"""
import csv
import os
import shutil
import subprocess
import sys

import pcbnew  # KiCad's bundled Python only

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
FT_PLUGIN_DIR = os.path.expanduser("~/Documents/KiCad/10.0/3rdparty/plugins")
FT_MODULE = "com_github_bennymeg_JLC-Plugin-for-KiCad.cli"

BOARDS = {
    # board: (pcb, output dir, output base name)
    "xmas_orn": ("board/xmas_orn/xmas_orn.kicad_pcb", "board/xmas_orn/production/rev2", "JLCPCB_xmas_orn_rev2"),
    "xmas_hub": ("board/xmas_hub/xmas_hub.kicad_pcb", "board/xmas_hub/production", "JLCPCB_xmas_hub"),
}

# Degrees added to the plugin's base angle (KiCad's angle on the top side,
# 180 - KiCad's on the bottom). Both boards are top-side only.
FT_ROTATION_OFFSET = {
    "PinSocket_1x07_P2.54mm_Vertical": 270,  # C22438157, ornament J1 (Y13): KiCad has it end-on
    "SOT-23-5": 270,                         # TLV75733 (ornament U2, Y13)
    "SOT-23-6": 270,                         # TPS54302 C311983 (hub U1-U4, Y14); same value as the
                                             #   MT3608 on the t31 boost board
    "TO-252-2": 180,                         # AOD4185 C400894 (hub Q1, Y14): tab away from the legs
    "TerminalBlock_Phoenix_MKDS-1,5-2-5.08_1x02_P5.08mm_Horizontal": 180,  # KF301 C474881 (hub J2-J5, Y14)
}
# Polarised footprints whose base angle was verified correct as is.
FT_VERIFIED_ZERO = {
    "ESP32-S3-WROOM-1",                      # C2913202 (ornament U1, Y13) -- but see the position offset
    "USB_C_Receptacle_HRO_TYPE-C-31-M-12",   # C165948 (ornament J3, Y13)
    "SW_SPST_B3U-1000P",                     # C231329 (ornament SW1/SW2, Y13)
    "D_SOD-123",                             # B5819W C8598 (ornament D3/D4, Y13)
    "LED_0603_1608Metric",                   # C2286 / C12624 (both boards)
    "D_SMA",                                 # SMAJ15A C148216 (hub D1, Y14)
    "DC-005_BarrelJack_5.5x2.1mm_Horizontal",  # C16214 (hub J1, Y14)
    "CP_Radial_D6.3mm_P2.50mm",              # C44601 (hub C1, Y14)
}
# 'FT Position Offset', mm in the footprint's own frame with y up; the plugin
# rotates it with the part. JLCPCB's WROOM-1 origin is the centre of the pad
# area, 3.62 mm from KiCad's body-centre origin toward the end pad row. The
# CPL uploaded for Y13 had the KiCad origin and the module rendered 3.6 mm
# into its antenna with the end pads bare; JLCPCB's engineer moved it.
FT_POSITION_OFFSET = {
    "ESP32-S3-WROOM-1": "0,-3.62",
}
# 'FT Origin': the plugin reports the pad-bounding-box centre for through-hole
# parts by default. For these JLCPCB wants the KiCad anchor (verified Y14);
# for the ornament's 1x7 socket the centre is right (verified Y13).
FT_ORIGIN = {
    "TerminalBlock_Phoenix_MKDS-1,5-2-5.08_1x02_P5.08mm_Horizontal": "Anchor",
    "DC-005_BarrelJack_5.5x2.1mm_Horizontal": "Anchor",
    "CP_Radial_D6.3mm_P2.50mm": "Anchor",
}
SYMMETRIC_PREFIXES = ("R_", "C_", "L_", "Fuse_", "TestPoint", "MountingHole")
# LCSC numbers the order was actually placed with, for footprints that have no
# 'LCSC Part #' field of their own (the ornament is hand-drawn and carries
# MPNs; a footprint that already has the field keeps it). JLCPCB's matcher
# fails on hyphenated MPNs, so these go onto the footprint as the field the
# plugin reads. Put them in the schematic too when it is next edited. The hub
# carries its own numbers; note that order Y14 was built with Q1 = AOD4185
# (C400894) because the design's AOD417 (C131415) was out of stock.
LCSC_BY_REF = {
    "xmas_orn": {"C1": "CL21A106KOQNNNE", "C2": "CL21A106KOQNNNE", "C3": "CL10A105KA8NNNC",
                 "D1": "C2286", "D2": "C2286", "D3": "C8598", "D4": "C8598",
                 "J1": "C22438157", "J3": "C165948",
                 "R1": "RC0603FR-075K1L", "R2": "RC0603FR-075K1L", "R3": "AC0603JR-0710KL", "R4": "AC0603JR-0710KL",
                 "R5": "RC0603JR-072KL", "R6": "RC0603FR-07470RL",
                 "SW1": "C231329", "SW2": "C231329", "U1": "C2913202", "U2": "TLV75733PDBVR-MS"},
    "xmas_hub": {},
}
# Not populated: excluded from BOM and CPL (the plugin honours KiCad's DNP flag).
DNP = {"xmas_orn": {"J2"}, "xmas_hub": set()}


def set_ft_fields(fp, board_name):
    """Write the plugin's fields onto one footprint, hidden; idempotent."""
    name = str(fp.GetFPID().GetLibItemName())
    ref = fp.GetReference()
    if ref in LCSC_BY_REF[board_name] and not (fp.HasField("LCSC Part #") and fp.GetFieldText("LCSC Part #").strip()):
        fp.SetField("LCSC Part #", LCSC_BY_REF[board_name][ref])
        fp.GetField("LCSC Part #").SetVisible(False)
    fp.SetDNP(ref in DNP[board_name])
    for fname, val in (("FT Rotation Offset", FT_ROTATION_OFFSET.get(name)),
                       ("FT Position Offset", FT_POSITION_OFFSET.get(name)),
                       ("FT Origin", FT_ORIGIN.get(name))):
        if val is None:
            if fp.HasField(fname):  # KiCad 10 API
                fp.Remove(fp.GetField(fname))
            continue
        fp.SetField(fname, str(val))
        fp.GetField(fname).SetVisible(False)


def refkey(r):
    head = r.rstrip("0123456789")
    return (head, int(r[len(head):] or 0))


def merge_bom(src, dst):
    """One line per (part, footprint); holes and test points dropped."""
    rows = {}
    with open(src, encoding="utf-8-sig", newline="") as f:
        for r in csv.DictReader(f):
            refs = [d.strip() for d in r["Designator"].split(",")]
            if not r["LCSC Part #"] and all(d.startswith(("H", "TP")) for d in refs):
                continue
            row = rows.setdefault((r["LCSC Part #"], r["Footprint"]), {"refs": [], "values": [], "qty": 0})
            row["refs"] += refs
            if r["Value"] not in row["values"]:
                row["values"].append(r["Value"])
            row["qty"] += int(r["Quantity"])
    with open(dst, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Comment", "Designator", "Footprint", "Quantity", "LCSC Part #"])
        for (part, fpn), row in sorted(rows.items(), key=lambda kv: refkey(sorted(kv[1]["refs"], key=refkey)[0])):
            w.writerow([" / ".join(sorted(row["values"])), ",".join(sorted(row["refs"], key=refkey)), fpn, row["qty"], part])
    return len(rows)


def main(board_name):
    pcb_rel, out_rel, base = BOARDS[board_name]
    pcb = os.path.join(ROOT, pcb_rel)
    out = os.path.join(ROOT, out_rel)
    prod = os.path.join(os.path.dirname(pcb), "production")   # where the plugin writes
    board = pcbnew.LoadBoard(pcb)
    report = []
    for fp in board.GetFootprints():
        set_ft_fields(fp, board_name)
        ref, name = fp.GetReference(), str(fp.GetFPID().GetLibItemName())
        if name.startswith(SYMMETRIC_PREFIXES) or ref.startswith(("H", "TP")):
            continue
        state = (f"rotation offset {FT_ROTATION_OFFSET[name]}" if name in FT_ROTATION_OFFSET else
                 "verified at 0" if name in FT_VERIFIED_ZERO else "UNVERIFIED -- check in JLCPCB's preview")
        if name in FT_POSITION_OFFSET:
            state += f", position offset {FT_POSITION_OFFSET[name]}"
        if name in FT_ORIGIN:
            state += f", origin {FT_ORIGIN[name]}"
        if fp.IsDNP():
            state += " (DNP, excluded)"
        report.append((ref, name, state))
    pcbnew.SaveBoard(pcb, board)

    if not os.path.isdir(os.path.join(FT_PLUGIN_DIR, FT_MODULE.split(".")[0])):
        raise SystemExit("Fabrication Toolkit is not installed: KiCad -> Plugin and Content Manager -> "
                         "'Fabrication Toolkit' -> Install (or copy bennymeg/Fabrication-Toolkit/plugins to "
                         + os.path.join(FT_PLUGIN_DIR, FT_MODULE.split(".")[0]) + ")")
    os.makedirs(prod, exist_ok=True)
    os.makedirs(out, exist_ok=True)
    stem = os.path.splitext(os.path.basename(pcb))[0]
    for f in ("bom.csv", "positions.csv", "designators.csv", stem + ".zip", stem + "_2.zip"):
        if os.path.exists(os.path.join(prod, f)):
            os.remove(os.path.join(prod, f))
    keep_netlist = os.path.exists(os.path.join(prod, "netlist.ipc"))  # rev-1 artefact in the ornament's production/
    r = subprocess.run([sys.executable, "-m", FT_MODULE, "-p", pcb, "--noBackup", "--nonInteractive", "--excludeDNP"],
                       cwd=FT_PLUGIN_DIR, capture_output=True, text=True)
    if r.returncode != 0 or not os.path.exists(os.path.join(prod, "positions.csv")):
        print(r.stdout[-2000:], r.stderr[-2000:])
        raise SystemExit("Fabrication Toolkit failed")
    shutil.rmtree(os.path.join(prod, "backups"), ignore_errors=True)
    for f in ("designators.csv",) + (() if keep_netlist else ("netlist.ipc",)):
        if os.path.exists(os.path.join(prod, f)):
            os.remove(os.path.join(prod, f))
    import glob
    zips = sorted(glob.glob(os.path.join(prod, stem + "*.zip")), key=os.path.getmtime)
    if not zips:
        raise SystemExit("Fabrication Toolkit wrote no gerber zip in " + prod)
    shutil.move(zips[-1], os.path.join(out, base + ".zip"))  # the plugin suffixes _2 if the name exists
    shutil.move(os.path.join(prod, "positions.csv"), os.path.join(out, base + "_positions.csv"))
    n = merge_bom(os.path.join(prod, "bom.csv"), os.path.join(out, base + "_bom.csv"))
    os.remove(os.path.join(prod, "bom.csv"))

    placements = sum(1 for _ in open(os.path.join(out, base + "_positions.csv"))) - 1
    print(f"{board_name}: {n} BOM lines, {placements} placements -> {os.path.relpath(out, ROOT)}/{base}_*")
    for ref, name, state in sorted(report, key=lambda r: refkey(r[0])):
        print(f"  {ref:4} {name[:52]:52} {state}")
    if any("UNVERIFIED" in r[2] for r in report):
        print("UNVERIFIED parts above: look at each in JLCPCB's Component Placements preview before ordering.")


if __name__ == "__main__":
    if len(sys.argv) != 2 or sys.argv[1] not in BOARDS:
        raise SystemExit("usage: jlc_export.py xmas_orn|xmas_hub")
    main(sys.argv[1])
    sys.stdout.flush()
    os._exit(0)  # pcbnew's interpreter teardown can segfault after a save; the work is done
