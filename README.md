# ESP32-S3 OSRS Model Renderer

ESP-IDF firmware for an **ESP32-S3** driving a **240×240 ST7789** SPI LCD. It renders an animated Old School RuneScape model in software — no GPU, no PSRAM — using **[toridraw](3rd/oldschool-clientc/3rd/toridraw)** for the raster and **[rscache](3rd/oldschool-clientc/3rd/rscache)** for the cache formats.

The model, its animation, its textures and toridraw's lookup tables are all baked into flash at build time. The device decodes nothing at boot.

Current subject: the Tree Gnome Village spirit tree (dat2 model **2851**) playing `entendseq`, the idle that fades the orbs in its branches.

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

---

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
