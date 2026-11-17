#!/usr/bin/env python3
"""Overlay KiCad pad outlines on a JLCPCB part-placement preview image.

JLCPCB renders *its* model of every part at the CPL position and angle. The
only way to know that a part will be built where KiCad thinks it is, is to
put KiCad's pads on top of that rendering and look. This script does that
for one side of one board and writes crops of the parts you name.

Where the preview comes from
    Order History -> (SMT line) Product Details -> Part Placement. Right-click
    the Top / Bottom image, "Open image in new tab", save it. The URL looks
    like /smtOrder/downloadSmtDFMPatchImg?fileType=pasterImg&layer=top&uuid=…
    Make sure the dialog belongs to the line you are checking: the page keeps
    one dialog per line, old lines included.

Usage
    KPY=/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3
    $KPY board/tools/jlc_preview_check.py board/wifi_bridge/wifi_bridge.kicad_pcb \
        Y12_bridge_bottom.png bottom --refs U1 U2 T1 D1 D2 --out /tmp/check

Output
    <out>/overlay.png     the whole preview with red pad outlines (cyan = pad 1,
                          yellow ring = footprint origin)
    <out>/crop_<REF>.png  one crop per requested ref, 2x when small

How to read it
    Every red rectangle must have the model's lead or castellation on it, and
    the model's pin-1 mark (dot, band, "-" glyph) must be on the cyan pad.
    A model that is offset from its pads by a constant is JLCPCB's footprint
    origin disagreeing with KiCad's (ESP32-S3-WROOM-1: 3.62 mm); a model that
    is rotated is a missing entry in the rotation-offset tables in gen_pcb.py.

The transform is derived from the board outline: the Edge.Cuts bounding box
is mapped onto the bounding box of green pixels in the image. Bottom images
are mirrored left-right, as JLCPCB draws them. It needs Pillow, which
KiCad's bundled Python has.
"""
import argparse
import json
import math
import os
import re
import sys

from PIL import Image, ImageDraw


def edge_bbox(text):
    xs, ys = [], []
    for m in re.finditer(r'\((?:gr_line|gr_arc|gr_rect|gr_circle|gr_poly)(.*?)\n\t\)', text, re.S):
        blk = m.group(1)
        if 'Edge.Cuts' not in blk:
            continue
        for x, y in re.findall(r'\((?:start|end|mid|xy) ([-\d.]+) ([-\d.]+)\)', blk):
            xs.append(float(x))
            ys.append(float(y))
        c = re.search(r'\(center ([-\d.]+) ([-\d.]+)\)\s*\(end ([-\d.]+) ([-\d.]+)\)', blk)
        if c:
            cx, cy, ex, ey = map(float, c.groups())
            r = math.hypot(ex - cx, ey - cy)
            xs += [cx - r, cx + r]
            ys += [cy - r, cy + r]
    if not xs:
        sys.exit("no Edge.Cuts found")
    return min(xs), min(ys), max(xs), max(ys)


def footprints(text):
    out = {}
    for m in re.finditer(r'\n\t\(footprint "([^"]+)"(.*?)\n\t\)', text, re.S):
        blk = m.group(2)
        ref = re.search(r'\(property "Reference" "([^"]+)"', blk).group(1)
        at = re.search(r'\n\t\t\(at ([-\d.]+) ([-\d.]+)(?: ([-\d.]+))?\)', blk)
        fx, fy, frot = float(at.group(1)), float(at.group(2)), float(at.group(3) or 0)
        flipped = '(layer "B.Cu")' in blk[:200]
        a = math.radians(frot)
        pads = []
        for num, x, y, rot, w, h in re.findall(
                r'\(pad "([^"]*)" \w+ \w+\s*\(at ([-\d.]+) ([-\d.]+)(?: ([-\d.]+))?\)\s*\(size ([-\d.]+) ([-\d.]+)\)', blk):
            x, y, w, h = float(x), float(y), float(w), float(h)
            # pad coordinates in the file are footprint-local; KiCad rotates
            # counter-clockwise on a y-down screen
            bx = fx + x * math.cos(a) + y * math.sin(a)
            by = fy - x * math.sin(a) + y * math.cos(a)
            pads.append((num, bx, by, w, h, float(rot or 0)))
        out[ref] = dict(fp=m.group(1), x=fx, y=fy, rot=frot, flipped=flipped, pads=pads)
    return out


