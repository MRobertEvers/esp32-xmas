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
#include "esp_netif_ip_addr.h"

#include "toridraw.h"
#include "toridraw_math.h"
#include "toridraw_mini.h"
#include "toridraw_model_transform.h"
#include "toridraw_animation.h"
#include "xmas_baked.h"

#include "captive_dns.h"
#include "http_api.h"
#include "model_store.h"
#include "net.h"
#include "display.h"
#include "settings.h"
#include "ui.h"

static const char *TAG = "xmas_model";

#define LCD_HOST                SPI2_HOST
#define PIN_LCD_SCLK            12
#define PIN_LCD_MOSI            11
#define PIN_LCD_CS              -1
#define PIN_LCD_DC              9
#define PIN_LCD_RST             8
#define PIN_LCD_BLK             7
#define PIN_PULSE               4
/*
 * BOOT, the only button on this board.
 *
 * It is pulled up and grounded when pressed, and the ROM only looks at it
 * during reset -- so once the app is running it is an ordinary input and the
 * one piece of physical input this device has.
 */
#define PIN_BOOT_BUTTON         0

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
 * Render period. 30 ms, which is a period this loop can actually hold.
 *
 * It was 20, chosen when a frame cost about 12 ms. Strips changed that: the
 * model is projected and depth-sorted once per strip, and four sorts of 1000
 * faces measure at 23-25 ms a frame. A period SHORTER than the work is not a
 * slower frame rate, it is a loop that never sleeps -- vTaskDelayUntil finds
 * its deadline already past and returns immediately, so the idle task on this
 * core never runs and the task watchdog fires every five seconds. That is
 * exactly what the log showed.
 *
 * 30 ms leaves real slack at ~24 ms of work, and the animation does not care:
 * its phase comes from esp_timer, not from counting passes, so playback rate
 * is unchanged by the render period.
 */
#define LOOP_PERIOD_MS          30
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

/*
 * How long BOOT must be held to forget the network, in milliseconds.
 *
 * Long, deliberately. The alternative to holding it is reflashing NVS over a
 * cable, so this has to be discoverable -- but it throws away the credentials
 * of a device that may be hanging on a tree, and a brush against the button
 * must not do that. Five seconds is past any accident and short enough to
 * find by holding it and watching the panel.
 */
#define REPROVISION_HOLD_MS     5000

/* How long a QR stays up before the model takes the panel back. Long enough to
 * fetch a phone from the next room, short enough that the device is not showing
 * a setup screen to a room full of guests. The unprovisioned one lingers
 * longer, because acting on it means typing a password. */
#define SETUP_SCREEN_MS         15000
#define SETUP_QR_MS             60000

/*
 * ONE WHOLE-FRAME BUFFER. The strip experiment is over; this is what is left
 * of it, and the comment is here so it is not attempted again.
 *
 * The idea was to draw 240x240 as four 240x60 strips into two small buffers,
 * freeing 57.6 KB for WiFi. It does not work with this library, and the reason
 * is not obvious from the API: ToriDraw's raster centres the model on the
 * middle of the CLIP RECTANGLE. Not on view_port.y_center -- measured, that
 * value reaches only the cull box and the pick path, and moving it (or
 * pose.offset_y, which is all it carries) moves the image not at all. Not on
 * the viewport height either.
 *
 * So a clip window over the top 60 rows does not show the top 60 rows of the
 * scene; it re-centres the whole model into them. Four strips are four whole
 * models stacked down the panel, which is what the panel showed. There is no
 * combination of viewport, clip rectangle and pose that renders a sub-window
 * of a larger frame -- the only vertical lever is position.y, in world units
 * through a perspective divide, which cannot produce an exact pixel shift for
 * vertices at different depths.
 *
 * What is left is STRIP_COUNT 1 and ToriDraw_MiniDrawModel, which is where
 * this started. The loop below still walks strips so that raising STRIP_COUNT
 * is one edit -- but read the paragraph above before doing it, because the
 * library will not draw what you expect.
 *
 * The 57.6 KB has to come from somewhere else. The candidate is the view arena
 * -- 32 KB of it is the batched raster's stash, which only exists because this
 * build arms the Xtensa assembly kernels.
 *
 * Static, in .bss, so the size is a link-time fact: ask for more than the part
 * has and the build fails with a number here rather than the device failing as
 * a black panel in a living room. Internal .bss is DMA-capable, and this is
 * what the panel's DMA reads directly since there is no conversion pass.
 */
