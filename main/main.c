/*
 * ESP32-S3 + ST7789 240x240 SPI LCD with LVGL demo
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

static const char *TAG = "st7789_lvgl";

/* -------------------------------------------------------------------------- */
/* Pin configuration — edit these to match your wiring                      */
/* -------------------------------------------------------------------------- */
#define LCD_HOST                SPI2_HOST

#define PIN_LCD_SCLK            12
#define PIN_LCD_MOSI            11
#define PIN_LCD_CS              10
#define PIN_LCD_DC              9
#define PIN_LCD_RST             8
#define PIN_LCD_BLK             7

#define LCD_H_RES               240
#define LCD_V_RES               240

#define LCD_PIXEL_CLOCK_HZ      (40 * 1000 * 1000)
#define LCD_CMD_BITS            8
#define LCD_PARAM_BITS          8

#define LCD_BLK_ON_LEVEL        1
#define LCD_DRAW_BUF_LINES      20

static esp_lcd_panel_io_handle_t s_lcd_io = NULL;
static esp_lcd_panel_handle_t s_lcd_panel = NULL;

static void backlight_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << PIN_LCD_BLK,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    gpio_set_level(PIN_LCD_BLK, !LCD_BLK_ON_LEVEL);
}

static void backlight_on(void)
{
    gpio_set_level(PIN_LCD_BLK, LCD_BLK_ON_LEVEL);
}

static void lcd_init(void)
{
    ESP_LOGI(TAG, "Initialize SPI bus");
    spi_bus_config_t bus_cfg = {
        .sclk_io_num = PIN_LCD_SCLK,
        .mosi_io_num = PIN_LCD_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * LCD_V_RES * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    ESP_LOGI(TAG, "Install panel IO");
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = PIN_LCD_DC,
        .cs_gpio_num = PIN_LCD_CS,
        .pclk_hz = LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = LCD_CMD_BITS,
        .lcd_param_bits = LCD_PARAM_BITS,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(LCD_HOST, &io_cfg, &s_lcd_io));

    ESP_LOGI(TAG, "Install ST7789 panel driver");
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = PIN_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(s_lcd_io, &panel_cfg, &s_lcd_panel));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_lcd_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_lcd_panel));

    /* Uncomment if the image is shifted (common on some 240x240 modules):
     * ESP_ERROR_CHECK(esp_lcd_panel_set_gap(s_lcd_panel, 0, 80));
     */

    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_lcd_panel, true));

    /* Uncomment if the image is mirrored or upside-down:
     * ESP_ERROR_CHECK(esp_lcd_panel_mirror(s_lcd_panel, true, false));
     */

    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_lcd_panel, true));
}

static lv_display_t *lvgl_init_display(void)
{
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = s_lcd_io,
        .panel_handle = s_lcd_panel,
        .buffer_size = LCD_H_RES * LCD_DRAW_BUF_LINES,
        .double_buffer = true,
        .hres = LCD_H_RES,
        .vres = LCD_V_RES,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma = true,
            .swap_bytes = true,
        },
    };

    return lvgl_port_add_disp(&disp_cfg);
}

static void arc_anim_cb(void *var, int32_t value)
{
    lv_arc_set_value((lv_obj_t *)var, value);
}

static void create_demo_ui(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0B1D3A), LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "ESP32-S3");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 24);

    lv_obj_t *subtitle = lv_label_create(scr);
    lv_label_set_text(subtitle, "ST7789 240x240");
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0xA8C7FA), LV_PART_MAIN);
    lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 52);

    lv_obj_t *arc = lv_arc_create(scr);
    lv_obj_set_size(arc, 120, 120);
    lv_arc_set_range(arc, 0, 100);
    lv_arc_set_value(arc, 35);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x34D399), LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x1F3A5F), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, 10, LV_PART_MAIN);
    lv_obj_align(arc, LV_ALIGN_CENTER, 0, 20);

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, arc);
    lv_anim_set_exec_cb(&anim, arc_anim_cb);
    lv_anim_set_values(&anim, 0, 100);
    lv_anim_set_duration(&anim, 2000);
    lv_anim_set_playback_duration(&anim, 2000);
    lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&anim);

    lv_obj_t *status = lv_label_create(scr);
    lv_label_set_text(status, "LVGL ready");
    lv_obj_set_style_text_color(status, lv_color_hex(0xFDE68A), LV_PART_MAIN);
    lv_obj_align(status, LV_ALIGN_BOTTOM_MID, 0, -24);
}

void app_main(void)
{
    ESP_LOGI(TAG, "ST7789 LVGL demo starting");

    backlight_init();
    lcd_init();
    backlight_on();

    lv_display_t *display = lvgl_init_display();
    if (display == NULL) {
        ESP_LOGE(TAG, "Failed to add LVGL display");
        return;
    }

    lvgl_port_lock(0);
    create_demo_ui();
    lvgl_port_unlock();

    ESP_LOGI(TAG, "Demo UI running on %dx%d display", LCD_H_RES, LCD_V_RES);
}
