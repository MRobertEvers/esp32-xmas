# ESP32-S3 OSRS Model Renderer

ESP-IDF firmware for an **ESP32-S3** driving a **240x240 ST7789** SPI LCD. At build time a model is extracted from the dat1 OSRS cache (`3d-raster/cache`), flashed to a dedicated partition, then decoded and rendered on-device with **rscache** + **toridraw**.

## Hardware

| LCD pin | ESP32-S3 GPIO | Notes |
|---------|---------------|-------|
| SCLK    | 12            | SPI clock |
| MOSI    | 11            | SPI data |
| CS      | 10            | Chip select |
| DC      | 9             | Data/command |
| RST     | 8             | Reset |
| BLK     | 7             | Backlight (active high) |
| VCC     | 3.3 V         | Use 3.3 V only |
| GND     | GND           | Common ground |

Default pins are defined in [`main/main.c`](main/main.c).

## Prerequisites

- [ESP-IDF v5.1+](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/index.html)
- Sibling checkout of [`3d-raster`](../3d-raster) with a dat1 cache at `3d-raster/cache`
- ESP32-S3 module with **octal PSRAM** (used for the 240x240 framebuffer)

## Build and flash

From the project root:

```powershell
. .\scripts\export-idf.ps1
idf.py set-target esp32s3
idf.py build
idf.py -p COM5 flash monitor
```

`idf.py build` automatically:

1. Builds the host `extract_model` tool (first time only)
2. Extracts model archive **7** from `../3d-raster/cache` into `build/model.bin`
3. Links toridraw/rsmodel from `../3d-raster` sources
4. Precomputes toridraw math/HSL lookup tables into flash (`.rodata`) via `tools/gen_const_tables`

`idf.py flash` writes both the firmware and `model.bin` to the `model` flash partition.

### Choose a different model

Pass a CMake cache variable when configuring/building:

```powershell
idf.py -DMODEL_ID=42 build
idf.py -DMODEL_ID=9424 build
```

Or extract manually:

```powershell
.\scripts\extract-model.ps1 -ModelId 42 -CacheDir C:\path\to\cache
```

## What you should see

A rotating 3D OSRS model on a dark blue background.

## Project layout

```
.
├── CMakeLists.txt          # MODEL_ID, cache path, model.bin flash hook
├── partitions.csv          # factory app + model data partition
├── components/
│   ├── rsmodel/            # model_new_decode from 3d-raster rscache
│   └── toridraw/           # software renderer from 3d-raster
├── tools/extract_model/    # host cache extractor
├── scripts/
│   ├── extract-model.ps1
│   └── export-idf.ps1
└── main/main.c
```

## Customization

- **GPIO pins:** edit `PIN_LCD_*` in [`main/main.c`](main/main.c)
- **Cache location:** `idf.py -DCACHE_DIR=C:\path\to\cache build`
- **3d-raster location:** `idf.py -DRASTER_DIR=C:\path\to\3d-raster build`
- **Display tuning:** see `lcd_init()` in [`main/main.c`](main/main.c)

## License

Example code — use and modify freely for your projects.