#define STRIP_ROWS    LCD_V_RES
#define STRIP_COUNT   (LCD_V_RES / STRIP_ROWS)
/* One buffer, so the raster waits for the transfer it would otherwise
 * overwrite. Two would overlap render with DMA and cost another 115 KB. */
#define STRIP_BUFFERS 1

_Static_assert(LCD_V_RES % STRIP_ROWS == 0,
               "the panel height must divide into whole strips");

static __attribute__((aligned(4)))
toripixel_t s_strip[STRIP_BUFFERS][LCD_H_RES * STRIP_ROWS];

/*
 * The mini view's arena.
 *
 * MEASURED ON THE PART, not on a host. For the spirit tree -- 534 vertices,
 * 1000 faces, depth_levels 1204 across its sequence, one texture -- the view
 * comes to 106,492 bytes.
 *
 * That is 31 KB more than the same model costs in a host build of the same
 * library, and the difference is not noise. This firmware arms the Xtensa
 * presorted-run kernels, which arms TORIDRAW_RASTER_BATCH, which makes the
 * arena provision the batched walk's y-ordered stash at 32 bytes per face. A
 * tool compiled without those kernels sizes an arena without it and reports a
 * number 32 KB too small. Sizing this buffer from that number is what put
 * "model needs a 106492 byte view; the arena is 81920" in the boot log.
 *
 * The dominant term is FACES, and steeply: on this lane 1000 faces needs about
 * 106 KB, 2000 about 190 KB, and 4000 about 376 KB -- which no configuration
 * of this part can hold beside a framebuffer. That is the ceiling a downloaded
 * model has to be checked against, and it is why the size lives here as one
 * number rather than being whatever the last model happened to want.
 *
 * 112 KB leaves about 5 KB spare for the current model. app_main refuses a
 * model that wants more rather than overrunning it, and logs the spare so this
 * can be tightened against evidence.
 */
#define MODEL_VIEW_ARENA_BYTES (80 * 1024)
static _Alignas(TORIDRAW_ARENA_ALIGN) uint8_t s_view_arena[MODEL_VIEW_ARENA_BYTES];

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

static void boot_button_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << PIN_BOOT_BUTTON,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
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

void display_set_brightness(int pct)
{
    backlight_set(pct);
}

/*
 * The camera the web page drives.
 *
 * Written by an HTTP handler, read by the render loop, with no lock between
 * them -- see display.h for why that is sound and what the worst case is.
 * `s_default_zoom` is what the boot framing computed for this model, which is
 * both the starting point and what "reset" means.
 */
static struct DisplayPose s_pose = { .spin = true };
static int s_default_zoom = 2000;

void
display_get_pose(struct DisplayPose *out)
{
    if( out )
        *out = s_pose;
}

void
display_set_pose(const struct DisplayPose *pose)
{
    struct DisplayPose next;

    if( !pose )
        return;

    next = *pose;

    /* Clamped here rather than trusted, because these arrive from a form. A
     * zoom of zero is a divide the projection does not survive, and an angle
     * outside 0..2047 indexes the trigonometry tables out of range. */
    next.yaw = ((next.yaw % 2048) + 2048) % 2048;
    next.pitch = ((next.pitch % 2048) + 2048) % 2048;

    if( next.zoom < 32 )
        next.zoom = 32;
    if( next.zoom > 20000 )
        next.zoom = 20000;

    if( next.offset_y < -LCD_V_RES )
        next.offset_y = -LCD_V_RES;
    if( next.offset_y > LCD_V_RES )
        next.offset_y = LCD_V_RES;

    s_pose = next;
}

int
display_default_zoom(void)
{
    return s_default_zoom;
}

static SemaphoreHandle_t s_blit_done;

static bool
blit_done_cb(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *e, void *ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_blit_done, &woken);
    return woken == pdTRUE;
}

/*
 * THE TRANSFER IS ASYNCHRONOUS, AND WITH STRIPS THAT FINALLY BUYS SOMETHING.
 *
 * esp_lcd_panel_draw_bitmap queues a DMA transfer and returns -- 0.26 ms to
 * enqueue against 11.5 ms actually on the wire at 80 MHz for a whole frame.
 * With ONE whole-frame buffer that overlap could never be taken: rendering the
 * next frame wrote the same 115 KB the panel was reading, so render and
 * transfer serialised wherever the wait went, and blit_wait was purely a
 * correctness barrier against tearing. Measured frame time was 30 ms.
 *
 * Two 28.8 KB strip buffers cost half of what one whole frame did (see
 * s_strip), so the second buffer that never fitted at 115 KB fits easily at
 * 28.8. Strip n+1 is now rendered while the panel is still reading strip n,
 * and the barrier below waits only for the buffer actually about to be
 * overwritten -- so the loop costs about max(render, transfer) rather than
 * their sum.
 */
