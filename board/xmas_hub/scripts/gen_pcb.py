#!/usr/bin/env python3
"""Generate board/xmas_hub/xmas_hub.kicad_pcb with KiCad's bundled pcbnew module.

Run with KiCad's python:
  /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3 scripts/gen_pcb.py

Reads the netlist exported from the schematic (scripts/hub.xml, produced by
`kicad-cli sch export netlist --format kicadxml`), places every footprint,
routes the board and pours the ground planes.  One-shot bootstrap: do not
re-run after hand edits in pcbnew.
"""
import os, sys, xml.etree.ElementTree as ET
import pcbnew
from pcbnew import VECTOR2I, FromMM

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT = os.path.join(ROOT, "xmas_hub.kicad_pcb")
NETLIST = os.path.join(HERE, "hub.xml")
FPLIB = "/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints"

W, H = 80.0, 66.0          # board size, mm
OX, OY = 50.0, 50.0        # board origin on the sheet
YOFF = 4.0                 # global shift of the "design" coordinates inside the board

def P(x, y):
    return VECTOR2I(FromMM(OX + x), FromMM(OY + y + YOFF))

# ------------------------------------------------------------------ netlist
xml = ET.parse(NETLIST).getroot()
comps = {}
for c in xml.find("components"):
    comps[c.get("ref")] = (c.find("footprint").text, c.find("value").text)
fields = {}
for c in xml.find("components"):
    d = {}
    for tag in ("datasheet", "description"):
        e = c.find(tag)
        if e is not None and e.text:
            d[tag.capitalize()] = e.text
    fl = c.find("fields")
    if fl is not None:
        for f in fl:
            if f.get("name") not in ("Footprint",):
                d[f.get("name")] = f.text or ""
    fields[c.get("ref")] = d
netof = {}   # (ref, pin) -> net name
for net in xml.find("nets"):
    for n in net:
        netof[(n.get("ref"), n.get("pin"))] = net.get("name")

board = pcbnew.CreateEmptyBoard()
nets = {}
def net(name):
    if name not in nets:
        n = pcbnew.NETINFO_ITEM(board, name)
        board.Add(n)
        nets[name] = n
    return nets[name]
for name in sorted(set(netof.values())):
    net(name)

fps = {}
def place(ref, x, y, rot=0, back=False):
    fpid, value = comps[ref]
    lib, name = fpid.split(":")
    libdir = os.path.join(ROOT, lib + ".pretty") if lib == "xmas_hub" else os.path.join(FPLIB, lib + ".pretty")
    fp = pcbnew.FootprintLoad(libdir, name)
    assert fp is not None, fpid
    fp.SetReference(ref)
    fp.SetValue(value)
    fp.SetFPIDAsString(fpid)
    board.Add(fp)
    if back:
        fp.Flip(P(x, y), False)
    fp.SetPosition(P(x, y))
    fp.SetOrientationDegrees(rot)
    for pad in fp.Pads():
        nn = netof.get((ref, pad.GetNumber()))
        if nn:
            pad.SetNet(net(nn))
    # hide values on silk, keep references small
    fp.Value().SetVisible(False)
    fp.Reference().SetLayer(pcbnew.F_Fab)
    fp.Reference().SetTextSize(VECTOR2I(FromMM(0.8), FromMM(0.8)))
    fp.Reference().SetTextThickness(FromMM(0.12))
    std = {"Datasheet": pcbnew.FIELD_T_DATASHEET, "Description": pcbnew.FIELD_T_DESCRIPTION}
    for fname, fval in fields.get(ref, {}).items():
        if fname in std:
            fp.GetField(std[fname]).SetText(fval)
        else:
            fp.SetField(fname, fval)
    for f in fp.GetFields():
        if f.GetId() not in (pcbnew.FIELD_T_REFERENCE,):
            f.SetVisible(False)
    fps[ref] = fp
    return fp

