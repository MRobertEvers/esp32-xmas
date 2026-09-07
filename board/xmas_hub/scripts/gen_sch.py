#!/usr/bin/env python3
"""Generate board/xmas_hub/xmas_hub.kicad_sch from scratch.

One-shot bootstrap generator: it lays out the schematic programmatically using
symbols pulled from the KiCad 10 standard libraries.  Once the schematic has
been hand-edited in KiCad this script should not be re-run (it overwrites).

Topology: 12 V barrel -> PTC -> TVS -> P-FET reverse protection -> +12V rail
-> four independent TPS54302 5 V / 3 A bucks -> per-arm PTC + LED -> screw
terminal.
"""
import os, sys, uuid
sys.path.insert(0, os.path.dirname(__file__))
from kisch import parse, find, find1, extract_symbol_text, symbol_pins, pin_pos

KICAD_SYM = "/Applications/KiCad/KiCad.app/Contents/SharedSupport/symbols"
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(os.path.dirname(HERE), "xmas_hub.kicad_sch")
PROJECT = "xmas_hub"
ROOT_UUID = "5a1c3e4e-8b9d-4f1a-9c2e-0d6b7a8f9e10"
NS = uuid.UUID(ROOT_UUID)

def uid(*key):
    return str(uuid.uuid5(NS, "|".join(str(k) for k in key)))

# ---------------------------------------------------------------- lib symbols
LIBSYMS = {}      # "Lib:Name" -> raw text with renamed header
PINS = {}         # "Lib:Name" -> [(num, name, x, y, rot, len, etype)]

def load_symbol(lib_id):
    if lib_id in LIBSYMS:
        return
    lib, name = lib_id.split(":")
    txt = extract_symbol_text(os.path.join(KICAD_SYM, lib + ".kicad_sym"), name)
    assert "(extends" not in txt, lib_id
    head = '(symbol "%s"' % name
    assert txt.startswith(head)
    LIBSYMS[lib_id] = '(symbol "%s"' % lib_id + txt[len(head):]
    PINS[lib_id] = symbol_pins(txt)

# ---------------------------------------------------------------- collectors
symbols = []     # emitted symbol s-expr strings
wires = []       # (x1,y1,x2,y2)
labels = []      # (name, x, y)
noconns = []     # (x, y)
texts = []       # (text, x, y, size)
pinpoints = []   # every pin connection point (for junction inference)
refcount = {}

def fmt(v):
    s = "%.4f" % v
    s = s.rstrip("0").rstrip(".")
    return s if s not in ("-0", "") else "0"

def prop(name, value, x, y, hide=False, justify=None, size=1.27, angle=0):
    j = "\n\t\t\t\t(justify %s)" % justify if justify else ""
    return (
        '\t\t(property "%s" "%s"\n\t\t\t(at %s %s %s)\n%s\t\t\t(show_name no)\n\t\t\t(do_not_autoplace no)\n'
        '\t\t\t(effects\n\t\t\t\t(font\n\t\t\t\t\t(size %s %s)\n\t\t\t\t)%s\n\t\t\t)\n\t\t)\n'
        % (name, value.replace('"', '\\"'), fmt(x), fmt(y), fmt(angle), "\t\t\t(hide yes)\n" if hide else "", fmt(size), fmt(size), j)
    )