/** Transfers queued and not yet reported complete by the ISR callback. */
static int s_blits_pending;

/**
 * Wait until at most `keep` transfers are still outstanding.
 *
 * With STRIP_BUFFERS buffers used round-robin, the buffer about to be drawn
 * into is the one used STRIP_BUFFERS transfers ago, so it is free exactly when
 * fewer than that many are in flight. Draining to `keep = STRIP_BUFFERS - 1`
 * before each render is therefore the whole synchronisation: it never waits
 * for the transfer that could still be overlapped with this render, and never
 * lets the raster into a buffer the panel is reading.
 */
static void blit_drain_to(int keep)
{
    while( s_blits_pending > keep )
    {
        xSemaphoreTake(s_blit_done, portMAX_DELAY);
        s_blits_pending--;
    }
}

/** Block until every queued transfer has completed. */
static void blit_wait(void)
{
    blit_drain_to(0);
}

/** Queue strip `index` for transfer. Returns once it is QUEUED, not once the
 *  panel has read it -- which is the point: the next strip renders meanwhile. */
static void blit_strip(const toripixel_t *pixels, int index)
{
    int y0 = index * STRIP_ROWS;

    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(
        s_lcd_panel, 0, y0, LCD_H_RES, y0 + STRIP_ROWS, pixels));
    s_blits_pending++;
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

    /*
     * COUNTING, not binary, and it starts empty.
     *
     * A frame now queues one transfer per strip and the ISR gives once per
     * completion, so two completions can land before the loop takes either.
     * A binary semaphore collapses those into one and the count of what is in
     * flight drifts -- which shows up as the raster drawing into a buffer the
     * panel is still reading, i.e. as tearing that comes and goes. s_blits_pending
     * is the count; this only carries the wakeups.
     */
    s_blit_done = xSemaphoreCreateCounting(io_cfg.trans_queue_depth, 0);
    ESP_ERROR_CHECK(s_blit_done ? ESP_OK : ESP_ERR_NO_MEM);
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

    /*
     * A white splash, so a panel that is powered but never written is
     * distinguishable from one that is not powered at all.
     *
     * Through the strip buffers rather than a 115 KB temporary: this runs
     * before the network comes up, when the heap is still at its emptiest, and
     * a boot-time allocation that only succeeds because it is early is a
     * failure waiting for the first time something else gets there first.
     */
    for( int s = 0; s < STRIP_COUNT; s++ )
    {
        toripixel_t *buf = s_strip[s % STRIP_BUFFERS];

        blit_drain_to(STRIP_BUFFERS - 1);
        for( int i = 0; i < LCD_H_RES * STRIP_ROWS; i++ )
            buf[i] = 0xFFFF;
        blit_strip(buf, s);
    }
    blit_wait();
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

/**
 * Draw one whole logical 240x240 frame, a strip at a time.
 *
 * `pose` is in LOGICAL frame coordinates -- offset_y 0 centres the model on
 * the panel, not on a strip. Each strip is drawn with the camera's vertical
 * centre shifted so the union of the strips is the frame the caller asked for;
 * ToriDraw clips each strip against its own target, so a face that lands
 * entirely in the other strip costs a cull and nothing else.
 *
 * `on_strip`, when given, is handed each strip's pixels after it is rendered
 * and before it is queued. The boot diagnostics inspect what was drawn, and
 * there is no whole frame left to inspect -- so they fold over the strips
 * instead, in order, which is equivalent for both a count and a hash.
 *
 * Returns whether the model projected to anything in ANY strip. A model
 * entirely inside one strip is invisible in the other, and reporting that as
 * "not visible" would make an ordinary frame look like a culled one.
 */
static bool
frame_render(struct ToriDraw_MiniView *view,
             struct ToriDraw_ModelHandle hnd,
             const struct ToriDraw_MiniPose *pose,
             toripixel_t bg,
             bool blit,
             void (*on_strip)(const toripixel_t *pixels, int count, void *ctx),
             void *ctx)
{
    bool visible = false;

    for( int s = 0; s < STRIP_COUNT; s++ )
    {
        toripixel_t *buf = s_strip[s % STRIP_BUFFERS];
        /* Only the buffer this strip is about to overwrite has to be free;
         * the other strip's transfer may still be running, and should be. */
        blit_drain_to(STRIP_BUFFERS - 1);

        for( int i = 0; i < LCD_H_RES * STRIP_ROWS; i++ )
            buf[i] = bg;

        struct ToriDraw_MiniTarget target = {
            .pixels = buf,
            .width = LCD_H_RES,
            .height = STRIP_ROWS,
            .stride = LCD_H_RES,
        };

        if( ToriDraw_MiniDrawModel(view, hnd, &target, pose) )
            visible = true;

        if( on_strip )
            on_strip(buf, LCD_H_RES * STRIP_ROWS, ctx);

        if( blit )
            blit_strip(buf, s);
    }

    return visible;
}

