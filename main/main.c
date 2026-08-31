/*
 * ESP32-S3 + ST7789 240x240: decode a flashed OSRS model and render with
 * toridraw.
 *
 * THE THREE LIBRARIES, AND WHY THERE ARE THREE. RSCache decodes the blob into
 * a struct shaped like the wire format. ToriDraw draws a struct shaped for the
 * raster. toridraw_rscache is the adaptor, and it exists so neither of the
 * other two has to know the other is there -- see 3rd/toridraw_rscache/README.md
 * for the four things that differ and are each a silent bug if skipped.
 *
 * This client drives ToriDraw through toridraw_mini.h, which is the interface
 * for exactly this situation: one model, one buffer, a fixed budget, no
 * malloc after init. The scene graph, the kernel tables and the three-stage
 * pipeline are all still there behind ToriDraw_MiniViewScene() if this ever
 * needs them; it does not.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_cpu.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_timer.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"

#include "toridraw.h"
#include "toridraw_math.h"
#include "toridraw_mini.h"
#include "toridraw_model_transform.h"
#include "toridraw_animation.h"
#include "xmas_baked.h"

static const char *TAG = "xmas_model";

#define LCD_HOST                SPI2_HOST
#define PIN_LCD_SCLK            12
#define PIN_LCD_MOSI            11
#define PIN_LCD_CS              -1
#define PIN_LCD_DC              9
#define PIN_LCD_RST             8
#define PIN_LCD_BLK             7
#define PIN_PULSE               4

#define LCD_H_RES               240
#define LCD_V_RES               240
/*
 * 80 MHz, which this wiring can actually reach.
 *
 * SCLK on GPIO12 and MOSI on GPIO11 are the IOMUX pins for SPI2 on the S3, so
 * the signals bypass the GPIO matrix and 80 MHz is in spec. On any other pins
 * the matrix caps the useful rate near 40 and this would have to come back
 * down.
 *
 * It matters because the frame is 240*240*2 = 115200 bytes: 23 ms on the wire
 * at 40 MHz, 11.5 at 80. That was the frame budget, not the raster -- and
 * halving it took the measured frame from about 50 ms to 30.
 */
#define LCD_PIXEL_CLOCK_HZ      (80 * 1000 * 1000)

/*
 * Render period. 20 ms to match the animation tick.
 *
 * The old 50 ms was chosen when the blit was synchronous-looking and nobody
 * had measured it. Once the transfer overlaps the next frame's render (see
 * blit_wait) the loop costs max(render, transfer) = about 12 ms, so 20 ms is
 * a period it can actually hold.
 */
#define LOOP_PERIOD_MS          20
#define LCD_BLK_ON_LEVEL        1

/*
 * Backlight brightness, 0-100.
 *
 * The panel used to be lit by holding BLK high, which is the only brightness a
 * plain GPIO has, and at full output this panel washes out: the dark blue
 * background lifts towards grey and the model's lit and shaded faces stop
 * being distinguishable. LEDC gives the pin a duty cycle instead.
 *
 * 11 bits at 20 kHz. The frequency is above hearing (these panels' backlight
 * drivers whine at a few kHz) and 11 bits leaves enough steps that the low end
 * is still smooth, which an 8-bit duty is not -- the bottom of a backlight
 * curve is where the eye has the most resolution.
 */
#define LCD_BL_BRIGHTNESS_PCT   35
#define LCD_BL_LEDC_TIMER       LEDC_TIMER_0
#define LCD_BL_LEDC_CHANNEL     LEDC_CHANNEL_0
#define LCD_BL_LEDC_MODE        LEDC_LOW_SPEED_MODE
#define LCD_BL_LEDC_RES         LEDC_TIMER_11_BIT
#define LCD_BL_LEDC_FREQ_HZ     20000

/*
 * Black, not the dark navy this used to be.
 *
 * Contrast on this panel is a ratio against the background, and a lit
 * background spends it before the model gets any. 0x0B1D3A reads as "nearly
 * black" on a monitor and as "grey haze" behind a backlight at any useful
 * brightness -- the model's own dark faces sat below it and vanished. Black
 * costs nothing and is the one background the panel can actually reach.
 */
#define FRAME_BG_RGB            0x000000
#define HEAD_SWING_DEG          30
#define HEAD_YAW_UNITS_PER_REV  2048
#define HEAD_YAW_POS_R2PI2048   ((HEAD_SWING_DEG * HEAD_YAW_UNITS_PER_REV) / 360)
#define HEAD_YAW_NEG_R2PI2048   (((360 - HEAD_SWING_DEG) * HEAD_YAW_UNITS_PER_REV) / 360)
#define HEAD_SWING_STEP         4

typedef enum
{
    HEAD_YAW_TO_POS = 0,
    HEAD_YAW_TO_NEG_WRAP,
    HEAD_YAW_TO_CENTER,
} HeadYawPhase;