class Part:
    """A placed symbol.  .pin(number_or_name) -> (x, y) in sheet coords."""
    def __init__(self, lib_id, ref, value, x, y, rot=0, mirror=None, footprint="", datasheet="",
                 description="", mpn="", lcsc="", ref_off=(2.54, -1.27), val_off=(2.54, 1.27),
                 justify="left", in_bom=True, on_board=True, dnp=False):
        load_symbol(lib_id)
        self.lib_id, self.ref, self.x, self.y, self.rot, self.mirror = lib_id, ref, x, y, rot, mirror
        self.pins = {}
        for (num, name, px, py, prot, ln, et) in PINS[lib_id]:
            p = pin_pos(x, y, rot, mirror, px, py)
            self.pins[num] = p
            if name and name not in self.pins:
                self.pins[name] = p
            pinpoints.append(p)
        power = ref.startswith("#")
        ang = 90 if rot % 180 == 90 else 0
        props = prop("Reference", ref, x + ref_off[0], y + ref_off[1], hide=power, justify=justify, angle=ang)
        props += prop("Value", value, x + val_off[0], y + val_off[1], hide=(power and lib_id == "power:PWR_FLAG"), justify=justify, angle=ang)
        props += prop("Footprint", footprint, x, y, hide=True)
        props += prop("Datasheet", datasheet, x, y, hide=True)
        props += prop("Description", description, x, y, hide=True)
        if mpn:
            props += prop("MPN", mpn, x, y, hide=True)
        if lcsc:
            props += prop("LCSC Part #", lcsc, x, y, hide=True)
        pins = ""
        for (num, name, px, py, prot, ln, et) in PINS[lib_id]:
            pins += '\t\t(pin "%s"\n\t\t\t(uuid "%s")\n\t\t)\n' % (num, uid("pin", ref, num))
        s = (
            '\t(symbol\n\t\t(lib_id "%s")\n\t\t(at %s %s %s)\n%s\t\t(unit 1)\n\t\t(body_style 1)\n'
            '\t\t(exclude_from_sim no)\n\t\t(in_bom %s)\n\t\t(on_board %s)\n\t\t(in_pos_files yes)\n\t\t(dnp %s)\n'
            '\t\t(uuid "%s")\n%s%s'
            '\t\t(instances\n\t\t\t(project "%s"\n\t\t\t\t(path "/%s"\n\t\t\t\t\t(reference "%s")\n\t\t\t\t\t(unit 1)\n\t\t\t\t)\n\t\t\t)\n\t\t)\n\t)\n'
            % (lib_id, fmt(x), fmt(y), fmt(rot), "\t\t(mirror %s)\n" % mirror if mirror else "",
               "yes" if in_bom else "no", "yes" if on_board else "no", "yes" if dnp else "no",
               uid("sym", ref), props, pins, PROJECT, ROOT_UUID, ref)
        )
        symbols.append(s)

    def pin(self, key):
        return self.pins[str(key)]

def wire(*pts):
    """Polyline through the given points (coordinates rounded to 4 dp, zero-length segments dropped)."""
    pts = [(round(p[0], 4), round(p[1], 4)) for p in pts]
    for a, b in zip(pts, pts[1:]):
        if a != b:
            wires.append((a[0], a[1], b[0], b[1]))

def label(name, x, y):
    labels.append((name, round(x, 4), round(y, 4)))

def nc(p):
    noconns.append(p)

def text(t, x, y, size=1.5):
    texts.append((t, x, y, size))

pwr_n = [0]
def power(kind, x, y, rot=0):
    pwr_n[0] += 1
    lib = {"+12V": "power:+12V", "GND": "power:GND", "FLAG": "power:PWR_FLAG"}[kind]
    ref = ("#FLG%02d" if kind == "FLAG" else "#PWR%02d") % pwr_n[0]
    val = "PWR_FLAG" if kind == "FLAG" else kind
    return Part(lib, ref, val, x, y, rot, description="Power symbol", ref_off=(0, 0), val_off=(0, -3.81 if kind != "GND" else 3.81), justify=None)

RES = {"100k": ("0603WAF1003T5E", "C25803"), "13.7k": ("RC0603FR-0713K7L", "C482771"), "1k": ("0603WAF1001T5E", "C21190"), "2.2k": ("0603WAF2201T5E", "C4190")}
CAP = {"100nF": ("CL10B104KB8NNNC", "C1591"), "75pF": ("CC0603JRNPO9BN750", "C282074"), "10uF 25V": ("CL31B106KAHNNNE", "C14860"), "22uF 10V": ("CL21A226MPQNNNE", "C29277")}

def R(ref, val, x, y, rot=0, desc="Resistor"):
    mpn, lcsc = RES[val]
    return Part("Device:R", ref, val, x, y, rot, footprint="Resistor_SMD:R_0603_1608Metric", mpn=mpn, lcsc=lcsc, description=desc + " (0603 1%)")

def C(ref, val, x, y, rot=0, fp="Capacitor_SMD:C_0603_1608Metric", desc="Ceramic capacitor", **kw):
    mpn, lcsc = CAP[val]
    return Part("Device:C", ref, val, x, y, rot, footprint=fp, mpn=mpn, lcsc=lcsc, description=desc, **kw)

