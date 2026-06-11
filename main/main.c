/*
 * ESP32-S3 + ST7789 240x240: decode a flashed OSRS model and render with toridraw.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"

#include "osrs/rscache/tables/model.h"
#include "gamecache/toridraw_cachemodel.h"
#include "toridraw/toridraw.h"
#include "toridraw/toridraw_light_model.h"

static const char *TAG = "xmas_model";

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
#define LCD_BLK_ON_LEVEL        1

#define MODEL_PART_NAME         "model"
#define FRAME_BG_RGB            0x0B1D3A

static esp_lcd_panel_io_handle_t s_lcd_io = NULL;
static esp_lcd_panel_handle_t s_lcd_panel = NULL;

static const esp_partition_t *s_model_part = NULL;
static const void *s_model_map = NULL;
static esp_partition_mmap_handle_t s_model_map_handle = 0;

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
    spi_bus_config_t bus_cfg = {
        .sclk_io_num = PIN_LCD_SCLK,
        .mosi_io_num = PIN_LCD_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * LCD_V_RES * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = PIN_LCD_DC,
        .cs_gpio_num = PIN_LCD_CS,
        .pclk_hz = LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(LCD_HOST, &io_cfg, &s_lcd_io));

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = PIN_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(s_lcd_io, &panel_cfg, &s_lcd_panel));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_lcd_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_lcd_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_lcd_panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_lcd_panel, true));
}

static bool model_partition_map(void)
{
    s_model_part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, MODEL_PART_NAME);
    if( !s_model_part )
    {
        ESP_LOGE(TAG, "partition '%s' not found", MODEL_PART_NAME);
        return false;
    }

    esp_err_t err = esp_partition_mmap(
        s_model_part,
        0,
        s_model_part->size,
        ESP_PARTITION_MMAP_DATA,
        &s_model_map,
        &s_model_map_handle);
    if( err != ESP_OK )
    {
        ESP_LOGE(TAG, "esp_partition_mmap failed: %s", esp_err_to_name(err));
        return false;
    }

    return true;
}

static uint16_t rgb888_to_rgb565_swapped(int rgb)
{
    uint16_t r = (uint16_t)((rgb >> 16) & 0xFF);
    uint16_t g = (uint16_t)((rgb >> 8) & 0xFF);
    uint16_t b = (uint16_t)(rgb & 0xFF);
    uint16_t c = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    return (uint16_t)((c >> 8) | (c << 8));
}

static void blit_framebuffer_to_lcd(const int *pixel_buffer)
{
    static uint16_t line_buf[LCD_H_RES];

    for( int y = 0; y < LCD_V_RES; y++ )
    {
        const int *row = pixel_buffer + y * LCD_H_RES;
        for( int x = 0; x < LCD_H_RES; x++ )
            line_buf[x] = rgb888_to_rgb565_swapped(row[x]);

        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(
            s_lcd_panel, 0, y, LCD_H_RES, y + 1, line_buf));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "OSRS model renderer starting");

    backlight_init();
    lcd_init();
    backlight_on();

    if( !model_partition_map() )
        return;

    const uint8_t *mapped = (const uint8_t *)s_model_map;
    uint32_t payload_size =
        (uint32_t)mapped[0]
        | ((uint32_t)mapped[1] << 8)
        | ((uint32_t)mapped[2] << 16)
        | ((uint32_t)mapped[3] << 24);
    const unsigned char *model_bytes = mapped + 4;

    if( payload_size == 0 || payload_size + 4 > s_model_part->size )
    {
        ESP_LOGE(TAG, "invalid model payload size %lu", (unsigned long)payload_size);
        return;
    }

    ESP_LOGI(TAG, "decoding %lu byte model from flash", (unsigned long)payload_size);
    struct CacheModel *cache_model = model_new_decode(model_bytes, (int)payload_size);
    if( !cache_model )
    {
        ESP_LOGE(TAG, "model_new_decode failed");
        return;
    }

    ESP_LOGI(
        TAG,
        "decoded model: %d vertices, %d faces",
        cache_model->vertex_count,
        cache_model->face_count);

    struct ToriDraw_Model *td_model = toridraw_model_new_from_cache_model(cache_model);
    model_free(cache_model);
    if( !td_model )
    {
        ESP_LOGE(TAG, "toridraw_model_new_from_cache_model failed");
        return;
    }

    struct ToriDraw_ModelHandle model_hnd = {
        .kind = TORIDRAWMK_MODEL,
        .u.model.model = td_model,
    };

    toridraw_init();
    struct ToriDraw_Context *ctx = toridraw_context_new();
    if( !ctx )
    {
        ESP_LOGE(TAG, "toridraw_context_new failed");
        return;
    }

    toridraw_light_model_default(model_hnd, 0, 0);

    int pixel_count = LCD_H_RES * LCD_V_RES;
    int *pixel_buffer = heap_caps_malloc(
        (size_t)pixel_count * sizeof(int), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if( !pixel_buffer )
    {
        ESP_LOGE(TAG, "failed to allocate framebuffer in PSRAM");
        return;
    }

    struct ToriDraw_ViewPort view_port = {
        .width = LCD_H_RES,
        .height = LCD_V_RES,
        .stride = LCD_H_RES,
        .x_center = LCD_H_RES / 2,
        .y_center = LCD_V_RES / 2,
        .clip_left = 0,
        .clip_top = 0,
        .clip_right = LCD_H_RES,
        .clip_bottom = LCD_V_RES,
    };

    struct ToriDraw_Camera camera = {
        .fov_rpi2048 = 512,
        .near_plane_z = 50,
        .pitch = 0,
        .yaw = 0,
        .roll = 0,
    };

    struct ToriDraw_Position position = {
        .x = 0,
        .y = 0,
        .z = 300,
        .pitch = 0,
        .yaw = 0,
        .roll = 0,
    };

    ESP_LOGI(TAG, "render loop running");

    int yaw = 0;
    while( true )
    {
        for( int i = 0; i < pixel_count; i++ )
            pixel_buffer[i] = FRAME_BG_RGB;

        // camera.yaw = yaw;
        position.yaw = yaw;

        int cull = toridraw_render_model1_project(
            model_hnd, ctx, &position, &view_port, &camera);
        if( cull == TORIDRAW_CULL_VISIBLE )
        {
            toridraw_render_model2_sort_faces(model_hnd, ctx);
            toridraw_render_model3_raster(ctx, &view_port, &camera, pixel_buffer, false);
        }

        blit_framebuffer_to_lcd(pixel_buffer);

        yaw = (yaw + 8) & 2047;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