def padpos(ref, num):
    p = fps[ref].FindPadByNumber(str(num)).GetPosition()
    return (pcbnew.ToMM(p.x) - OX, pcbnew.ToMM(p.y) - OY - YOFF)

def track(netname, pts, width, layer=pcbnew.F_Cu):
    for a, b in zip(pts, pts[1:]):
        t = pcbnew.PCB_TRACK(board)
        t.SetStart(P(*a)); t.SetEnd(P(*b))
        t.SetWidth(FromMM(width)); t.SetLayer(layer); t.SetNet(net(netname))
        board.Add(t)

def via(netname, x, y, dia=0.6, drill=0.3):
    v = pcbnew.PCB_VIA(board)
    v.SetPosition(P(x, y)); v.SetDrill(FromMM(drill)); v.SetWidth(pcbnew.F_Cu, FromMM(dia))
    v.SetViaType(pcbnew.VIATYPE_THROUGH); v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu); v.SetNet(net(netname))
    board.Add(v)

def silk(text, x, y, size=1.0, rot=0, layer=pcbnew.F_SilkS, bold=False, left=False):
    t = pcbnew.PCB_TEXT(board)
    t.SetText(text); t.SetPosition(P(x, y)); t.SetLayer(layer)
    if left:
        t.SetHorizJustify(pcbnew.GR_TEXT_H_ALIGN_LEFT)
    t.SetTextSize(VECTOR2I(FromMM(size), FromMM(size))); t.SetTextThickness(FromMM(size * 0.15))
    t.SetTextAngleDegrees(rot); t.SetBold(bold)
    if layer == pcbnew.B_SilkS:
        t.SetMirrored(True)
    board.Add(t)

def edge_rect():
    r = 2.0  # corner radius
    def line(a, b):
        s = pcbnew.PCB_SHAPE(board); s.SetShape(pcbnew.SHAPE_T_SEGMENT)
        s.SetStart(VECTOR2I(FromMM(OX + a[0]), FromMM(OY + a[1]))); s.SetEnd(VECTOR2I(FromMM(OX + b[0]), FromMM(OY + b[1])))
        s.SetLayer(pcbnew.Edge_Cuts); s.SetWidth(FromMM(0.1)); board.Add(s)
    def arc(center, start, end):
        s = pcbnew.PCB_SHAPE(board); s.SetShape(pcbnew.SHAPE_T_ARC)
        s.SetArcGeometry(VECTOR2I(FromMM(OX + start[0]), FromMM(OY + start[1])),
                         VECTOR2I(FromMM(OX + center[0] + (start[0] - center[0]) * 0.7071 + (end[0] - center[0]) * 0.7071),
                                  FromMM(OY + center[1] + (start[1] - center[1]) * 0.7071 + (end[1] - center[1]) * 0.7071)),
                         VECTOR2I(FromMM(OX + end[0]), FromMM(OY + end[1])))
        s.SetLayer(pcbnew.Edge_Cuts); s.SetWidth(FromMM(0.1)); board.Add(s)
    line((r, 0), (W - r, 0)); line((W, r), (W, H - r)); line((W - r, H), (r, H)); line((0, H - r), (0, r))
    arc((W - r, r), (W - r, 0), (W, r)); arc((W - r, H - r), (W, H - r), (W - r, H))
    arc((r, H - r), (r, H), (0, H - r)); arc((r, r), (0, r), (r, 0))

def zone(netname, layer, inset=0.5, clearance=0.3, priority=0):
    z = pcbnew.ZONE(board)
    z.SetLayer(layer); z.SetNet(net(netname))
    o = z.Outline(); o.NewOutline()
    for (x, y) in [(inset, inset), (W - inset, inset), (W - inset, H - inset), (inset, H - inset)]:
        o.Append(FromMM(OX + x), FromMM(OY + y))
    z.SetLocalClearance(FromMM(clearance)); z.SetMinThickness(FromMM(0.25))
    z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
    z.SetThermalReliefGap(FromMM(0.4)); z.SetThermalReliefSpokeWidth(FromMM(0.5))
    z.SetAssignedPriority(priority); z.SetZoneName("%s_%s" % (netname, "F" if layer == pcbnew.F_Cu else "B"))
    board.Add(z)
    return z