# ============================================================ INPUT SECTION
ix, iy = 30.48, 60.96
text("12 V INPUT  -  5.5 x 2.1 mm barrel, centre positive", ix - 12.7, iy - 20.32, 2.0)
text("F1: 5 A PTC.  D1: 15 V TVS (clamps hot-plug transients; forward-biased on reversed supply so F1 trips).\n"
     "Q1: P-FET in the high side, body diode toward the load - drops ~30 mV instead of a Schottky's 0.5 V.\n"
     "Rated for 12 V in only: Q1 Vgs and D1 standoff both assume a 12 V adapter.", ix - 12.7, iy - 12.7, 1.27)

J1 = Part("Connector:Barrel_Jack", "J1", "DC-005 5.5x2.1mm", ix, iy, 0,
          footprint="xmas_hub:DC-005_BarrelJack_5.5x2.1mm_Horizontal",
          datasheet="https://www.lcsc.com/datasheet/C16214.pdf",
          description="DC barrel jack 5.5 x 2.1 mm, DC-005 style, centre positive; pin 1 centre, pin 2 sleeve, pin 3 switch (unused)", mpn="DC-005 (2.0 mm pin)", lcsc="C16214",
          ref_off=(-5.08, -6.35), val_off=(-7.62, 6.35))
F1 = Part("Device:Polyfuse", "F1", "5A PTC 16V", ix + 16.51, iy - 2.54, 90,
          footprint="Fuse:Fuse_2920_7451Metric", description="Resettable PTC fuse, 5 A hold, 16 V, 2920",
          mpn="2920L500/16MR", lcsc="C207091", ref_off=(-1.27, -6.35), val_off=(-6.35, -8.89))
D1 = Part("Device:D_Zener", "D1", "SMAJ15A", ix + 25.4, iy + 1.27, 270,
          footprint="Diode_SMD:D_SMA", description="TVS diode 15 V standoff, 400 W, SMA (unidirectional); cathode to +12V", mpn="SMAJ15A", lcsc="C148216")
Q1 = Part("Transistor_FET:Q_PMOS_GDS", "Q1", "AOD417", ix + 35.56, iy, 90,
          footprint="Package_TO_SOT_SMD:TO-252-2", description="P-MOSFET -30 V, 25 A, 10 mOhm, DPAK; reverse polarity protection", mpn="AOD417", lcsc="C131415",
          ref_off=(-2.54, -6.35), val_off=(-2.54, -8.89))
R1 = R("R1", "100k", ix + 35.56, iy + 8.89, desc="Q1 gate pull-down")
C1 = Part("Device:C_Polarized", "C1", "100uF 25V", ix + 45.72, iy + 1.27, 0,
          footprint="Capacitor_THT:CP_Radial_D6.3mm_P2.50mm", description="Electrolytic bulk capacitor 100 uF 25 V 105C, 6.3 x 11 mm radial, 2.5 mm pitch",
          mpn="GR107V025E11RR0VH4FP0", lcsc="C44601")
R2 = R("R2", "2.2k", ix + 60.96, iy + 1.27, desc="12 V indicator LED series resistor")
D2 = Part("Device:LED", "D2", "LED red", ix + 60.96, iy + 8.89, 90,
          footprint="LED_SMD:LED_0603_1608Metric", description="12 V present indicator, red 0603", mpn="KT-0603R", lcsc="C2286")
TP1 = Part("Connector:TestPoint", "TP1", "+12V", ix + 66.04, iy - 2.54, 0,
           footprint="TestPoint:TestPoint_Pad_D1.5mm", description="Test point", ref_off=(-1.27, -2.54), val_off=(-1.27, -5.08), justify="right", in_bom=False)
TP2 = Part("Connector:TestPoint", "TP2", "GND", ix + 15.24, iy + 12.7, 180,
           footprint="TestPoint:TestPoint_Pad_D1.5mm", description="Test point", ref_off=(1.27, 2.54), val_off=(1.27, 5.08), in_bom=False)
P12 = power("+12V", ix + 71.12, iy - 7.62)
FLG1 = power("FLAG", ix + 68.58, iy - 2.54)
GND_IN = power("GND", ix + 30.48, iy + 15.24)
FLG2 = power("FLAG", ix + 20.32, iy + 12.7, 180)