static int head_yaw_advance(int head_yaw, HeadYawPhase *phase)
{
    switch( *phase )
    {
    case HEAD_YAW_TO_POS:
        head_yaw += HEAD_SWING_STEP;
        if( head_yaw >= HEAD_YAW_POS_R2PI2048 )
        {
            head_yaw = HEAD_YAW_POS_R2PI2048;
            *phase = HEAD_YAW_TO_NEG_WRAP;
        }
        break;

    case HEAD_YAW_TO_NEG_WRAP:
        head_yaw = ToriDraw_NormalizeAngle(head_yaw - HEAD_SWING_STEP);
        if( head_yaw > HEAD_YAW_POS_R2PI2048 && head_yaw <= HEAD_YAW_NEG_R2PI2048 )
        {
            head_yaw = HEAD_YAW_NEG_R2PI2048;
            *phase = HEAD_YAW_TO_CENTER;
        }
        break;

    case HEAD_YAW_TO_CENTER:
        head_yaw = ToriDraw_NormalizeAngle(head_yaw + HEAD_SWING_STEP);
        if( head_yaw < HEAD_YAW_POS_R2PI2048 )
        {
            head_yaw = 0;
            *phase = HEAD_YAW_TO_POS;
        }
        break;
    }

    return head_yaw;
}

/** How many 20 ms ticks frame `f` is held for; at least one. */
static int
anim_frame_hold(const struct ToriDraw_Animation* anim, int f)
{
    int hold;

    if( !anim || f < 0 || f >= anim->frame_count )
        return 1;
    hold = anim->frames[f].delay;
    return hold > 0 ? hold : 1;
}

static esp_lcd_panel_io_handle_t s_lcd_io = NULL;
static esp_lcd_panel_handle_t s_lcd_panel = NULL;

static void backlight_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode = LCD_BL_LEDC_MODE,
        .timer_num = LCD_BL_LEDC_TIMER,
        .duty_resolution = LCD_BL_LEDC_RES,
        .freq_hz = LCD_BL_LEDC_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_channel_config_t channel = {
        .gpio_num = PIN_LCD_BLK,
        .speed_mode = LCD_BL_LEDC_MODE,
        .channel = LCD_BL_LEDC_CHANNEL,
        .timer_sel = LCD_BL_LEDC_TIMER,
        .duty = 0, /* dark until the first frame is on the panel */
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
    ESP_ERROR_CHECK(ledc_channel_config(&channel));
}

/** Set the backlight to `pct` of full, 0-100. */
static void
backlight_set(int pct)
{
    const uint32_t full = (1u << LCD_BL_LEDC_RES) - 1u;
    uint32_t duty;

    if( pct < 0 )
        pct = 0;
    if( pct > 100 )
        pct = 100;

    /*
     * Squared, because perceived brightness is not the duty cycle. A linear
     * 35% duty still looks close to full on; the square puts the requested
     * percentage roughly where the eye expects it.
     */
    duty = (full * (uint32_t)pct * (uint32_t)pct) / (100u * 100u);

    /* BLK active-low would want the complement; this panel is active-high, and
     * LCD_BLK_ON_LEVEL records which. */
    if( !LCD_BLK_ON_LEVEL )
        duty = full - duty;

    ESP_ERROR_CHECK(ledc_set_duty(LCD_BL_LEDC_MODE, LCD_BL_LEDC_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LCD_BL_LEDC_MODE, LCD_BL_LEDC_CHANNEL));
}

static void pulse_gpio_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << PIN_PULSE,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    gpio_set_level(PIN_PULSE, 0);
}

static void backlight_on(void)
{
    backlight_set(LCD_BL_BRIGHTNESS_PCT);
}

static SemaphoreHandle_t s_blit_done;

static bool
blit_done_cb(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *e, void *ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_blit_done, &woken);
    return woken == pdTRUE;
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
        .spi_mode = 3,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(LCD_HOST, &io_cfg, &s_lcd_io));

    /* Given once up front: the first frame has no transfer to wait for. */
    s_blit_done = xSemaphoreCreateBinary();
    ESP_ERROR_CHECK(s_blit_done ? ESP_OK : ESP_ERR_NO_MEM);
    xSemaphoreGive(s_blit_done);
    {
        esp_lcd_panel_io_callbacks_t cbs = { .on_color_trans_done = blit_done_cb };
        ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(s_lcd_io, &cbs, NULL));
    }

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = PIN_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(s_lcd_io, &panel_cfg, &s_lcd_panel));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_lcd_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_lcd_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_lcd_panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(s_lcd_panel, false, false));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(s_lcd_panel, false));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(s_lcd_panel, 0, 0));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_lcd_panel, true));

    uint16_t *buf = heap_caps_malloc(
        240 * 240 * 2,
        MALLOC_CAP_DMA);

    for (int i = 0; i < 240 * 240; i++)
    {
        buf[i] = 0xFFFF; // white
    }

    ESP_ERROR_CHECK(
        esp_lcd_panel_draw_bitmap(
            s_lcd_panel,
            0,
            0,
            240,
            240,
            buf));

    vTaskDelay(pdMS_TO_TICKS(100));
    /* The splash queued a transfer of its own, so the semaphore now holds that
     * completion rather than the priming give. Either way exactly one is
     * outstanding, which is what the loop expects. */
    heap_caps_free(buf);
}