# ================================================================== placement
edge_rect()

# --- input section ---------------------------------------------------------
place("J1", 14.0, 10.0, 0)
place("F1", 24.5, 5.5, 0)
place("D1", 25.0, 12.4, 270)
place("Q1", 24.0, 21.0, 90)
place("R1", 21.72, 29.0, 270)
place("C1", 29.5, 36.0, 180)
place("R2", 30.5, 44.0, 180)
place("D2", 26.5, 44.0, 0)
place("TP1", 30.5, 48.5, 0)
place("TP2", 26.5, 48.5, 0)
for i, (x, y) in enumerate([(3.5, -0.5), (3.5, 58.5), (74.5, -0.5), (74.5, 58.5)]):
    place("H%d" % (i + 1), x, y, 0)

BUS_X = 34.5
# --- arms ------------------------------------------------------------------
ROWS = [9.0, 22.0, 35.0, 48.0]
for k, cy in enumerate(ROWS):
    n = k + 1
    place("U%d" % n, 44.0, cy, 180)
    place("C%d1" % n, 43.0, cy - 4.2, 90)         # 10u input, vertical: pad1 VIN (bottom) pad2 GND (top)
    place("C%d2" % n, 42.0, cy + 4.2, 180)        # boot cap, pad1 BOOT (right), pad2 SW (left)
    place("R%d1" % n, 39.5, cy - 2.0, 0)          # 100k: pad1 VOUT sense, pad2 FB
    place("C%d5" % n, 39.5, cy, 0)                # 75p: pad1 VOUT sense, pad2 FB
    place("R%d2" % n, 39.5, cy + 2.0, 180)        # 13.3k: pad1 FB (right), pad2 GND (left)
    place("L%d" % n, 52.0, cy, 0)                 # pad1 SW, pad2 VOUT
    place("C%d3" % n, 58.7, cy - 2.2, 90)         # 22u: pad1 VOUT (bottom), pad2 GND (top)
    place("C%d4" % n, 58.7, cy + 2.2, 270)        # 22u: pad1 VOUT (top), pad2 GND (bottom)
    place("F%d1" % n, 64.5, cy, 0)                # PTC: pad1 VOUT, pad2 ARM
    place("R%d3" % n, 65.0, cy - 3.8, 90)         # 1k: pad1 ARM (bottom), pad2 LED A (top)
    place("D%d1" % n, 63.0, cy - 3.8, 90)         # LED: pad1 K (bottom), pad2 A (top)
    place("J%d" % (n + 1), 73.0, cy - 2.54, 270)  # terminal: pin1 ARM, pin2 GND

# report a few pad positions / courtyards so the routing assumptions can be checked
def bbox(ref, layer):
    fp = fps[ref]; b = fp.GetCourtyard(layer).BBox()
    return tuple(round(pcbnew.ToMM(v) - o, 2) for v, o in ((b.GetLeft(), OX), (b.GetTop(), OY + YOFF), (b.GetRight(), OX), (b.GetBottom(), OY + YOFF)))
if "--check" in sys.argv:
    for ref in ("U1", "J2", "Q1", "J1", "C11", "C12", "R12", "C13", "C14", "R13", "D11", "D1", "R1", "R2", "D2", "C1", "F1", "L1", "F11"):
        print(ref, {p.GetNumber(): (round(padpos(ref, p.GetNumber())[0], 3), round(padpos(ref, p.GetNumber())[1], 3), netof.get((ref, p.GetNumber()))) for p in fps[ref].Pads() if p.GetNumber()})
        print("   courtyard", bbox(ref, pcbnew.F_CrtYd))
    sys.exit(0)