y_ig = iy + 12.7
wire(J1.pin(1), F1.pin(1)); label("VIN_J", ix + 8.89, iy - 2.54)
wire(F1.pin(2), Q1.pin("D")); label("VIN_F", ix + 21.59, iy - 2.54)
wire(D1.pin("K"), (ix + 25.4, iy - 2.54)); wire(D1.pin("A"), (ix + 25.4, y_ig))
wire(Q1.pin("G"), R1.pin(1)); wire(R1.pin(2), (ix + 35.56, y_ig))
wire(Q1.pin("S"), (ix + 71.12, iy - 2.54), P12.pin(1))
wire(C1.pin(1), (ix + 45.72, iy - 2.54)); wire(C1.pin(2), (ix + 45.72, y_ig))
wire(R2.pin(1), (ix + 60.96, iy - 2.54)); wire(R2.pin(2), D2.pin("A")); wire(D2.pin("K"), (ix + 60.96, y_ig))
wire(J1.pin(2), (ix + 10.16, iy + 2.54), (ix + 10.16, y_ig), (ix + 60.96, y_ig))
wire((ix + 30.48, y_ig), GND_IN.pin(1))

# mounting holes (no pins)
for i in range(4):
    Part("Mechanical:MountingHole", "H%d" % (i + 1), "M3", ix - 12.7 + i * 15.24, iy + 33.02, 0,
         footprint="MountingHole:MountingHole_3.2mm_M3", description="Mounting hole M3", ref_off=(-2.54, -3.81), val_off=(-1.27, 3.81), in_bom=False)
text("Mounting holes", ix - 15.24, iy + 27.94, 1.27)