/**
 * What every one of the boot checks and the frame report wants to know about
 * a frame, gathered in the one pass over the pixels that strips allow.
 *
 * `hash` folds every pixel in strip order, so it identifies a whole frame even
 * though no whole frame exists in memory at any instant. That is what the
 * alpha A/B test compares: the same model rendered twice, once with a
 * transparency level forced opaque, must hash differently or that level never
 * reached the raster.
 */
struct StripScan
{
    toripixel_t bg;
    long drawn;
    uint32_t hash;
};

/**
 * Put a QR-and-text screen on the panel, through the same strips the model
 * uses. Blocks until the last strip has been transferred, because the caller's
 * next act is usually to wait for something rather than to draw again.
 */
static void
screen_present(const struct UiScreen *screen)
{
    ui_screen_prepare(screen);

    for( int s = 0; s < STRIP_COUNT; s++ )
    {
        toripixel_t *buf = s_strip[s % STRIP_BUFFERS];

        blit_drain_to(STRIP_BUFFERS - 1);
        ui_screen_paint(screen, buf, s * STRIP_ROWS, STRIP_ROWS, LCD_H_RES);
        blit_strip(buf, s);
    }

    blit_wait();
}

/**
 * Watch BOOT, and forget the network if it is held.
 *
 * Called once a frame from the render loop rather than given a task of its
 * own: it is two register reads and a counter, and a task would cost a stack
 * for the privilege of doing that at a lower rate.
 *
 * `held_ms` is the caller's counter, so this stays a pure function of the pin
 * and the period.
 */
static void
boot_button_poll(int *held_ms, int period_ms)
{
    /* Pulled up, so LOW is pressed. */
    if( gpio_get_level(PIN_BOOT_BUTTON) != 0 )
    {
        *held_ms = 0;
        return;
    }

    *held_ms += period_ms;

    if( *held_ms < REPROVISION_HOLD_MS )
        return;

    ESP_LOGW(TAG, "BOOT held; forgetting the network and restarting");
    settings_forget_wifi();

    {
        /* Say so on the panel before restarting. Coming back up on the setup
         * QR is the confirmation, but that is several seconds away and a
         * device that appears to ignore a five-second press reads as broken. */
        struct UiScreen screen = {
            .title = "FORGETTING",
            .line1 = "WIFI CLEARED",
            .line3 = "RESTARTING FOR SETUP",
        };

        blit_wait();
        screen_present(&screen);
        vTaskDelay(pdMS_TO_TICKS(1500));
    }

    esp_restart();
}