# ================================================================== routing
# --- input -----------------------------------------------------------------
track("/VIN_J", [padpos("J1", 1), (18.5, 5.5), padpos("F1", 1)], 2.5)
track("/VIN_F", [padpos("F1", 2), (28.5, 6.1), (28.5, 16.0), (26.0, 18.5), (24.0, 19.7)], 2.5)
track("/VIN_F", [padpos("D1", 1), (28.5, padpos("D1", 1)[1])], 1.0)
track("GND", [padpos("D1", 2), (22.5, padpos("D1", 2)[1])], 0.8); via("GND", 22.5, padpos("D1", 2)[1])
track("Net-(Q1-G)", [padpos("Q1", 1), padpos("R1", 1)], 0.3)
track("GND", [padpos("R1", 2), (21.72, 31.3)], 0.3); via("GND", 21.72, 31.3)
track("+12V", [padpos("Q1", 3), (BUS_X, padpos("Q1", 3)[1])], 3.0)
track("+12V", [padpos("C1", 1), (BUS_X, 36.0)], 2.5)
track("+12V", [padpos("R2", 1), (BUS_X, 44.0)], 0.5)
track("Net-(D2-A)", [padpos("D2", 2), padpos("R2", 2)], 0.3)
track("GND", [padpos("D2", 1), (24.0, 44.0)], 0.3); via("GND", 24.0, 44.0)
track("+12V", [padpos("TP1", 1), (BUS_X, 48.5)], 0.5)
track("GND", [padpos("TP2", 1), (26.5, 50.3)], 0.5); via("GND", 26.5, 50.3)
# 12 V bus
track("+12V", [(BUS_X, ROWS[0] - 3.3), (BUS_X, 48.5)], 3.0)

# --- arms ------------------------------------------------------------------
for k, cy in enumerate(ROWS):
    n = k + 1
    U, C1, C2, R1, C5, R2, L, C3, C4, F, R3, D, J = ("U%d" % n, "C%d1" % n, "C%d2" % n, "R%d1" % n, "C%d5" % n, "R%d2" % n,
                                                     "L%d" % n, "C%d3" % n, "C%d4" % n, "F%d1" % n, "R%d3" % n, "D%d1" % n, "J%d" % (n + 1))
    vin, sw, gnd, fb, boot = padpos(U, 3), padpos(U, 2), padpos(U, 1), padpos(U, 4), padpos(U, 6)
    # 12 V feed -> input cap -> VIN pin
    track("+12V", [(BUS_X, cy - 3.3), (41.8, cy - 3.3), padpos(C1, 1)], 0.9)
    track("+12V", [vin, (44.5, cy - 2.0), padpos(C1, 1)], 0.5)
    track("GND", [padpos(C1, 2), (44.6, cy - 5.7)], 0.6); via("GND", 44.6, cy - 5.7)
    track("GND", [gnd, (gnd[0], cy + 2.7)], 0.5); via("GND", gnd[0], cy + 2.7)
    # switch node
    swn = "/SW%d" % n
    track(swn, [sw, (46.8, cy)], 0.7)
    track(swn, [(46.8, cy), padpos(L, 1)], 1.5)
    # bootstrap
    bn = netof[(U, "6")]
    track(bn, [boot, padpos(C2, 1)], 0.3)
    track(swn, [padpos(C2, 2), (padpos(C2, 2)[0], cy + 5.2), (47.3, cy + 5.2), (48.6, cy + 3.9), padpos(L, 1)], 0.5)
    # feedback divider
    fbn = "/FB%d" % n
    track(fbn, [fb, (41.0, fb[1]), (40.325, cy - 1.6)], 0.25)
    track(fbn, [padpos(R1, 2), padpos(C5, 2), padpos(R2, 1)], 0.25)
    track("GND", [padpos(R2, 2), (38.675, cy + 3.6)], 0.3); via("GND", 38.675, cy + 3.6)
    vout = "/5V_%d" % n
    track(vout, [padpos(R1, 1), padpos(C5, 1), (37.3, cy), (37.3, cy + 6.1), (57.4, cy + 6.1), (57.4, cy + 0.5)], 0.25)
    # output node
    track(vout, [padpos(L, 2), (62.4, cy)], 2.0)
    track("GND", [padpos(C3, 2), (58.7, cy - 4.6)], 0.8); via("GND", 58.7, cy - 4.6)
    track("GND", [padpos(C4, 2), (58.7, cy + 4.6)], 0.8); via("GND", 58.7, cy + 4.6)
    # arm output after PTC
    armn = "/ARM%d" % n
    track(armn, [padpos(F, 2), (70.46, cy), padpos(J, 1)], 2.0)
    track(armn, [padpos(R3, 1), (66.3, cy - 1.6)], 0.3)
    ledn = netof[(D, "2")]
    track(ledn, [padpos(R3, 2), padpos(D, 2)], 0.3)
    track("GND", [padpos(D, 1), (63.9, cy - 1.2)], 0.3); via("GND", 63.9, cy - 1.2)
    # extra ground stitching under the buck
    for (x, y) in [(52.0, cy + 5.2), (52.0, cy - 5.2)]:
        via("GND", x, y)