# ============================================================ BUCK ARMS
def arm(k, ux, uy):
    n = k + 1
    text("ARM %d  -  5 V / 3 A synchronous buck (TPS54302), PTC 2.6 A hold, screw terminal" % n, ux - 38.1, uy - 20.32, 2.0)
    U = Part("Regulator_Switching:TPS54302", "U%d" % n, "TPS54302DDCR", ux, uy, 0,
             footprint="Package_TO_SOT_SMD:SOT-23-6", datasheet="https://www.ti.com/lit/ds/symlink/tps54302.pdf",
             description="4.5-28 V input, 3 A synchronous step-down converter, 400 kHz, SOT-23-6", mpn="TPS54302DDCR", lcsc="C311983",
             ref_off=(-3.81, -11.43), val_off=(-6.35, 12.7))
    P = power("+12V", ux - 33.02, uy - 7.62)
    Cin = C("C%d1" % n, "10uF 25V", ux - 25.4, uy + 1.27, fp="Capacitor_SMD:C_1206_3216Metric", desc="Input decoupling, X7R 25 V 1206")
    Cb = C("C%d2" % n, "100nF", ux + 16.51, uy - 8.89, 90, desc="Bootstrap capacitor, X7R 50 V", ref_off=(-2.54, -3.81), val_off=(-2.54, -6.35))
    L = Part("Device:L", "L%d" % n, "10uH", ux + 24.13, uy, 90, footprint="Inductor_SMD:L_Sunlord_MWSA0804S",
             description="Shielded power inductor 10 uH, Isat 7 A, Irms 5.5 A, 59 mOhm, 8x8x4 mm", mpn="MWSA0804S-100MT", lcsc="C17700171",
             ref_off=(-2.54, -2.54), val_off=(-2.54, -5.08))
    Rt = R("R%d1" % n, "100k", ux + 33.02, uy + 3.81, desc="Feedback divider, top")
    Rb = R("R%d2" % n, "13.7k", ux + 33.02, uy + 11.43, desc="Feedback divider, bottom: Vout = 0.596 V x (1 + 100k/13.7k) = 4.95 V")
    Cff = C("C%d5" % n, "75pF", ux + 40.64, uy + 3.81, desc="Feed-forward capacitor across the top FB resistor, C0G")
    Co1 = C("C%d3" % n, "22uF 10V", ux + 48.26, uy + 3.81, fp="Capacitor_SMD:C_0805_2012Metric", desc="Output capacitor, X5R 10 V 0805")
    Co2 = C("C%d4" % n, "22uF 10V", ux + 55.88, uy + 3.81, fp="Capacitor_SMD:C_0805_2012Metric", desc="Output capacitor, X5R 10 V 0805")
    Fa = Part("Device:Polyfuse", "F%d1" % n, "2.6A PTC", ux + 74.93, uy, 90, footprint="Fuse:Fuse_1812_4532Metric",
              description="Resettable PTC fuse 2.6 A hold, 16 V, 1812", mpn="SMD1812P260TF/16", lcsc="C438899",
              ref_off=(-3.81, -3.81), val_off=(-5.08, -6.35))
    Rl = R("R%d3" % n, "1k", ux + 81.28, uy + 3.81, desc="Arm LED series resistor")
    Dl = Part("Device:LED", "D%d1" % n, "LED green", ux + 81.28, uy + 11.43, 90, footprint="LED_SMD:LED_0603_1608Metric",
              description="Arm live indicator, green 0603", mpn="KT-0603G", lcsc="C12624")
    J = Part("Connector:Screw_Terminal_01x02", "J%d" % (n + 1), "ARM %d 5V" % n, ux + 93.98, uy, 0,
             footprint="TerminalBlock_Phoenix:TerminalBlock_Phoenix_MKDS-1,5-2-5.08_1x02_P5.08mm_Horizontal",
             description="Screw terminal 2-pin 5.08 mm, 5 V arm %d out: pin 1 = +5 V, pin 2 = GND" % n,
             mpn="KF301-5.0-2P", lcsc="C474881", ref_off=(1.27, -5.08), val_off=(-5.08, 6.35))
    G1 = power("GND", ux - 25.4, uy + 17.78)
    G2 = power("GND", ux + 83.82, uy + 17.78)

    yr = uy - 2.54
    yg = uy + 15.24
    # input side
    wire(P.pin(1), (ux - 33.02, yr), U.pin("VIN"))
    wire(Cin.pin(1), (ux - 25.4, yr)); wire(Cin.pin(2), (ux - 25.4, yg))
    nc(U.pin("EN"))
    text("EN floats (internal pull-up)", ux - 23.5, uy + 10.8, 0.9)
    wire(U.pin("GND"), (ux, yg))
    wire((ux - 25.4, yg), (ux + 55.88, yg)); wire((ux - 25.4, yg), G1.pin(1))
    # bootstrap + switch node
    wire(U.pin("BOOT"), (ux + 12.7, yr), (ux + 12.7, uy - 8.89), Cb.pin(1) if Cb.pin(1)[0] < Cb.pin(2)[0] else Cb.pin(2))
    cb_r = Cb.pin(2) if Cb.pin(1)[0] < Cb.pin(2)[0] else Cb.pin(1)
    wire(cb_r, (ux + 20.32, uy))
    lpins = sorted([L.pin(1), L.pin(2)])
    wire(U.pin("SW"), lpins[0]); label("SW%d" % n, ux + 12.7, uy)
    # output node
    wire(lpins[1], (ux + 71.12, uy)); label("5V_%d" % n, ux + 29.21, uy)
    wire(Rt.pin(1), (ux + 33.02, uy)); wire(Rt.pin(2), Rb.pin(1)); wire(Rb.pin(2), (ux + 33.02, yg))
    wire(Cff.pin(1), (ux + 40.64, uy)); wire(Cff.pin(2), (ux + 40.64, uy + 7.62), (ux + 33.02, uy + 7.62))
    wire(U.pin("FB"), (ux + 15.24, uy + 2.54), (ux + 15.24, uy + 7.62), (ux + 33.02, uy + 7.62)); label("FB%d" % n, ux + 16.51, uy + 7.62)
    wire(Co1.pin(1), (ux + 48.26, uy)); wire(Co1.pin(2), (ux + 48.26, yg))
    wire(Co2.pin(1), (ux + 55.88, uy)); wire(Co2.pin(2), (ux + 55.88, yg))
    # arm output
    fpins = sorted([Fa.pin(1), Fa.pin(2)])
    wire((ux + 71.12, uy), fpins[0]); wire(fpins[1], J.pin(1)); label("ARM%d" % n, ux + 79.0, uy)
    wire(Rl.pin(1), (ux + 81.28, uy)); wire(Rl.pin(2), Dl.pin("A")); wire(Dl.pin("K"), (ux + 81.28, yg), (ux + 83.82, yg))
    wire(J.pin(2), (ux + 83.82, uy + 2.54), (ux + 83.82, yg), G2.pin(1))
    text("+5V", ux + 88.9, uy - 1.27, 1.0); text("GND", ux + 88.9, uy + 5.08, 1.0)

arm(0, 149.86, 60.96)
arm(1, 294.64, 60.96)
arm(2, 149.86, 158.75)
arm(3, 294.64, 158.75)