/*
 * NO CONVERSION, AND THAT IS THE POINT.
 *
 * An ST7789 clocks a 16bpp pixel most significant byte first and
 * esp_lcd_panel_draw_bitmap hands it our bytes unaltered, so on this
 * little-endian part a native RGB565 framebuffer arrives with red and blue
 * traded. esp_lcd can swap that for you on its i80 bus and cannot on SPI.
 *
 * The fix is not a pass over the frame. ToriDraw is built with
 * TORIDRAW_PIXEL_FORMAT=TORIDRAW_PF_RGB565_BE, so the palette in flash is
 * already in the panel's order and every kernel writes the panel's own words.
 * The swap costs one XOR of a shift at table-generation time on a desktop,
 * and nothing at all here, forever.
 */
/*
 * THE TRANSFER IS ASYNCHRONOUS, AND WITH ONE FRAMEBUFFER THAT BUYS NOTHING
 * EXCEPT THE NEED FOR A BARRIER.
 *
 * esp_lcd_panel_draw_bitmap queues a DMA transfer and returns -- 0.26 ms to
 * enqueue against 11.5 ms actually on the wire at 80 MHz. That looks like an
 * 11 ms overlap waiting to be taken, and it is not: there is ONE framebuffer,
 * and rendering the next frame writes the same 115 KB the panel is reading.
 * Render and transfer both need the buffer, so they serialise no matter where
 * the wait goes. Measured frame time is 30 ms: 11.5 on the wire, 7.5 of
 * render and animation, and tick quantisation for the rest.
 *
 * A second framebuffer would genuinely overlap them, and does not fit --
 * 115 KB against the ~112 KB of internal DRAM left after the view. That is the
 * trade the baking bought and it is already spent.
 *
 * So blit_wait is a CORRECTNESS barrier, not a performance one. Without it the
 * next frame's clear overwrites pixels the panel is still reading and the
 * image tears. At the old 50 ms period a 23 ms transfer had always finished
 * before the next frame started, which is why there was no barrier and no
 * visible problem; at 30 ms there would be.
 */
/** Block until the panel has finished reading the framebuffer. */
static void blit_wait(void)
{
    xSemaphoreTake(s_blit_done, portMAX_DELAY);
}

static void blit_framebuffer_to_lcd(const toripixel_t *pixel_buffer)
{
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(
        s_lcd_panel, 0, 0, LCD_H_RES, LCD_V_RES, pixel_buffer));
}

