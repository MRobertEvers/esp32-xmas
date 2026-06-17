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



## SPI Mode settings

Why Mode 3 is Needed: The "Missing CS" Problem
In a standard SPI setup, the Chip Select (CS) pin acts as a reset button for the display's internal communication logic. When CS goes low, the display wakes up and gets ready to receive exactly 8 bits. When CS goes high, the transaction is over. If there was a glitch and it only received 7 bits, the high CS resets the counter anyway, so the next byte starts perfectly aligned.

Because your display lacks a physical CS pin, its internal CS line is permanently hardwired to Ground. The display is always listening, and its bit-counter never resets. It relies 100% on counting exactly 8 clock pulses per byte to stay aligned.

This is where the SPI Mode becomes critical. The modes define the Clock Polarity (CPOL) and Clock Phase (CPHA)—essentially, what the clock line does when it is not sending data, and when it samples the data.

SPI Mode 0 (CPOL=0): The clock line idles LOW.

SPI Mode 3 (CPOL=1): The clock line idles HIGH.

Both modes technically sample data on the rising edge of the clock signal, which the ST7789 controller accepts. The failure happens during initialization. When the ESP32 boots, its GPIO pins float before the SPI peripheral takes over and configures them.

If you use Mode 0, the clock line is trying to idle low. As the ESP32 initializes the bus, tiny voltage spikes or the transition from a floating state to a driven low state can cause a microscopic electrical bounce. The always-listening display sees that bounce as a "ghost" clock pulse. Now your bit-counter is off by one. You send 10101010, but the display reads it shifted by one bit, permanently garbling every initialization command you send.

By switching to Mode 3, you force the clock line to idle HIGH. It is actively driven to 3.3V while idle, making it highly resistant to those micro-glitches during startup. The display waits for a deliberate drop to logic low before it starts counting, keeping your bit-stream perfectly aligned.

How to Tell Without a Datasheet
When you are grabbing generic components from Chinese marketplaces—like the typical "Dollar Express" deals where datasheets are non-existent—you have to rely on visual inspection and empirical testing.

1. Count the Pins
This is your most reliable tell. If you look at the silkscreen on the back of the module and see GND, VCC, SCL, SDA, RES, DC, and BLK, but absolutely no CS, CE, or NSS pin, you are dealing with a hardwired-CS display. Default immediately to SPI Mode 3.

(Note: These generic boards often confusingly label the SPI clock as SCL and MOSI as SDA, which are I2C naming conventions, but if it has a DC (Data/Command) pin, it is definitely SPI).

2. Identify the Controller Family
Certain form factors have notorious default behaviors. The 1.3-inch to 1.54-inch 240x240 IPS displays almost universally use the ST7789 controller. To save space and simplify wiring for microcontrollers with limited GPIO, the manufacturers often omit the CS pin trace entirely. If you buy a 240x240 display and it has 7 pins, expect to use Mode 3.

3. The "Try It and See" Method
If you are completely unsure, it is 100% safe to just test it. Sending Mode 0 data to a Mode 3 display (or vice versa) will not fry the electronics; it just results in a blank screen or a screen filled with static white noise because the initialization registers received garbage data. If Mode 0 gives you a blank screen on a new module, flip it to Mode 3 before you start checking your wiring.