text("xmas_hub - 12 V to 4 x 5 V star power hub for the xmas_orn ornaments.\n"
     "Each arm is its own regulator + fuse: a short on one arm only drops that arm.\n"
     "Arm cables: 20-22 AWG twin lead, 3.5 x 1.3 mm barrel plugs (centre +) into the ornament's DC-002 jack.\n"
     "Total output is limited by the 12 V supply: 12 ornaments at ~0.4 A each is ~25 W, i.e. a 12 V / 3 A adapter.",
     ix - 12.7, iy + 55.88, 1.5)

# ---------------------------------------------------------------- junctions
def infer_junctions():
    from collections import Counter
    ends = Counter()
    for (x1, y1, x2, y2) in wires:
        ends[(x1, y1)] += 1; ends[(x2, y2)] += 1
    for p in pinpoints: ends[p] += 1
    def on_segment(p, w):
        x, y = p; x1, y1, x2, y2 = w
        if x1 == x2 and x == x1 and min(y1, y2) < y < max(y1, y2): return True
        if y1 == y2 and y == y1 and min(x1, x2) < x < max(x1, x2): return True
        return False
    js = set()
    for p, n in ends.items():
        if n >= 3: js.add(p)
        elif any(on_segment(p, w) for w in wires): js.add(p)
    return sorted(js)

junctions = infer_junctions()

# ---------------------------------------------------------------- emit
out = []
out.append('(kicad_sch\n\t(version 20260306)\n\t(generator "eeschema")\n\t(generator_version "10.0")\n\t(uuid "%s")\n\t(paper "A3")\n' % ROOT_UUID)
out.append('\t(title_block\n\t\t(title "xmas_hub - 12 V to 4 x 5 V star power hub")\n\t\t(rev "1")\n\t\t(company "esp32-xmas")\n'
           '\t\t(comment 1 "Feeds up to 4 arms of xmas_orn ornaments; one TPS54302 buck and one PTC per arm")\n\t)\n')
out.append('\t(lib_symbols\n')
for k in sorted(LIBSYMS):
    body = LIBSYMS[k].replace("\n", "\n\t\t")
    out.append('\t\t' + body + '\n')
out.append('\t)\n')
for (x, y) in junctions:
    out.append('\t(junction\n\t\t(at %s %s)\n\t\t(diameter 0)\n\t\t(color 0 0 0 0)\n\t\t(uuid "%s")\n\t)\n' % (fmt(x), fmt(y), uid("junc", x, y)))
for (x, y) in noconns:
    out.append('\t(no_connect\n\t\t(at %s %s)\n\t\t(uuid "%s")\n\t)\n' % (fmt(x), fmt(y), uid("nc", x, y)))
for i, (x1, y1, x2, y2) in enumerate(wires):
    out.append('\t(wire\n\t\t(pts\n\t\t\t(xy %s %s) (xy %s %s)\n\t\t)\n\t\t(stroke\n\t\t\t(width 0)\n\t\t\t(type default)\n\t\t)\n\t\t(uuid "%s")\n\t)\n'
               % (fmt(x1), fmt(y1), fmt(x2), fmt(y2), uid("wire", i, x1, y1, x2, y2)))
for (t, x, y, size) in texts:
    out.append('\t(text "%s"\n\t\t(exclude_from_sim no)\n\t\t(at %s %s 0)\n\t\t(effects\n\t\t\t(font\n\t\t\t\t(size %s %s)\n\t\t\t)\n\t\t\t(justify left bottom)\n\t\t)\n\t\t(uuid "%s")\n\t)\n'
               % (t.replace('"', '\\"').replace("\n", "\\n"), fmt(x), fmt(y), fmt(size), fmt(size), uid("text", x, y)))
for (name, x, y) in labels:
    out.append('\t(label "%s"\n\t\t(at %s %s 0)\n\t\t(effects\n\t\t\t(font\n\t\t\t\t(size 1.27 1.27)\n\t\t\t)\n\t\t\t(justify left bottom)\n\t\t)\n\t\t(uuid "%s")\n\t)\n'
               % (name, fmt(x), fmt(y), uid("label", name, x, y)))
out.extend(symbols)
out.append('\t(sheet_instances\n\t\t(path "/"\n\t\t\t(page "1")\n\t\t)\n\t)\n\t(embedded_fonts no)\n)\n')
open(OUT, "w").write("".join(out))
print("wrote", OUT, "symbols:", len(symbols), "wires:", len(wires), "junctions:", len(junctions))