# stitching vias in the input section
for (x, y) in [(6.0, 20.0), (6.0, 30.0), (6.0, 40.0), (6.0, 50.0), (14.0, 26.0), (14.0, 36.0), (14.0, 46.0), (14.0, 52.5), (30.0, 30.2), (30.0, 52.5), (20.0, 52.5), (40.0, 58.0), (8.0, 60.0)]:
    via("GND", x, y)

# ================================================================== silkscreen
silk("XMAS HUB  12V IN -> 4 x 5V/3A", 8.0, 56.0, 1.2, bold=True, left=True)
silk("esp32-xmas  rev 1", 8.0, 58.2, 0.8, left=True)
silk("12V IN", 9.5, 1.3, 1.0, bold=True)
silk("5.5x2.1 ctr +", 11.0, 3.0, 0.8)
silk("100uF", 28.25, 40.2, 0.8)
silk("12V", 28.5, 42.4, 0.8); silk("+12V", 30.5, 46.9, 0.8); silk("GND", 26.5, 46.9, 0.8)
for k, cy in enumerate(ROWS):
    silk("ARM %d" % (k + 1), 64.0, cy + 3.7, 0.8, bold=True)
    silk("+", 67.0, cy - 2.54, 1.0, bold=True); silk("-", 67.0, cy + 2.54, 1.0, bold=True)
    silk("A%d" % (k + 1), 63.0, cy - 5.8, 0.8)
# back silk
silk("xmas_hub rev 1", 20.0, 28.0, 1.2, layer=pcbnew.B_SilkS)
silk("12V in -> 4 x 5V/3A (TPS54302 per arm)", 20.0, 30.0, 0.9, layer=pcbnew.B_SilkS)
silk("arm cable: 3.5x1.3mm plug, centre +", 20.0, 31.8, 0.9, layer=pcbnew.B_SilkS)

# ================================================================== zones
zone("GND", pcbnew.F_Cu)
zone("GND", pcbnew.B_Cu)
filler = pcbnew.ZONE_FILLER(board)
filler.Fill(board.Zones())

# design settings (mirrors the project file so pcbnew and kicad-cli agree)
ds = board.GetDesignSettings()
ds.m_TrackMinWidth = FromMM(0.2); ds.m_ViasMinSize = FromMM(0.5); ds.m_MinThroughDrill = FromMM(0.3); ds.m_CopperEdgeClearance = FromMM(0.5)
pcbnew.SaveBoard(OUT, board)
print("wrote", OUT, "footprints", len(fps), "tracks", len(board.GetTracks()))