def green_bbox(im):
    px = im.load()
    w, h = im.size
    xs, ys = [], []
    for y in range(0, h, 2):
        for x in range(0, w, 2):
            r, g, b = px[x, y]
            if g > 60 and g > r + 25 and g > b + 25:
                xs.append(x)
                ys.append(y)
    if not xs:
        sys.exit("no green board pixels found in the image")
    return min(xs), min(ys), max(xs), max(ys)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("pcb")
    ap.add_argument("image")
    ap.add_argument("side", choices=["top", "bottom"])
    ap.add_argument("--refs", nargs="*", default=None, help="refs to crop (default: every part on that side)")
    ap.add_argument("--out", default="jlc_check")
    args = ap.parse_args()

    text = open(args.pcb).read()
    ex0, ey0, ex1, ey1 = edge_bbox(text)
    fps = footprints(text)
    mirror = args.side == "bottom"

    im = Image.open(args.image).convert("RGB")
    bb = green_bbox(im)
    sx = (bb[2] - bb[0]) / (ex1 - ex0)
    sy = (bb[3] - bb[1]) / (ey1 - ey0)
    if abs(sx - sy) / sx > 0.03:
        print("warning: x and y scales differ (%.2f vs %.2f px/mm); a part may be overhanging a corner" % (sx, sy))
    s = (sx + sy) / 2
    print("board %.1f x %.1f mm, image %d x %d px, %.2f px/mm" % (ex1 - ex0, ey1 - ey0, im.size[0], im.size[1], s))

    def T(x, y):
        X = bb[0] + (ex1 - x) * s if mirror else bb[0] + (x - ex0) * s
        return X, bb[1] + (y - ey0) * s

    refs = args.refs or [r for r, f in fps.items() if f["flipped"] == mirror and f["pads"]]
    os.makedirs(args.out, exist_ok=True)
    d = ImageDraw.Draw(im)
    for ref in refs:
        f = fps.get(ref)
        if not f:
            print("no footprint", ref)
            continue
        if f["flipped"] != mirror:
            print("note: %s is on the other side" % ref)
        for num, x, y, w, h, prot in f["pads"]:
            a = math.radians(prot)
            cx, cy = T(x, y)
            pts = []
            for dx, dy in [(-w / 2, -h / 2), (w / 2, -h / 2), (w / 2, h / 2), (-w / 2, h / 2)]:
                rx = dx * math.cos(a) + dy * math.sin(a)
                ry = -dx * math.sin(a) + dy * math.cos(a)
                if mirror:
                    rx = -rx
                pts.append((cx + rx * s, cy + ry * s))
            d.polygon(pts, outline=(0, 255, 255) if num == "1" else (255, 0, 0))
        cx, cy = T(f["x"], f["y"])
        d.ellipse([cx - 4, cy - 4, cx + 4, cy + 4], outline=(255, 255, 0), width=2)
    im.save(os.path.join(args.out, "overlay.png"))

    for ref in refs:
        f = fps.get(ref)
        if not f or not f["pads"]:
            continue
        xs = [T(p[1], p[2])[0] for p in f["pads"]]
        ys = [T(p[1], p[2])[1] for p in f["pads"]]
        m = max(60, int(0.35 * max(max(xs) - min(xs), max(ys) - min(ys))))
        box = (int(min(xs) - m), int(min(ys) - m), int(max(xs) + m), int(max(ys) + m))
        c = im.crop(box)
        if c.size[0] < 500:
            c = c.resize((c.size[0] * 2, c.size[1] * 2))
        c.save(os.path.join(args.out, "crop_%s.png" % ref))
    print("wrote", args.out, "overlay.png and", len(refs), "crops")


if __name__ == "__main__":
    main()
