# ESP32-S3 OSRS Model Renderer

ESP-IDF firmware for an **ESP32-S3** driving a **240×240 ST7789** SPI LCD. It renders an animated Old School RuneScape model in software — no GPU, no PSRAM — using **[toridraw](3rd/oldschool-clientc/3rd/toridraw)** for the raster and **[rscache](3rd/oldschool-clientc/3rd/rscache)** for the cache formats.

The model, its animation, its textures and toridraw's lookup tables are all baked into flash at build time. The device decodes nothing at boot.

Current subject: the Tree Gnome Village spirit tree (dat2 model **2851**) playing `entendseq`, the idle that fades the orbs in its branches.

For experimental relative positioning of four or more ornaments using their
existing Wi-Fi radios, see the [spatial layout module](components/xmas_spatial/README.md)
and [standalone firmware demo](examples/spatial_layout/README.md). The module
implements discovery, FTM ranging and a documented 3D solver without third-party
libraries; it is opt-in and does not start Wi-Fi in the renderer.

---

## Hardware

| LCD pin | GPIO | Notes |
|---------|------|-------|
| SCLK    | 12   | SPI clock — IOMUX pin for SPI2, which is what allows 80 MHz |
| MOSI    | 11   | SPI data — likewise IOMUX |
| CS      | −1   | Tied low on this board |
| DC      | 9    | Data/command |
| RST     | 8    | Reset |
| BLK     | 7    | Backlight, active high, driven by LEDC PWM |
| VCC     | 3.3 V | 3.3 V only |
| GND     | GND  | |

Pins are defined at the top of [`main/main.c`](main/main.c).

**No PSRAM.** It was needed once for toridraw's lookup tables; those are `const` in flash now, and so is the model. Everything left fits in internal DRAM.

### Board (`board/xmas_orn`)

KiCad 10 project.

#### Revision history

| Rev | Date | Module | Power | Outline | Notes |
|-----|------|--------|-------|---------|-------|
| 1 | June 2026 | ESP32-S3-MINI-1-N8 (8 MB flash, no PSRAM) | USB-C only | 27.5 × 39 mm | Ordered as PCBWay SMT quote W1016619ASI9; JLCPCB outputs in `production/` |
| 2 | Sept 2026 | ESP32-S3-WROOM-1-N16R8 (16 MB flash, 8 MB octal PSRAM) | USB-C or 5 V barrel jack, Schottky OR-ed | 27.5 × 41.5 mm | Jack footprint unpopulated; fresh autoroute |

#### Rev 2 changes

- **Module:** ESP32-S3-WROOM-1-N16R8 (16 MB flash, 8 MB octal PSRAM) replaces the
  ESP32-S3-MINI-1-N8. Same GPIOs as above, and none of them are the octal-PSRAM
  pins (35/36/37), so `main.c` is unchanged. The firmware still does not *need*
  PSRAM; `sdkconfig.defaults` keeps the 8 MB flash size so it also boots on rev 1.
  Build with the `sdkconfig.rev2.defaults` overlay for rev 2 boards (see below).
- **Board outline** grew from 27.5 × 39 mm to 27.5 × 41.5 mm; the four mounting
  holes did not move. The module's antenna end overhangs the bottom edge, as the
  MINI-1's did.
- **Power inputs are OR-ed.** USB-C VBUS feeds `VIN` through D3 and the DC jack
  through D4 (B5819W Schottky, SOD-123), so USB and the jack can both be plugged in
  without back-feeding each other. `VIN` goes to the TLV75733 3.3 V LDO as before.