void app_main(void)
{
    ESP_LOGI(TAG, "OSRS model renderer starting");

    backlight_init();
    pulse_gpio_init();
    lcd_init();
    backlight_on();

    printf("Remaining heap: %lu\n", (unsigned long)esp_get_free_heap_size());

    /* ToriDraw_Init before anything reads a table. With
     * TORIDRAW_TABLES_PRECOMPUTED the palette and the trigonometry are already
     * const in flash, so this builds only the yaw-pair table -- which is
     * derived from whichever sine and cosine tables are selected and so cannot
     * be frozen. */
    ToriDraw_Init();

    /*
     * THE MODEL AND ITS ANIMATION COME OUT OF FLASH, NOT OFF THE HEAP.
     *
     * Decoding them at boot did not fit and never could: the model wanted
     * ~50 KB, ninety-nine frames ~82 KB, the mini view 159 KB and the
     * framebuffer 115 KB -- 406 KB against 311 KB of internal DRAM -- and the
     * decode fragmented the one large region so badly that 229 KB free came
     * in a largest block of 55 KB, which held neither of the two big buffers.
     *
     * Baked, all of that is .rodata. What is left in RAM is three int16
     * vertex arrays, because ToriDraw_ModelAnimateFrame writes those and
     * nothing else. See tools/bake_model/bake_model.c.
     */
    static struct ToriDraw_Model s_model;
    int16_t *live_vx = malloc((size_t)xmas_baked_vertex_count * sizeof(int16_t));
    int16_t *live_vy = malloc((size_t)xmas_baked_vertex_count * sizeof(int16_t));
    int16_t *live_vz = malloc((size_t)xmas_baked_vertex_count * sizeof(int16_t));
    /* Live, because a type-5 framemap transform writes it every frame; see
     * xmas_baked.h. One byte per face, so it is noise next to the vertices. */
    uint8_t *live_fa = malloc((size_t)xmas_baked_face_count);

    if( !live_vx || !live_vy || !live_vz || !live_fa )
    {
        ESP_LOGE(TAG, "no room for %d live vertices", xmas_baked_vertex_count);
        return;
    }
    xmas_baked_model_init(&s_model, live_vx, live_vy, live_vz, live_fa);

    struct ToriDraw_Model *td_model = &s_model;
    ESP_LOGI(TAG, "baked model: %d vertices, %d faces (%u B of live vertices)",
             td_model->vertex_count, td_model->face_count,
             (unsigned)(3 * xmas_baked_vertex_count * sizeof(int16_t)));

    struct ToriDraw_ModelHandle model_hnd = ToriDraw_ModelHandleOwned(td_model);

    /*
     * THE TWO BIG BLOCKS COME FIRST, BEFORE THE ANIMATION IS DECODED.
     *
     * This ordering is not tidiness, it is the difference between running and
     * not. The view is one 155 KB allocation and the framebuffer another 115
     * KB; the animation is ninety-nine frames of small ones. Decoding the
     * animation first left 229 KB free in a largest block of 55 KB -- enough
     * memory, cut into pieces too small to hold either. The frames fit in the
     * gaps around the big blocks; the big blocks do not fit in the gaps around
     * the frames.
     *
     * The model has to be decoded before this because the limits are computed
     * from it, and it is the one allocation that must precede them.
     */

    /*
     * The animation, also out of flash: base, frames and every transform array
     * are const. Nothing is decoded and nothing is allocated.
     *
     * The bind pose came with the model -- original_vertices_* point at flash
     * and the live arrays were seeded from them by xmas_baked_model_init -- so
     * there is no ToriDraw_ModelCaptureOriginalVertices here. Calling it would
     * be actively wrong: it would try to snapshot the LIVE vertices over a
     * const bind pose.
     *
     * The model is not lit here either. It was lit at bake time, so its face
     * colours are final; lighting again would darken it twice.
     */
    const struct ToriDraw_Animation *anim = xmas_baked_animation();
    if( anim )
    {
        /* A type-5 group is a transparency op. Saying so at boot means a model
         * whose orbs do not fade is one log line away from an answer, rather
         * than a visual comparison against a game client. */
        int alpha_ops = 0;

        for( int i = 0; anim->base && i < anim->base->length; i++ )
            if( anim->base->types[i] == 5 )
                alpha_ops++;

        ESP_LOGI(TAG, "baked animation: %d frames, %d bones", anim->frame_count,
                 anim->base ? anim->base->length : 0);
        ESP_LOGI(TAG, "rig: %d transform groups, %d of them alpha fades%s",
                 anim->base ? anim->base->length : 0, alpha_ops,
                 (alpha_ops && !td_model->face_bones) ? "  <-- BUT THE MODEL HAS NO FACE BONES" : "");
    }
    else
        ESP_LOGW(TAG, "no baked animation; drawing the rest pose");

    /*
     * The view: ToriDraw's whole per-client allocation, out of memory this
     * function owns. Sized from the model, asserted against the buffer, and
     * never grown -- ToriDraw_MiniDrawModel does not allocate.
     */
    struct ToriDraw_MiniLimits limits;
    ToriDraw_MiniLimitsForModel(model_hnd, &limits);

    /*
     * SIZED FOR THE WIDEST POSE, NOT THE BIND POSE -- and this is a
     * correctness requirement, not headroom.
     *
     * depth_levels is 2 * min_z_depth_any_rotation + 2, and the face sort
     * DROPS a face whose depth exceeds the table. Sized from the bind pose it
     * came out at 1832, while this sequence reaches nearly twice the bind
     * extent: on every pose that overflowed the table the whole model was
     * dropped, and the tree flickered in and out about half the time. It looks
     * exactly like a rendering bug and is a sizing one.
     *
     * ToriDraw_MiniLimitsInclude takes the max of every field, which is what
     * it is for -- widening limits across several models. Across poses of one
     * model it does the same job.
     *
     * The same pass finds the widest extent, which the framing below needs for
     * the same reason: a zoom chosen from the bind pose is chosen from the one
     * frame that happens to be smallest.
     */
    int widest_extent = 0;
    struct ToriDraw_BoundsCylinder widest_bounds;

    ToriDraw_ModelSetBoundsCylinder(td_model);
    widest_bounds = *ToriDraw_ModelGetBoundsCylinder(model_hnd);

    for( int f = 0; f < (anim ? anim->frame_count : 0); f++ )
    {
        const struct ToriDraw_BoundsCylinder *b;
        int height, e;

        ToriDraw_ModelAnimateReset(td_model);
        ToriDraw_ModelAnimateFrame(td_model, anim->base, &anim->frames[f]);
        ToriDraw_ModelSetBoundsCylinder(td_model);

        ToriDraw_MiniLimitsInclude(&limits, model_hnd);

        b = ToriDraw_ModelGetBoundsCylinder(model_hnd);
        height = b->max_y - b->min_y;
        e = (2 * b->radius > height) ? 2 * b->radius : height;
        if( e > widest_extent )
        {
            widest_extent = e;
            widest_bounds = *b;
        }
    }

    /* Back to the bind pose, so the first drawn frame is not the last scanned
     * one. */
    if( anim )
        ToriDraw_ModelAnimateReset(td_model);

    /*
     * ONE BOUNDS CYLINDER FOR THE WHOLE LOOP -- the widest pose's -- and it is
     * never recomputed again.
     *
     * ToriDraw_MiniDrawModel derives the camera's vertical centring from the
     * cylinder: `position.y` folds in `(max_y - min_y) / 2` so a tall model
     * frames like a short one. Recomputing the cylinder per frame therefore
     * moves the CAMERA every frame, and on a sequence whose poses differ in
     * height that swings the model clean off a 240x240 panel -- which is what
     * "flickering in and out" was. The frames that vanished still cost 1.3 ms
     * of raster, because every span was being clipped rather than skipped;
     * that is the tell that separates "off-screen" from "culled".
     *
     * A fixed cylinder large enough for every pose keeps the framing still and
     * the cull correct, and drops an O(vertices) pass from every frame.
     */
    td_model->bounds_cylinder = widest_bounds;
    td_model->has_bounds_cylinder = true;

    size_t view_bytes = ToriDraw_MiniViewBytes(&limits);
    ESP_LOGI(TAG,
             "mini view wants %u bytes (max_faces %d, max_verts %d, depth_levels %d)",
             (unsigned)view_bytes, limits.scene.max_faces, limits.scene.max_vertices,
             limits.scene.depth_levels);
    ESP_LOGI(TAG, "internal heap: %u free, %u largest block",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                        MALLOC_CAP_8BIT));

    void *view_memory = heap_caps_aligned_alloc(
        TORIDRAW_ARENA_ALIGN, view_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if( !view_memory )
    {
        ESP_LOGE(TAG, "failed to allocate %u bytes for the mini view", (unsigned)view_bytes);
        return;
    }

    struct ToriDraw_MiniView *view =
        ToriDraw_MiniViewInit(view_memory, view_bytes, &limits);

    int pixel_count = LCD_H_RES * LCD_V_RES;
    /* DMA-capable: with no conversion step this buffer is what the panel
     * reads directly. */
    toripixel_t *pixel_buffer = heap_caps_malloc(
        (size_t)pixel_count * sizeof(toripixel_t), MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if( !pixel_buffer )
    {
        ESP_LOGE(TAG, "failed to allocate framebuffer in internal DRAM");
        return;
    }

    struct ToriDraw_MiniTarget target = {
        .pixels = pixel_buffer,
        .width = LCD_H_RES,
        .height = LCD_V_RES,
        .stride = LCD_H_RES,
    };

    ESP_LOGI(TAG, "internal heap after load: %u free, %u largest block",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                        MALLOC_CAP_8BIT));

    /*
     * REGISTER THE TEXTURES, or the textured faces are never drawn.
     *
     * ToriDraw's texture map starts empty and its raster skips a face whose
     * texture id is absent from it -- silently, with no error, no gap in the
     * depth order and no cost. A model whose textures were never registered
     * therefore looks exactly like a model with no textured faces, which is
     * what this did for the 48 textured faces of the spirit tree.
     *
     * The view sized a texture map because ToriDraw_MiniLimitsInclude saw the
     * model had textures; filling it is still the caller's job.
     */
    {
        const struct XmasBakedTexture *textures = xmas_baked_textures();
        int texture_count = xmas_baked_texture_count();

        for( int i = 0; i < texture_count; i++ )
            ToriDraw_MiniSetTexture(view, textures[i].id, textures[i].texture);

        ESP_LOGI(TAG, "registered %d texture%s", texture_count,
                 texture_count == 1 ? "" : "s");
    }

    struct ToriDraw_MiniPose pose = TORIDRAW_MINI_POSE_DEFAULT;

    /*
     * FRAME THE MODEL TO THE PANEL, rather than taking the pose default.
     *
     * TORIDRAW_MINI_POSE_DEFAULT is the reference client's INVENTORY ICON
     * distance: it is chosen so a sword and a shield look the right size
     * relative to each other in a 32x32 slot, not so that any one model fills
     * a 240x240 screen. On this model it lands a 27-pixel blob in the middle
     * of the panel, which is correct and useless.
     *
     * The projection is coord * UNIT_SCALE / z, so a model whose largest
     * extent is E spans E * 512 / zoom pixels: solve that for the span we
     * want. The bounds cylinder is already computed above, and its radius is
     * a rotation-invariant bound, which is what keeps the model inside the
     * panel at every yaw the animation passes through rather than only at the
     * one it was framed at.
     */
    {
        /*
         * FRAMED AGAINST EVERY POSE, not the bind pose.
         *
         * Framing from the rest pose put half this sequence off the panel:
         * `visible=1` with zero pixels drawn, frame after frame. It is a
         * TRANSFORM animation -- the tree grows and moves -- so its widest
         * pose is nothing like its first, and a zoom chosen from the bind
         * pose is chosen from the one frame that happens to be smallest.
         *
         * Walking all of them once at boot costs about 150 ms and is the only
         * way to get a number that holds for the whole loop. The alternative,
         * re-framing per frame, would make the tree pulse as the camera chased
         * its own bounding cylinder.
         */
        int extent = widest_extent;

        if( extent == 0 )
        {
            const struct ToriDraw_BoundsCylinder* b =
                ToriDraw_ModelGetBoundsCylinder(model_hnd);
            int height = b->max_y - b->min_y;
            extent = (2 * b->radius > height) ? 2 * b->radius : height;
        }

        int target_px = (LCD_V_RES * 4) / 5; /* leave a fifth as margin */
        if( extent > 0 )
            pose.zoom = (extent * 512) / target_px;
        if( pose.zoom < 32 )
            pose.zoom = 32; /* closer than this and the near plane clips it */

        ESP_LOGI(TAG, "widest pose extent %d -> zoom %d", extent, pose.zoom);
    }

    /*
     * EVERY POSE HAS GEOMETRY -- checked once, at boot.
     *
     * This started as a debugging pass and stayed as a guard, because the bug
     * it found is invisible from inside the render loop. The first animation
     * tried here was `pog_spirit_tree_transform`, whose loc is
     * `models=49769,49771` -- a PAIR of trees that the sequence cross-fades by
     * collapsing every vertex of the one that is not current onto a single
     * point. Rendering one model of the pair drew nothing for 48 of 99 poses.
     *
     * From the loop that looks like flicker, and it costs a frame of raster
     * each time, so it does not even look like a cull. From here it is one
     * line. 80 ms at boot to never diagnose that from a serial log again.
     */
    {
        const toripixel_t probe_bg = toripixel_pack_argb8888(FRAME_BG_RGB);
        int n = anim ? anim->frame_count : 0;
        int empty = 0;

        for( int f = 0; f < n; f++ )
        {
            int drawn = 0;

            for( int i = 0; i < pixel_count; i++ )
                pixel_buffer[i] = probe_bg;
            ToriDraw_ModelAnimateReset(td_model);
            ToriDraw_ModelAnimateFrame(td_model, anim->base, &anim->frames[f]);
            ToriDraw_MiniDrawModel(view, model_hnd, &target, &pose);
            for( int i = 0; i < pixel_count && !drawn; i++ )
                if( pixel_buffer[i] != probe_bg )
                    drawn = 1;

            if( !drawn )
                empty++;
        }

        if( empty )
            ESP_LOGW(TAG,
                     "%d of %d poses draw nothing -- the sequence probably "
                     "drives a second model that this one is hidden for",
                     empty, n);
        else
            ESP_LOGI(TAG, "all %d poses draw", n);

        ToriDraw_ModelAnimateReset(td_model);
    }

    /*
     * DOES ALPHA REACH THE PANEL? Answered, not assumed.
     *
     * This model has 420 faces with a transparency byte. Whether the raster
     * honours them is not visible from a photograph of a tree, and it is
     * exactly the class of bug that has bitten this client twice already --
     * the textures were never registered, the frame delays were dropped by a
     * `-split ","[0]`. Both looked like working code and rendered silently
     * wrong.
     *
     * So: draw one pose with the alphas, draw it again with face_alphas NULL
     * (which ToriDraw_TriangleFaceAlpha reads as fully opaque), and compare.
     * If the two images are identical while alpha faces exist, the alpha is
     * being dropped somewhere between here and the span.
     */
    {
        const toripixel_t probe_bg = toripixel_pack_argb8888(FRAME_BG_RGB);
        alphaint_t *saved = td_model->face_alphas;
        uint32_t with = 5381;
        uint32_t without = 5381;
        int alpha_faces = 0;

        for( int i = 0; i < td_model->face_count; i++ )
            if( saved && saved[i] )
                alpha_faces++;

        for( int pass = 0; pass < 2; pass++ )
        {
            uint32_t h = 5381;

            td_model->face_alphas = pass ? NULL : saved;
            for( int i = 0; i < pixel_count; i++ )
                pixel_buffer[i] = probe_bg;
            ToriDraw_MiniDrawModel(view, model_hnd, &target, &pose);
            for( int i = 0; i < pixel_count; i++ )
                h = h * 33u + pixel_buffer[i];

            if( pass )
                without = h;
            else
                with = h;
        }
        td_model->face_alphas = saved;

        if( alpha_faces == 0 )
            ESP_LOGI(TAG, "no alpha faces in this model");
        else if( with == without )
            ESP_LOGW(TAG,
                     "%d faces carry alpha but the image is IDENTICAL with it "
                     "removed -- alpha is not reaching the raster",
                     alpha_faces);
        else
            ESP_LOGI(TAG, "alpha honoured (%d of %d faces)", alpha_faces,
                     td_model->face_count);

        /*
         * EVERY LEVEL, not just alpha as a whole.
         *
         * The test above proves *some* transparency reaches the span. It does
         * not prove all of it does: a model with three distinct alpha values
         * passes that test if only one of them survives, and the two that do
         * not would be silently drawn opaque. So force each level to opaque on
         * its own and check the image moves for each.
         *
         * The levels are transparency bytes -- 0 opaque, 255 hidden -- and 254
         * and 255 are not levels at all but the reference's sentinels for
         * render types 3 and 2. Those are reported rather than tested.
         */
        {
            alphaint_t levels[16];
            int level_n = 0;
            alphaint_t *scratch = malloc((size_t)td_model->face_count);

            for( int i = 0; i < td_model->face_count && saved; i++ )
            {
                int seen = 0;
                if( !saved[i] )
                    continue;
                for( int j = 0; j < level_n; j++ )
                    if( levels[j] == saved[i] )
                        seen = 1;
                if( !seen && level_n < 16 )
                    levels[level_n++] = saved[i];
            }

            for( int L = 0; L < level_n && scratch; L++ )
            {
                uint32_t h = 5381;
                int faces = 0;

                for( int i = 0; i < td_model->face_count; i++ )
                {
                    scratch[i] = saved[i] == levels[L] ? 0 : saved[i];
                    if( saved[i] == levels[L] )
                        faces++;
                }
                td_model->face_alphas = scratch;
                for( int i = 0; i < pixel_count; i++ )
                    pixel_buffer[i] = probe_bg;
                ToriDraw_MiniDrawModel(view, model_hnd, &target, &pose);
                for( int i = 0; i < pixel_count; i++ )
                    h = h * 33u + pixel_buffer[i];
                td_model->face_alphas = saved;

                if( levels[L] >= 254 )
                    ESP_LOGI(TAG, "  alpha %3u: %d faces (render-type sentinel, not blended)",
                             (unsigned)levels[L], faces);
                else if( h == with )
                    ESP_LOGW(TAG,
                             "  alpha %3u: %d faces, forcing them opaque changes NOTHING "
                             "-- this level is not being blended",
                             (unsigned)levels[L], faces);
                else
                    ESP_LOGI(TAG, "  alpha %3u (opacity %u/255): %d faces, blended",
                             (unsigned)levels[L], 255u - (unsigned)levels[L], faces);
            }
            free(scratch);
        }
    }

    ESP_LOGI(TAG, "render loop running");

    int head_yaw = 0;
    HeadYawPhase head_yaw_phase = HEAD_YAW_TO_POS;
    int pulse_tick = 0;
    int pulse_level = 0;
    /* Reported against the CLOCK, not a frame count. A frame count times an
     * assumed period is a rate that quietly lies whenever the loop does not
     * hold its period -- which is how the 30 ms frame time went unnoticed. */
    int64_t report_clock_us = esp_timer_get_time();
    int report_frames = 0;

    /*
     * THE ANIMATION RUNS ON A CLOCK, NOT ON THE RENDER LOOP.
     *
     * A sequence's timing is `frame=id,N` where N is a count of 20 ms client
     * cycles -- 50 Hz -- and it is per frame, not global. `entready` holds
     * every one of its twelve frames for 21 ticks, so a pose lasts 420 ms and
     * the sway takes five seconds. Advancing one frame per loop pass instead
     * played it at 20 poses a second: the same animation, twenty times too
     * fast, which reads as a twitch rather than a tree moving.
     *
     * Deriving the phase from esp_timer rather than counting passes also means
     * the playback rate does not change when the render loop does. Raster time
     * moved from 3.3 ms to 6.1 ms when the textures were registered; on a
     * pass-counted animation that alone would have slowed the tree down.
     */
    const int64_t anim_tick_us = 20000; /* 50 Hz, the reference client cycle */
    int64_t anim_clock_us = esp_timer_get_time();
    int anim_frame = 0;
    int anim_tick = 0;
    /* Advances since the last report. Counted rather than inferred: sampling
     * the frame INDEX once a second on a twelve-frame loop aliases, and read
     * as correct playback while the sequence was running twenty times too
     * fast. A rate cannot alias. */
    int anim_advances = 0;
    /* The panel transfer is synchronous, so it is frame time like any other
     * stage; measuring it says whether the loop is raster-bound or wire-bound
     * before anything is optimised on a guess. */
    uint32_t blit_cycles = 0;
    const int pulse_period_ticks = 2000 / LOOP_PERIOD_MS;

    printf("Remaining heap before loop: %lu\n", (unsigned long)esp_get_free_heap_size());

    /* Native RGB565, not swapped: the swap belongs at the wire, and
     * blit_framebuffer_to_lcd is where it happens. */
    const toripixel_t bg = toripixel_pack_argb8888(FRAME_BG_RGB);
    TickType_t loop_deadline = xTaskGetTickCount();

    while( true )
    {
        /* The panel is done with the buffer; it is ours to overwrite. */
        blit_wait();

        for( int i = 0; i < pixel_count; i++ )
            pixel_buffer[i] = bg;

        pose.yaw = head_yaw;

        /*
         * Pose the model, then draw it.
         *
         * RESET FIRST, EVERY FRAME. ToriDraw_ModelAnimateFrame moves the live
         * vertices from wherever they are; without the reset each frame would
         * compose on the last and the tree would wander off the panel within a
         * second. The reset is what makes a frame index a POSITION rather than
         * a step.
         *
         * The bounds cylinder is NOT recomputed here. It was fixed once, to
         * the widest pose, above -- see there for why moving it per frame
         * moves the camera and throws the model off the panel.
         */
        uint32_t anim_cycles = 0;
        if( anim && anim->frames && anim->frame_count > 0 )
        {
            uint32_t a0 = esp_cpu_get_cycle_count();
            ToriDraw_ModelAnimateReset(td_model);
            ToriDraw_ModelAnimateFrame(td_model, anim->base, &anim->frames[anim_frame]);
            anim_cycles = esp_cpu_get_cycle_count() - a0;

            /*
             * Consume whole ticks, and keep the remainder in the clock. A
             * loop pass that overruns its period does not lose animation
             * time, and one that is early advances nothing -- which is what
             * makes this a rate rather than a ratio to the frame rate.
             */
            int64_t now = esp_timer_get_time();
            int elapsed = (int)((now - anim_clock_us) / anim_tick_us);

            if( elapsed > 0 )
            {
                anim_clock_us += (int64_t)elapsed * anim_tick_us;
                anim_tick += elapsed;

                /* A frame with no recorded delay still has to advance, or a
                 * sequence that omits them stops dead on frame 0. */
                for( int hold = anim_frame_hold(anim, anim_frame); anim_tick >= hold;
                     hold = anim_frame_hold(anim, anim_frame) )
                {
                    anim_tick -= hold;
                    anim_advances++;
                    if( ++anim_frame >= anim->frame_count )
                        anim_frame = 0;
                }
            }
        }

        /* false means the model projected to nothing -- behind the camera or
         * culled against the target. An ordinary answer for a pose we chose,
         * and the target is left holding the background we just wrote. */
        uint32_t t0 = esp_cpu_get_cycle_count();
        bool visible = ToriDraw_MiniDrawModel(view, model_hnd, &target, &pose);
        uint32_t raster_cycles = esp_cpu_get_cycle_count() - t0;

        /*
         * What actually landed in the buffer, once a second.
         *
         * A render loop that draws nothing looks exactly like a render loop
         * that draws correctly when the only way to check is to look at a
         * panel that may not be plugged in. Counting the pixels that differ
         * from the background separates "the raster ran" from "the raster
         * ran and produced geometry", and it is the difference between
         * debugging the kernel and debugging the wiring.
         */
        report_frames++;
        if( esp_timer_get_time() - report_clock_us >= 1000000 )
        {
            int64_t window_us = esp_timer_get_time() - report_clock_us;
            long drawn = 0;
            for( int i = 0; i < pixel_count; i++ )
                if( pixel_buffer[i] != bg )
                    drawn++;
            ESP_LOGI(TAG,
                     "frame %d: %.1f fps, %.1f poses/s  visible=%d  %ld px  "
                     "anim %.2f ms  raster %.2f ms  blit %.2f ms",
                     anim_frame, report_frames * 1e6 / (double)window_us,
                     anim_advances * 1e6 / (double)window_us, (int)visible, drawn,
                     (double)anim_cycles / (CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ * 1000.0),
                     (double)raster_cycles / (CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ * 1000.0),
                     (double)blit_cycles / (CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ * 1000.0));
            report_clock_us = esp_timer_get_time();
            report_frames = 0;
            anim_advances = 0;
        }

        uint32_t b0 = esp_cpu_get_cycle_count();
        blit_framebuffer_to_lcd(pixel_buffer);
        blit_cycles = esp_cpu_get_cycle_count() - b0;

        if( ++pulse_tick >= pulse_period_ticks )
        {
            pulse_tick = 0;
            pulse_level ^= 1;
            gpio_set_level(PIN_PULSE, pulse_level);
        }

        head_yaw = head_yaw_advance(head_yaw, &head_yaw_phase);

        /*
         * A PERIOD, not a gap.
         *
         * vTaskDelay sleeps for its argument ON TOP of the work already done,
         * so a 20 ms delay after 7 ms of render is a 27 ms frame -- which is
         * what this was, and it showed up as the once-a-second report landing
         * every 1.4 s. vTaskDelayUntil sleeps until the deadline instead, so
         * the period is 20 ms whatever the frame cost, and it stops drifting
         * away from the 20 ms animation tick it is meant to match.
         */
        vTaskDelayUntil(&loop_deadline, pdMS_TO_TICKS(LOOP_PERIOD_MS));
    }
}