static void
strip_scan(const toripixel_t *pixels, int count, void *ctx)
{
    struct StripScan *scan = ctx;

    for( int i = 0; i < count; i++ )
    {
        if( pixels[i] != scan->bg )
            scan->drawn++;
        scan->hash = scan->hash * 33u + pixels[i];
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "OSRS model renderer starting");

    backlight_init();
    pulse_gpio_init();
    boot_button_init();
    lcd_init();
    backlight_on();

    net_heap_report("at boot");

    /*
     * THE NETWORK COMES UP BEFORE THE MODEL, and the order is deliberate.
     *
     * Both of the model's big buffers are static now (see s_framebuffer and
     * s_view_arena), so nothing here can take memory the renderer needs. What
     * the order does buy is a boot that shows the provisioning QR while the
     * radio is settling, rather than after -- and a heap report either side of
     * the most expensive thing this firmware does.
     */
    ESP_ERROR_CHECK(settings_init());
    backlight_set(settings_get()->brightness_pct);
    ESP_ERROR_CHECK(net_init());

    bool provisioned = false;

    if( settings_have_wifi() )
    {
        const struct Settings *cfg = settings_get();

        provisioned = net_start_sta(cfg->wifi_ssid, cfg->wifi_pass, 20000) == ESP_OK;

        /* Credentials that no longer work -- a moved device, a changed
         * password -- are not a reason to sit on a blank panel. Fall through
         * to the AP so the user can hand it new ones. */
        if( !provisioned )
            ESP_LOGW(TAG, "stored network unreachable; falling back to provisioning");
    }

    if( !provisioned )
    {
        /*
         * PROVISIONING ENDS HERE, IN A RESTART, NOT IN A HANDOVER.
         *
         * The panel shows a QR the phone's own camera can act on and the
         * device waits. When the portal receives credentials it saves them and
         * reboots (see http_api.c), so this path never has to tear an access
         * point down and bring a station up in its place -- which is the part
         * of a mode switch that goes wrong, and it would go wrong on a device
         * whose only output is a 240-pixel panel.
         */
        char ssid[NET_AP_SSID_MAX];
        char pass[NET_AP_PASS_MAX];
        char payload[96];
        char line1[NET_AP_SSID_MAX + 8];
        char line2[NET_AP_PASS_MAX + 8];

        net_start_ap();
        net_ap_credentials(ssid, sizeof(ssid), pass, sizeof(pass));

        /* 192.168.4.1, which is esp_netif's default address for the SoftAP.
         * The DNS responder points every name here and the portal redirects
         * here, so the two have to agree with the netif and with each other. */
        captive_dns_start(ESP_IP4TOADDR(192, 168, 4, 1));
        http_api_start(true);

        /*
         * The standard Wi-Fi join payload, which iOS Camera and Android both
         * act on natively -- "Join network" appears with no app installed.
         * That is the whole reason provisioning is an access point rather than
         * BLE: a camera cannot open a BLE session, and Safari has no Web
         * Bluetooth to fall back to.
         *
         * Nothing here is escaped because nothing here needs escaping: the
         * SSID is XMAS-<hex> and net_ap_credentials draws the password from an
         * alphabet with the five reserved characters removed.
         */
        snprintf(payload, sizeof(payload), "WIFI:T:WPA;S:%s;P:%s;;", ssid, pass);
        snprintf(line1, sizeof(line1), "%s", ssid);
        snprintf(line2, sizeof(line2), "%s", pass);

        struct UiScreen screen = {
            .title = "SCAN TO SET UP",
            .qr_payload = payload,
            .line1 = line1,
            .line2 = line2,
            .line3 = "OR JOIN THIS WIFI BY HAND",
        };

        screen_present(&screen);
        ESP_LOGI(TAG, "waiting to be provisioned");

        /*
         * THE QR IS SHOWN FOR A WHILE, AND THEN THE MODEL RUNS ANYWAY.
         *
         * Sitting on the setup screen forever is the obvious thing and the
         * wrong one. This is an ornament: a device that cannot reach WiFi --
         * because the router is off, or it has been carried to a house it was
         * never provisioned for -- should still light up and draw its tree.
         * Refusing to render until it is configured makes a network failure
         * look like a broken device.
         *
         * The access point and the portal stay up the whole time, so the setup
         * flow is still available to anyone who joins; the panel simply stops
         * advertising it after long enough for someone to have scanned it.
         */
        vTaskDelay(pdMS_TO_TICKS(SETUP_QR_MS));
        ESP_LOGI(TAG, "still unprovisioned; showing the model with the setup AP up");
    }
    else
    {
        http_api_start(false);

        /*
         * The device's own address, as a QR.
         *
         * This is the answer to "how does the phone find it once it is on the
         * network", and it is worth having a screen for: the device knows its
         * address, so nothing has to depend on mDNS resolving, on a router's
         * client list, or on the user reading a number off a panel and typing
         * it. Scanning it opens the control page in Safari, which -- unlike
         * the captive sheet the setup form runs in -- is a real browser.
         */
        char ip[16];
        char host[NET_AP_SSID_MAX] = { 0 };
        static char payload[32];
        static char line1[24];
        static char line2[NET_AP_SSID_MAX + 8];

        net_ip_str(ip, sizeof(ip));
        net_start_mdns(host, sizeof(host));

        /* One display answers to xmas.local, elected among whoever is on the
         * network. See net.h -- the point is that they do not fight over it. */
        net_start_alias_election();

        /*
         * Find the model server, if nobody has said where it is.
         *
         * The setup form takes an address for it, and this is what makes that
         * field optional rather than necessary: a server that announces itself
         * over Bonjour is one the display can configure itself from. Only when
         * unset -- an address someone typed is a decision, and rediscovering
         * over the top of it would silently move the device to a different
         * server the next time one appeared.
         */
        if( !settings_get()->server_url[0] )
        {
            struct Settings next = *settings_get();

            if( net_find_model_server(next.server_url, sizeof(next.server_url), 1500) )
                settings_set(&next);
        }

        /*
         * The QR carries the ADDRESS and the caption carries the NAME.
         *
         * The address always works and needs nothing typed; the name survives
         * the router handing out a different address, and iOS resolves it
         * natively. Neither is a substitute for the other, so the screen shows
         * both.
         */
        snprintf(payload, sizeof(payload), "http://%s/", ip);
        snprintf(line1, sizeof(line1), "%s", ip);
        snprintf(line2, sizeof(line2), "%s.LOCAL", host);

        struct UiScreen screen = {
            .title = "SCAN TO CONTROL",
            .qr_payload = payload,
            .line1 = line1,
            .line2 = host[0] ? line2 : NULL,
            .line3 = "OR OPEN EITHER ADDRESS",
        };

        screen_present(&screen);
        vTaskDelay(pdMS_TO_TICKS(SETUP_SCREEN_MS));
    }

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
    /*
     * A DOWNLOADED MODEL IF THERE IS ONE, the built-in model otherwise.
     *
     * The two arrive by different routes and are then identical: the baked one
     * points its arrays at .rodata, the downloaded one at a mapped flash
     * partition, and from here down nothing distinguishes them. That is the
     * whole design -- a bundle is the bake's output in binary, so the device
     * reads a downloaded model exactly as cheaply as a compiled-in one, and
     * decodes neither.
     *
     * The built-in model is also the fallback, and it is what makes a failed
     * or refused download survivable: a device that cannot load its slot shows
     * the spirit tree rather than a black panel.
     */
    static struct ToriDraw_Model s_model;
    const struct ModelBundle *bundle;
    struct ToriDraw_Model *td_model;

    model_store_init(MODEL_VIEW_ARENA_BYTES);
    bundle = model_store_active();

    /*
     * Advertise what is showing, now that it is known -- mDNS came up earlier,
     * with the address, but the model is only chosen here. It goes in the
     * service's TXT record so another display can list this one and say what
     * it is showing without opening a connection to it.
     */
    net_mdns_set_model(bundle ? bundle->header->name : "built-in");

    if( bundle )
    {
        td_model = (struct ToriDraw_Model *)&bundle->model;
        ESP_LOGI(TAG, "downloaded model \"%.*s\": %d vertices, %d faces", XMB_NAME_MAX,
                 bundle->header->name, td_model->vertex_count, td_model->face_count);
    }
    else
    {
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

        td_model = &s_model;
        ESP_LOGI(TAG, "baked model: %d vertices, %d faces (%u B of live vertices)",
                 td_model->vertex_count, td_model->face_count,
                 (unsigned)(3 * xmas_baked_vertex_count * sizeof(int16_t)));
    }

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
    const struct ToriDraw_Animation *anim =
        bundle ? (bundle->has_animation ? &bundle->animation : NULL) : xmas_baked_animation();
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

    if( bundle )
    {
        /*
         * A BUNDLE CARRIES BOTH ANSWERS, so the scan below does not run.
         *
         * The baker did this pass -- it is arithmetic about the model and its
         * sequence, not about the device, and doing it here costs a walk of
         * every pose before the first frame can be drawn. The limits come
         * from the header the same way, which is also what the download
         * checked the arena against, so the number the slot was accepted for
         * is the number the view is built with.
         */
        int height;

        widest_bounds = td_model->bounds_cylinder;
        height = widest_bounds.max_y - widest_bounds.min_y;
        widest_extent = (2 * widest_bounds.radius > height) ? 2 * widest_bounds.radius : height;

        limits.scene.max_faces = bundle->header->limit_max_faces;
        limits.scene.max_vertices = bundle->header->limit_max_vertices;
        limits.scene.depth_levels = bundle->header->limit_depth_levels;
        limits.scene.textures = bundle->header->limit_textures != 0;
    }

    for( int f = 0; f < ((anim && !bundle) ? anim->frame_count : 0); f++ )
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

    if( view_bytes > sizeof(s_view_arena) )
    {
        ESP_LOGE(TAG, "model needs a %u byte view; the arena is %u",
                 (unsigned)view_bytes, (unsigned)sizeof(s_view_arena));
        return;
    }
    ESP_LOGI(TAG, "view arena: %u of %u bytes used, %u spare",
             (unsigned)view_bytes, (unsigned)sizeof(s_view_arena),
             (unsigned)(sizeof(s_view_arena) - view_bytes));

    struct ToriDraw_MiniView *view =
        ToriDraw_MiniViewInit(s_view_arena, view_bytes, &limits);

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
        /*
         * The two sources hand over the same two things -- a ToriDraw_Texture
         * and the CACHE ID its faces name it by -- from different places, so
         * the registration below is written once over a pair of pointers
         * rather than twice.
         */
        int texture_count = bundle ? bundle->texture_count : xmas_baked_texture_count();
        const struct XmasBakedTexture *baked = bundle ? NULL : xmas_baked_textures();
        int registered = 0;

        for( int i = 0; i < texture_count; i++ )
        {
            int id = bundle ? bundle->texture_descs[i].id : baked[i].id;
            struct ToriDraw_Texture *tex =
                bundle ? &bundle->textures[i] : baked[i].texture;

            /*
             * Range-checked here, not left to ToriDraw's assert.
             *
             * The texture map is indexed by the model's own cache id and holds
             * TORIDRAW_TEXTURE_ID_CAPACITY of them -- 256, chosen to keep the
             * view arena down (see components/toridraw/CMakeLists.txt). An id
             * past that writes off the end of the map, and the library's guard
             * against it is an assert, which a release build removes. A model
             * this device cannot fully texture should draw untextured faces
             * and say so, not corrupt the arena.
             */
            if( id < 0 || id >= TORIDRAW_TEXTURE_ID_CAPACITY )
            {
                ESP_LOGW(TAG, "texture id %d is past the %d-entry map; its faces will not draw",
                         id, TORIDRAW_TEXTURE_ID_CAPACITY);
                continue;
            }

            ToriDraw_MiniSetTexture(view, id, tex);
            registered++;
        }

        ESP_LOGI(TAG, "registered %d of %d texture%s", registered, texture_count,
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

        /* The framing is the starting point for the camera the page drives,
         * and what its reset button returns to. */
        s_default_zoom = pose.zoom;
        s_pose.zoom = pose.zoom;
        s_pose.pitch = pose.pitch;
        s_pose.offset_y = pose.offset_y;
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
            struct StripScan scan = { .bg = probe_bg };

            ToriDraw_ModelAnimateReset(td_model);
            ToriDraw_ModelAnimateFrame(td_model, anim->base, &anim->frames[f]);
            frame_render(view, model_hnd, &pose, probe_bg, false, strip_scan, &scan);

            if( scan.drawn == 0 )
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
            struct StripScan scan = { .bg = probe_bg, .hash = 5381 };

            td_model->face_alphas = pass ? NULL : saved;
            frame_render(view, model_hnd, &pose, probe_bg, false, strip_scan, &scan);

            if( pass )
                without = scan.hash;
            else
                with = scan.hash;
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
                struct StripScan scan = { .bg = probe_bg, .hash = 5381 };
                uint32_t h;
                int faces = 0;

                for( int i = 0; i < td_model->face_count; i++ )
                {
                    scratch[i] = saved[i] == levels[L] ? 0 : saved[i];
                    if( saved[i] == levels[L] )
                        faces++;
                }
                td_model->face_alphas = scratch;
                frame_render(view, model_hnd, &pose, probe_bg, false, strip_scan, &scan);
                h = scan.hash;
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
    int boot_held_ms = 0;
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
    const int pulse_period_ticks = 2000 / LOOP_PERIOD_MS;

    printf("Remaining heap before loop: %lu\n", (unsigned long)esp_get_free_heap_size());

    /* Native RGB565, not swapped: the swap belongs at the wire, and
     * blit_framebuffer_to_lcd is where it happens. */
    const toripixel_t bg = toripixel_pack_argb8888(FRAME_BG_RGB);
    TickType_t loop_deadline = xTaskGetTickCount();

    while( true )
    {
        /*
         * A DOWNLOAD REQUEST IS TAKEN HERE, BETWEEN FRAMES, and carried out on
         * this task.
         *
         * Erasing and writing flash stalls the instruction cache, and this
         * loop reads its model straight out of mapped flash -- so a download
         * running on the HTTP task would be erasing the region the raster is
         * reading. Doing it here means the model is provably not being drawn
         * while its slot is written.
         *
         * model_store_apply does not return on success: it restarts, and the
         * new model comes up by the ordinary boot path. See model_store.h.
         */
        {
            char url[256];

            if( model_store_take_request(url, sizeof(url)) )
            {
                struct UiScreen busy = {
                    .title = "LOADING",
                    .line1 = "FETCHING MODEL",
                    .line3 = "THIS TAKES A MOMENT",
                };

                blit_wait();
                screen_present(&busy);

                if( model_store_apply(url) != ESP_OK )
                {
                    struct ModelProgress p;
                    struct UiScreen failed = {
                        .title = "FAILED",
                        .line1 = "COULD NOT LOAD",
                        .line3 = "KEEPING THE CURRENT MODEL",
                    };

                    model_store_progress(&p);
                    ESP_LOGE(TAG, "download failed: %s", p.error);
                    screen_present(&failed);
                    vTaskDelay(pdMS_TO_TICKS(3000));

                    /* The deadline is stale after seconds of downloading; a
                     * vTaskDelayUntil against it would spin without sleeping
                     * until the clock caught up. */
                    loop_deadline = xTaskGetTickCount();
                }
            }
        }

        /*
         * THE CAMERA COMES FROM THE LIVE POSE, once a frame.
         *
         * `spin` is the ornament's own sweep; with it off the yaw is whatever
         * the page last set. The sweep keeps running either way so that
         * turning it back on continues from where it would have been, rather
         * than snapping.
         */
        {
            struct DisplayPose live;

            display_get_pose(&live);

            pose.pitch = live.pitch;
            pose.zoom = live.zoom;
            pose.offset_y = live.offset_y;
            pose.yaw = live.spin ? head_yaw : live.yaw;
        }

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
        /*
         * Counting is decided BEFORE the frame, not after it.
         *
         * There is no whole frame in memory to count once it is drawn -- each
         * strip is handed to the panel and then overwritten by the strip after
         * it. So the once-a-second census rides along with the render as a
         * per-strip fold, and the decision to take it has to be made while
         * there is still something to fold over.
         */
        bool report_now = (esp_timer_get_time() - report_clock_us) >= 1000000;
        struct StripScan scan = { .bg = bg };

        uint32_t t0 = esp_cpu_get_cycle_count();
        bool visible = frame_render(view, model_hnd, &pose, bg, true,
                                    report_now ? strip_scan : NULL, &scan);
        uint32_t frame_cycles = esp_cpu_get_cycle_count() - t0;

        /*
         * What actually landed on the panel, once a second.
         *
         * A render loop that draws nothing looks exactly like a render loop
         * that draws correctly when the only way to check is to look at a
         * panel that may not be plugged in. Counting the pixels that differ
         * from the background separates "the raster ran" from "the raster
         * ran and produced geometry", and it is the difference between
         * debugging the kernel and debugging the wiring.
         *
         * `frame` is render, clear, census and whatever wait the strip
         * buffers imposed, which is the number that has to fit the period --
         * the separate blit figure this used to print stopped meaning anything
         * once the transfer overlapped the next strip's render.
         */
        report_frames++;
        if( report_now )
        {
            int64_t window_us = esp_timer_get_time() - report_clock_us;

            ESP_LOGI(TAG,
                     "frame %d: %.1f fps, %.1f poses/s  visible=%d  %ld px  "
                     "anim %.2f ms  frame %.2f ms",
                     anim_frame, report_frames * 1e6 / (double)window_us,
                     anim_advances * 1e6 / (double)window_us, (int)visible, scan.drawn,
                     (double)anim_cycles / (CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ * 1000.0),
                     (double)frame_cycles / (CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ * 1000.0));
            report_clock_us = esp_timer_get_time();
            report_frames = 0;
            anim_advances = 0;
        }

        if( ++pulse_tick >= pulse_period_ticks )
        {
            pulse_tick = 0;
            pulse_level ^= 1;
            gpio_set_level(PIN_PULSE, pulse_level);
        }

        head_yaw = head_yaw_advance(head_yaw, &head_yaw_phase);

        /* Held long enough, this forgets the network and restarts; it does not
         * return. See boot_button_poll. */
        boot_button_poll(&boot_held_ms, LOOP_PERIOD_MS);

        /*
         * A PERIOD, not a gap -- but never a zero.
         *
         * vTaskDelay sleeps for its argument ON TOP of the work already done,
         * so a 30 ms delay after 24 ms of render is a 54 ms frame.
         * vTaskDelayUntil sleeps until the deadline instead, so the period is
         * what it says whatever the frame cost.
         *
         * What it does NOT do is sleep at all when the deadline has already
         * passed, and a frame that overruns its period leaves it permanently
         * behind. The loop then spins without yielding, the idle task on this
         * core never runs, and the task watchdog trips every five seconds --
         * which is what a 24 ms frame against a 20 ms period did. So an
         * overrun resets the schedule and still gives up the core for a tick:
         * dropping a frame is fine, never yielding is not.
         */
        if( (TickType_t)(xTaskGetTickCount() - loop_deadline) < pdMS_TO_TICKS(LOOP_PERIOD_MS) )
        {
            vTaskDelayUntil(&loop_deadline, pdMS_TO_TICKS(LOOP_PERIOD_MS));
        }
        else
        {
            vTaskDelay(1);
            loop_deadline = xTaskGetTickCount();
        }
    }
}