- **J2, DC barrel jack (DC-002, 3.5 × 1.3 mm), not populated.** Footprint and
  3D model live in the project library `xmas_orn.pretty` / `xmas_orn.3dshapes`.
  The jack must be fed **5 V DC** (the LDO's absolute maximum input is 5.5 V);
  the silkscreen says so next to the opening. Pin 1 is the centre pin (`VJACK`),
  pin 2 the sleeve (GND), pin 3 the unused switch contact.
- Copper keep-out rings around the mounting holes; minimum drill lowered to
  0.2 mm for the WROOM-1 thermal vias (within JLCPCB capability).

The routing is a fresh autoroute (Freerouting) that passes DRC with the project
rules, not a hand layout.

Outputs: `board/xmas_orn/docs/` holds the schematic and layer PDFs, a copper/silk
SVG, and 3D renders of both sides
([top](board/xmas_orn/docs/xmas_orn_rev2_top.png),
[bottom](board/xmas_orn/docs/xmas_orn_rev2_bottom.png));
`board/xmas_orn/production/rev2/` holds the JLCPCB gerber/drill zip, BOM and
placement file, exported with `kicad-cli` (J2 excluded as DNP). The placement
rotations are KiCad's raw angles, not the per-package corrections the
Fabrication Toolkit plugin applies, so check the part orientation in JLCPCB's
placement preview before confirming. The files directly under `production/` are
the rev 1 outputs.

Firmware for rev 2: `sdkconfig.rev2.defaults` overlays the 16 MB flash size and
enables the octal PSRAM. The base defaults still build a firmware that runs on
both revisions.

#### Parts on hand for rev 2

The rev 2 part choices follow what was actually bought (from the order emails,
all AliExpress unless noted):

| Part | Qty | Order | Ordered | Used as |
|------|-----|-------|---------|---------|
| ESP32-S3 WROOM-1 family module, N16R8 | 5 | 8212428463925361 | 2026-06-15 | **U1.** Bare module, 16 MB flash + 8 MB PSRAM. The listing photo shows the WROOM-1 shield can; if these turn out to be the 1U (IPEX) variant the same pads apply, only the antenna overhang is unused. |
| DC-002 DC power jack, 3.5 × 1.3 mm, 3-pin | 20 | 8212428463985361 (shipped in package 8212428463965361) | 2026-06-15 | **J2**, not populated by default. Footprint from LCSC C381119 (XKB DC-002-2.0A-1.3); pin 1 centre, 2 sleeve, 3 switch. |
| ESP32-S3-MINI-1-N4R2 | 4 | 8211751284255361 | 2026-06-17 | Not used. Drop-in for the rev 1 MINI-1 footprint (4 MB flash + 2 MB PSRAM) if a rev 1 board is respun instead. |
| ESP32-S3 1.69" touch-screen dev boards | 2 | 8211746880775361 | 2026-06-17 | Not used; dev boards, not modules. |
| ESP-32S (classic ESP32) module set | 1 | 8212349322565361 | 2026-06-17 | Not used; wrong chip family. |
| AYWHP ESP32-S3 dev boards with WROOM-1-N16R8 | 3 | Amazon 114-8171301-3841008 | 2026-06-04 | Not used; dev boards. Handy for bringing up the N16R8 firmware config before rev 2 boards arrive. |
| 3.3 V / 5 V / 12 V power supply module | 1 | 8212428463975361 (same package) | 2026-06-15 | Not on the board. |

#### JLCPCB placement gotchas (learned on order W2026090908054344, 2026-09-08)

- **Rotations.** `kicad-cli` writes KiCad's raw footprint angle, but JLCPCB
  applies that angle to *its own* part model, whose 0° differs per package.
  On the first rev 2 order the 1×7 socket J1 came out vertical and the
  WROOM-1 U1 was flipped end-for-end (antenna on the board, shield hanging
  off the edge). JLCPCB's engineer corrected both before production, and the
  CPL in `production/rev2/` now carries the JLCPCB-correct angles for those
  two parts (J1 = 0, U1 = 0). Any new footprint needs the same check.
- **Always open Order History → DFM Analysis after paying** and tick
  "Show Components": that view is the engineer-adjusted placement that will
  actually be built. Verify every polarised part (diodes, LEDs, ICs, the
  module, connectors) against `docs/xmas_orn_rev2_top.png` before the SMT
  line starts. "Confirm Parts Placement" at order time makes JLCPCB wait for
  your sign-off instead.
- **Through-hole parts must be in the CPL** or JLCPCB skips them: the hub's
  jack, terminal blocks and bulk cap were missing from the SMD-only export.
  `xmas_hub/production/JLCPCB_xmas_hub_positions.csv` now lists all 60 parts
  in the five-column form (Designator, Mid X, Mid Y, Rotation, Layer);
  JLCPCB's parser rejects quoted fields, so keep footprint names out of it.
- **Part numbers.** Give JLCPCB LCSC C-numbers, not manufacturer numbers; its
  matcher fails on hyphenated MPNs. Substitutions made on that order: hub Q1
  AOD417 → AOD4185 (C400894, out of stock); ornament J1 = C22438157,
  J3 = C165948, SW1/SW2 = C231329.

#### Sourcing the rest

Everything new on rev 2 now carries an `LCSC Part #` field in the schematic and
board so an assembler can pick it up directly:

| Ref | Part | LCSC / JLCPCB | Status |
|-----|------|---------------|--------|
| U1 | ESP32-S3-WROOM-1-N16R8 | [C2913202](https://www.lcsc.com/product-detail/C2913202.html) ([DigiKey 16162642](https://www.digikey.com/en/products/detail/espressif-systems/ESP32-S3-WROOM-1-N16R8/16162642)) | 5 on hand |
| J2 | DC-002 jack (XKB DC-002-2.0A-1.3) | [C381119](https://www.lcsc.com/product-detail/C381119.html) | 20 on hand; not populated |
| D3, D4 | B5819W SL, 1 A / 40 V Schottky, SOD-123 | [C8598](https://jlcpcb.com/partdetail/9093-B5819WSL/C8598), a JLCPCB *basic* part | **Not on hand.** Needs 2 per board. |

B5819W was not in any order. It is sourced from JLCPCB's parts library as
[C8598](https://jlcpcb.com/partdetail/9093-B5819WSL/C8598) (Jiangsu Changjing
B5819W SL), a *basic* library part, so it is placed at assembly with no feeder
fee and nothing needs to be bought separately.

Nothing else on the rev 2 BOM changed from rev 1, so the rev 1 part numbers in
`production/JLCPCB_xmas_orn_bom.csv` still apply to those references.

---

### Enclosure (`board/xmas_orn/enclosure`)

A printed case for the rev 2 board plus its display, done as a **common core** (tray + snap-on
bezel) that slots into interchangeable **outers**, the default being a rounded hanging sleeve.
Parametric OpenSCAD, print-oriented STLs for the Bambu Lab A1 mini, and the core envelope /
groove interface any new outer needs are documented in
[board/xmas_orn/enclosure/README.md](board/xmas_orn/enclosure/README.md). The barrel jack and
USB-C are open through both the core and the sleeve.
Five themed outers (Captain America shield, two Spider-Man variants, Iron Man, Black Widow)
share the same pocket and export one STL per colour for the AMS lite.

The power hub gets a plainer two-part box, same toolchain, in
[board/xmas_hub/enclosure/](board/xmas_hub/enclosure/README.md): four M3 screws from below
clamp the board and the lid together, with a notch for the 12 V jack and windows for the four
arm terminals.

---

### Power hub (`board/xmas_hub`)

A second KiCad 10 project: the base-of-tree distribution board for the rev 2
jack. A 12 V / 3 A barrel supply goes in; four independently regulated and
fused **5 V / 3 A arms** come out on screw terminals, each feeding a short
cluster of ornaments through their DC-002 jacks. One TPS54302 buck and one PTC
per arm, so a fault on one arm leaves the other three running — the "star"
topology without a buck on every ornament. Design notes, BOM with LCSC numbers,
cable guidance and the generator scripts are in
[`board/xmas_hub/README.md`](board/xmas_hub/README.md); JLCPCB outputs in
`board/xmas_hub/production/`.

## Software used

| Area | Tool | Version / where | Used for |
|------|------|-----------------|----------|
| Firmware | [ESP-IDF](https://docs.espressif.com/projects/esp-idf/) | v5.1+ | `idf.py` build, flash, monitor; CMake drives the bake generators |
| Firmware | Host C compiler | any `gcc` on `PATH` (`HOST_CC` overrides) | Builds `tools/bake_model` and `tools/extract_model`, which run at build time |
| Firmware | PowerShell | `scripts/*.ps1` | The three build-time generators (`bake-model`, `gen-const-tables`, `extract-model`) |
| Firmware | [toridraw / rscache](https://github.com/MRobertEvers/oldschool-clientc) | submodule pin in `3rd/` | Software rasteriser and OSRS cache readers |
| Board | [KiCad](https://www.kicad.org/) | 10.0.6 | `board/xmas_orn` and `board/xmas_hub` schematics and layouts |
| Board | `kicad-cli` | ships with KiCad | ERC/DRC, netlist, BOM, position file, gerber/drill export, PDF/SVG/PNG renders in `docs/` |
| Board | KiCad's bundled Python (`pcbnew`) | inside the KiCad app bundle | `board/xmas_hub/scripts/gen_sch.py` / `gen_pcb.py` generate that board programmatically |
| Board | [Freerouting](https://github.com/freerouting/freerouting) | | Autorouting; both boards are fresh autoroutes that pass the project DRC |
| Board | [Fabrication Toolkit](https://github.com/bennymeg/Fabrication-Toolkit) | KiCad plugin, `fabrication-toolkit-options.json` | Rev 1 JLCPCB outputs under `production/`; rev 2 used `kicad-cli` directly |
| Enclosure | [OpenSCAD](https://openscad.org/) | 2026.09 snapshot, `brew install --cask openscad@snapshot` | `board/xmas_orn/enclosure/xmas_orn_case.scad`; `export.sh` renders the STLs and the `check_*` parts prove the fit |
| Enclosure | [trimesh](https://trimesh.org/) (optional) | `pip install trimesh numpy networkx` | Watertightness and overlap-volume checks on the exported meshes |
| Enclosure | [Bambu Studio](https://bambulab.com/en/download/studio) | | Slicing for the Bambu Lab A1 mini; the STLs are already print-oriented |

On macOS the stable `openscad` cask is disabled (Gatekeeper); use the snapshot cask. KiCad's
`kicad-cli` and Python live at `/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli` and
`.../Contents/Frameworks/Python.framework/Versions/Current/bin/python3`. `qlmanage -t` is a
handy stand-in for `pdftoppm` when previewing the exported PDFs.

## Prerequisites

- ESP-IDF v5.1+
- A host `gcc` on `PATH` (MinGW is fine) to build the bake tools. `HOST_CC` overrides.
- **The OSRS caches**, which are *not* vendored — see below.

### Libraries: vendored as a submodule

`toridraw`, `rscache` and `toridraw_rscache` come from
[`oldschool-clientc`](https://github.com/MRobertEvers/oldschool-clientc), pinned
as a submodule at [`3rd/oldschool-clientc`](3rd/oldschool-clientc).

```powershell
git clone git@github.com:MRobertEvers/esp32-xmas.git
cd esp32-xmas
git submodule update --init --recursive
```

`--recursive` matters: the nested `OSRS-Content` submodule supplies `all.seq`,
which is where the animation frame ids and their hold times come from.

This used to be `../oldschool-clientc` — whatever happened to be checked out
next to this repo on the machine doing the build. That is not a version. The
Xtensa raster kernels this client depends on are developed in that repo, so two
working copies routinely meant two different firmwares from the same source
tree, with nothing recording the difference.

To build against a live working copy of the library instead of the pin:

```powershell
idf.py -DCLIENTC_DIR=C:\path\to\oldschool-clientc build
```

### Caches: local, not vendored

The caches are gigabytes of game data and are gitignored in *both* repos, so the
submodule carries the libraries and the configs but **no cache**. They get their
own root, defaulting to a sibling checkout:

| Variable | Default | |
|---|---|---|
| `CACHE_ROOT` | `../oldschool-clientc` | where the caches live |
| `ANIM_CACHE_DIR` | `${CACHE_ROOT}/cache.osrs239` | dat2 cache |
| `CACHE_DIR` | `${CACHE_ROOT}/cache.rs289lc` | dat1 cache |

Pointing these at the submodule would look tidier and would silently break every
fresh clone, because the directory exists and is empty.

## Build and flash

```powershell
. C:\Users\<you>\esp\esp-idf\export.ps1
idf.py set-target esp32s3
idf.py build
idf.py -p COM3 flash monitor
```

`idf.py build` runs three generators before compiling:

1. **`scripts/bake-model.ps1`** → `build/gen/xmas_baked.c`. The model, the rig, every animation frame and every texture, as `const` arrays.
2. **`scripts/gen-const-tables.ps1`** → `build/gen/toridraw_tables_precomputed.c`. toridraw's HSL palette and trigonometry, for the configured pixel format.
3. **`scripts/extract-model.ps1`** → `build/model.bin`, flashed to the `model` partition.

All three track their CMake cache variables, so changing the model re-bakes.

---

## Choosing a model and animation

```powershell
idf.py -DANIM_MODEL=2851 -DANIM_SEQ=entendseq build
```

| Variable | Default | Meaning |
|---|---|---|
| `ANIM_MODEL` | `2851` | dat2 model id |
| `ANIM_SEQ` | `entendseq` | sequence name in `all.seq` |
| `ANIM_CACHE_DIR` | `${CACHE_ROOT}/cache.osrs239` | dat2 cache |

### Pick a sequence that actually animates this model

Some sequences drive **two** models and hide whichever is not current, by collapsing every one of its vertices onto a single point. Render one model of such a pair and roughly half the frames draw nothing at all — which looks like flicker, and is the animation behaving correctly.

`pog_spirit_tree_anim` is one of these: `models=49769,49771`, cross-faded by `pog_spirit_tree_transform`. 48 of its 99 poses came out with bounds `radius 0`.

The firmware checks for this at boot and says so:

```
all 12 poses draw
```

or, if you have picked one of those sequences:

```
W: 48 of 99 poses draw nothing -- the sequence probably drives a
   second model that this one is hidden for
```

A `readyanim` has no partner: it loops, and every frame has geometry.

### A sequence can hide part of a model on purpose

Picking a sequence that animates the model is not the same as picking one that
shows all of it. Model 2851 has two idles, and `all.loc` uses them to mean
different things:

| loc | anim | orbs |
|---|---|---|
| `spirittree_big_2ops` | `entready` | hidden |
| `spirittree_big_2ops_orbs` | `entendseq` | fading |

The rig has two type-5 (transparency) groups. `entready` holds them at a
constant +16 and +32 on every frame -- and +32x8 = 256, which clamps to 255,
so the orbs are fully hidden. That is correct: that loc has no orbs.
`entendseq` ramps the second group 20 -> 6 and back and dips the first to -10,
which is the fade.

So "the orbs do not fade" had two different causes in sequence, and only the
first was a bug: the model had no face bones baked, so no type-5 op ran at all;
and then the sequence in use genuinely asked for them to be hidden.

---

## Lighting

Applied at bake time, so the device receives finished colours and never lights anything.

```
lightness = ambient + (L·N) / (attenuation × face_count)
```

That result then *scales* each face's own base lightness, clamped to `[2,126]`.

| Variable | Default | Effect |
|---|---|---|
| `LIGHT_AMBIENT` | `128` | The floor. Slides the whole model brighter or darker. |
| `LIGHT_ATTENUATION` | `192` | **The contrast knob.** Divides the directional term, so *lower* is a wider swing. Reference is 768. |
| `LIGHT_GAMMA` | `1.0` | Re-curves the distribution. A **brightness** knob — see below. |
| `LIGHT_DIR` | `-50,-10,-50` | Light vector. |

Every bake prints what it did:

```
lighting: ambient 128, attenuation 192, dir -50,-10,-50
  p10 15, p50 35, p90 120 (inner spread 105)
  lightness 2..126 (spread 124), mean 49, 7% on the clamps
```

**Read the inner spread, not the range.** The min and max are two faces; p10–p90 is the thousand in between, and that is what the eye reads as contrast. Measured on model 2851:

| ambient | attenuation | gamma | inner spread | mean | clipped |
|---|---|---|---|---|---|
| 64 | 768 (reference) | 1.0 | — | 24 | 0% |
| 96 | 384 | 1.0 | 69 | 37 | 0% |
| 96 | 192 | 1.0 | 88 | 38 | 2% |
| 96 | 192 | 0.8 | 87 | 46 | 2% |
| **128** | **192** | **1.0** | **105** | **49** | **7%** |
| 96 | 128 | 1.0 | 108 | 39 | 17% |

`attenuation` is the only knob that moves the inner spread. **Gamma does not add contrast** — measured, it moved the mean 37 → 73 while *shrinking* inner spread 69 → 55. It is there for a model that is too dark, not one that is too flat.

`clipped` is the share of face corners pinned at 2 or 126, which have lost their shading. 7% is the price of the current default; `-DLIGHT_AMBIENT=96 -DLIGHT_ATTENUATION=192 -DLIGHT_GAMMA=0.8` is the same contrast at 2%.

---

## Animation timing

Sequences run on the reference client's **50 Hz** cycle, and each frame carries its own hold time.

That hold time lives **only in the seq config** — `frame=<packed id>,<ticks>` in `all.seq`. The frame archive does not carry it, so nothing downstream can recover it if the packer drops it. `entendseq` holds its 18 frames for 4-5 ticks: about 90 ms a pose. (`entready`, the other idle for this rig, holds for 21.)

The render loop derives the animation phase from `esp_timer` rather than counting passes, so playback rate does not change when render cost does. The boot log reports a measured rate:

```
frame 4: 44.2 fps, 2.0 poses/s  visible=1  7794 px  anim 1.06 ms  raster 5.74 ms  blit 0.25 ms
```

Both numbers are per wall-clock second. Counting frames and multiplying by an assumed period produces a rate that lies whenever the loop misses its period.

---

## Textures

A textured face carries a texture **id**, not texels, and toridraw's texture map starts empty. **Its raster silently skips any face whose id is not registered** — no error, no gap in the depth order. An unregistered texture is indistinguishable from a model with no textured faces.

The bake resolves each id the model references to its cache sprite, resamples it to 128×128 ARGB (64 KB of `.rodata` each) and emits it; `main.c` registers them with `ToriDraw_MiniSetTexture` before the first draw. Boot reports the count:

```
registered 1 texture
```

---

## Alpha

Two independent things, both of which have to work.

**Static alpha** — `face_alphas` is a transparency byte per face, 0 opaque to 255 hidden (254 and 255 are render-type sentinels, not levels).

**Animated alpha** — a framemap transform of **type 5** adds to `face_alphas` for a group of faces. It is the only transform that touches faces rather than vertices, and it needs *two* things beyond the vertex path: a writable `face_alphas`, and the model's **face** bone map, which is not the vertex one. Without either, `ToriDraw_ModelApplyTransform` returns immediately and the model animates perfectly except that nothing ever fades.

So `face_alphas` is live RAM seeded from a const `original_face_alphas`, exactly as the vertices are — see [`main/xmas_baked.h`](main/xmas_baked.h).

Boot verifies both, per level:

```
rig: 53 transform groups, 2 of them alpha fades
alpha honoured (420 of 1000 faces)
  alpha 112 (opacity 143/255): 18 faces, blended
  alpha  80 (opacity 175/255): 192 faces, blended
  alpha 144 (opacity 111/255): 210 faces, blended
```

Each line is an A/B: that level is forced opaque and the frame is re-rendered and hashed. Aggregate alpha passing does not prove every level does — a model with three levels passes the aggregate test if only one survives.

---

## Display

**Pixel format is `TORIDRAW_PF_RGB565_BE`** — RGB565 with the high byte first, which is what an SPI ST7789 clocks out. The palette is baked in the panel's own byte order, so there is no per-frame conversion pass. The Xtensa raster kernels serve both `RGB565` and `RGB565_BE` from one implementation.

**Backlight** is LEDC PWM (20 kHz, 11-bit, squared for perceptual response) at `LCD_BL_BRIGHTNESS_PCT` — 35 by default. A plain GPIO has exactly one brightness, and at full output this panel washes out.

**Background is black.** A lit background spends contrast before the model gets any; the previous dark navy read as grey haze behind the backlight, and the model's own dark faces sat below it.

### Frame budget

At 80 MHz the 115200-byte frame is 11.5 ms on the wire, against ~7 ms of render. Measured frame time is 30 ms.

The DMA transfer is asynchronous, but with **one** framebuffer that buys nothing: rendering the next frame writes the same 115 KB the panel is reading, so the two serialise wherever the wait goes. A second framebuffer would genuinely overlap them and does not fit — 115 KB against the ~112 KB of internal DRAM left after the view. `blit_wait()` is therefore a *correctness* barrier against tearing, not a performance one.

---

## Memory

Baking is what removes the PSRAM requirement. Decoding the original animated model at boot wanted 406 KB against 311 KB of internal DRAM, and fragmented what remained — 229 KB free in a largest block of 55 KB, which held neither the view nor the framebuffer.

| | |
|---|---|
| Model, rig, frames, textures | `.rodata` (flash) |
| toridraw palette + trig tables | `.rodata` (flash) |
| Live vertices | 3 × `int16_t` × vertex count (~3 KB) |
| Live face alphas | 1 byte × face count (~1 KB) |
| toridraw view (scratch) | ~113 KB |
| Framebuffer | 115 KB, DMA-capable |

See [`tools/bake_model/bake_model.c`](tools/bake_model/bake_model.c) for the full reasoning.

---

## Boot diagnostics

Several checks stayed in after they caught real bugs. They cost about 100 ms total and each replaces a visual comparison against a game client with one log line.

| Line | Catches |
|---|---|
| `all N poses draw` | A sequence that hides this model for half its frames |
| `registered N textures` | Textures never registered, so textured faces silently skipped |
| `rig: N groups, M alpha fades` | A rig that fades, on a model with no face bones to fade |
| `alpha honoured (N of M faces)` | Alpha not reaching the raster at all |
| `alpha K: N faces, blended` | One alpha level being dropped while others work |
| `X fps, Y poses/s` | A loop that is not holding its period |

Every one of these corresponds to a bug that rendered a plausible-looking image.
