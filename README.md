# ESP32-S3 ST7789 LVGL Display

ESP-IDF firmware for an **ESP32-S3 dev module** driving a **240x240 ST7789** SPI LCD with an **LVGL** demo UI.

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

Default pins are defined at the top of [`main/main.c`](main/main.c).

## Prerequisites

- [ESP-IDF v5.1+](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/index.html) installed and exported in your shell
- USB cable connected to the ESP32-S3 dev module

## Build and flash

From the project root:

```sh
idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash monitor
```

Replace `COMx` with your serial port (for example `COM3` on Windows or `/dev/ttyUSB0` on Linux).

On first build, the Component Manager downloads `esp_lvgl_port` and LVGL automatically.

## What you should see

After flashing, the display shows:

- Dark blue background
- **ESP32-S3** title and **ST7789 240x240** subtitle
- Animated arc indicator (oscillates 0–100%)
- **LVGL ready** status text

## Customization

### Change GPIO pins

Edit the `#define PIN_LCD_*` block near the top of [`main/main.c`](main/main.c).

### Display looks wrong

Try these tweaks in [`main/main.c`](main/main.c) inside `lcd_init()`:

| Symptom | Fix |
|---------|-----|
| Image shifted | Uncomment `esp_lcd_panel_set_gap()` |
| Wrong colors | Toggle `esp_lcd_panel_invert_color()` |
| Mirrored / upside down | Adjust `esp_lcd_panel_mirror()` or the `rotation` fields in `disp_cfg` |
| Garbled pixels | Lower `LCD_PIXEL_CLOCK_HZ` (for example 20 MHz) |

### Backlight polarity

If the backlight stays off, change `LCD_BLK_ON_LEVEL` to `0` in [`main/main.c`](main/main.c).

### Memory / performance

- `LCD_DRAW_BUF_LINES` controls LVGL draw buffer height (default: 20 lines).
- `sdkconfig.defaults` enables octal PSRAM for larger buffers; disable if your module has no PSRAM.

## Project layout

```
.
├── CMakeLists.txt
├── sdkconfig.defaults
├── main/
│   ├── CMakeLists.txt
│   ├── idf_component.yml
│   └── main.c
└── README.md
```

## License

Example code — use and modify freely for your projects.
