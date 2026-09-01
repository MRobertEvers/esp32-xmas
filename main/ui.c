#include <string.h>

#include "esp_log.h"

#include "qrcodegen.h"
#include "ui.h"

static const char *TAG = "ui";

/*
 * A 5x7 font, five column bytes per glyph, bit 0 the top row.
 *
 * ASCII 0x20 to 0x5F: space, punctuation, digits and uppercase, which is every
 * character this device puts on a screen -- an SSID, a password out of an
 * uppercase alphabet, a dotted-quad address, and a two-word label. Lowercase is
 * folded to uppercase at draw time rather than carried, which costs 160 bytes
 * less and reads the same at this size.
 *
 * ToriDraw has a font, and it is not this one: that is an OSRS cache font,
 * loaded from an archive this firmware does not carry. 320 bytes of .rodata is
 * cheaper than baking a glyph set to write two lines of ASCII.
 */
#define FONT_FIRST 0x20
#define FONT_LAST  0x5F
#define FONT_W     5
#define FONT_H     7

static const uint8_t k_font[FONT_LAST - FONT_FIRST + 1][FONT_W] = {
    { 0x00, 0x00, 0x00, 0x00, 0x00 }, /* space */
    { 0x00, 0x00, 0x5F, 0x00, 0x00 }, /* ! */
    { 0x00, 0x07, 0x00, 0x07, 0x00 }, /* " */
    { 0x14, 0x7F, 0x14, 0x7F, 0x14 }, /* # */
    { 0x24, 0x2A, 0x7F, 0x2A, 0x12 }, /* $ */
    { 0x23, 0x13, 0x08, 0x64, 0x62 }, /* % */
    { 0x36, 0x49, 0x55, 0x22, 0x50 }, /* & */
    { 0x00, 0x05, 0x03, 0x00, 0x00 }, /* ' */
    { 0x00, 0x1C, 0x22, 0x41, 0x00 }, /* ( */
    { 0x00, 0x41, 0x22, 0x1C, 0x00 }, /* ) */
    { 0x14, 0x08, 0x3E, 0x08, 0x14 }, /* * */
    { 0x08, 0x08, 0x3E, 0x08, 0x08 }, /* + */
    { 0x00, 0x50, 0x30, 0x00, 0x00 }, /* , */
    { 0x08, 0x08, 0x08, 0x08, 0x08 }, /* - */
    { 0x00, 0x60, 0x60, 0x00, 0x00 }, /* . */
    { 0x20, 0x10, 0x08, 0x04, 0x02 }, /* / */
    { 0x3E, 0x51, 0x49, 0x45, 0x3E }, /* 0 */
    { 0x00, 0x42, 0x7F, 0x40, 0x00 }, /* 1 */
    { 0x42, 0x61, 0x51, 0x49, 0x46 }, /* 2 */
    { 0x21, 0x41, 0x45, 0x4B, 0x31 }, /* 3 */
    { 0x18, 0x14, 0x12, 0x7F, 0x10 }, /* 4 */
    { 0x27, 0x45, 0x45, 0x45, 0x39 }, /* 5 */
    { 0x3C, 0x4A, 0x49, 0x49, 0x30 }, /* 6 */
    { 0x01, 0x71, 0x09, 0x05, 0x03 }, /* 7 */
    { 0x36, 0x49, 0x49, 0x49, 0x36 }, /* 8 */
    { 0x06, 0x49, 0x49, 0x29, 0x1E }, /* 9 */
    { 0x00, 0x36, 0x36, 0x00, 0x00 }, /* : */
    { 0x00, 0x56, 0x36, 0x00, 0x00 }, /* ; */
    { 0x08, 0x14, 0x22, 0x41, 0x00 }, /* < */
    { 0x14, 0x14, 0x14, 0x14, 0x14 }, /* = */
    { 0x00, 0x41, 0x22, 0x14, 0x08 }, /* > */
    { 0x02, 0x01, 0x51, 0x09, 0x06 }, /* ? */
    { 0x32, 0x49, 0x79, 0x41, 0x3E }, /* @ */
    { 0x7E, 0x11, 0x11, 0x11, 0x7E }, /* A */
    { 0x7F, 0x49, 0x49, 0x49, 0x36 }, /* B */
    { 0x3E, 0x41, 0x41, 0x41, 0x22 }, /* C */
    { 0x7F, 0x41, 0x41, 0x22, 0x1C }, /* D */
    { 0x7F, 0x49, 0x49, 0x49, 0x41 }, /* E */
    { 0x7F, 0x09, 0x09, 0x09, 0x01 }, /* F */
    { 0x3E, 0x41, 0x49, 0x49, 0x7A }, /* G */
    { 0x7F, 0x08, 0x08, 0x08, 0x7F }, /* H */
    { 0x00, 0x41, 0x7F, 0x41, 0x00 }, /* I */
    { 0x20, 0x40, 0x41, 0x3F, 0x01 }, /* J */
    { 0x7F, 0x08, 0x14, 0x22, 0x41 }, /* K */
    { 0x7F, 0x40, 0x40, 0x40, 0x40 }, /* L */
    { 0x7F, 0x02, 0x0C, 0x02, 0x7F }, /* M */
    { 0x7F, 0x04, 0x08, 0x10, 0x7F }, /* N */
    { 0x3E, 0x41, 0x41, 0x41, 0x3E }, /* O */
    { 0x7F, 0x09, 0x09, 0x09, 0x06 }, /* P */
    { 0x3E, 0x41, 0x51, 0x21, 0x5E }, /* Q */
    { 0x7F, 0x09, 0x19, 0x29, 0x46 }, /* R */
    { 0x46, 0x49, 0x49, 0x49, 0x31 }, /* S */
    { 0x01, 0x01, 0x7F, 0x01, 0x01 }, /* T */
    { 0x3F, 0x40, 0x40, 0x40, 0x3F }, /* U */
    { 0x1F, 0x20, 0x40, 0x20, 0x1F }, /* V */
    { 0x3F, 0x40, 0x38, 0x40, 0x3F }, /* W */
    { 0x63, 0x14, 0x08, 0x14, 0x63 }, /* X */
    { 0x07, 0x08, 0x70, 0x08, 0x07 }, /* Y */
    { 0x61, 0x51, 0x49, 0x45, 0x43 }, /* Z */
    { 0x00, 0x7F, 0x41, 0x41, 0x00 }, /* [ */
    { 0x02, 0x04, 0x08, 0x10, 0x20 }, /* backslash */
    { 0x00, 0x41, 0x41, 0x7F, 0x00 }, /* ] */
    { 0x04, 0x02, 0x01, 0x02, 0x04 }, /* ^ */
    { 0x40, 0x40, 0x40, 0x40, 0x40 }, /* _ */
};

/*
 * Version 6 caps the QR at 41 modules, which is 164 pixels at 4 per module and
 * leaves room for a quiet zone on a 240-pixel panel. The two buffers are 172
 * bytes each at that version; the temporary is only live inside prepare.
 *
 * The payloads are all short -- a `WIFI:` join string is about 35 characters
 * and a device URL about 25 -- so the cap is a memory bound, not a limit
 * anything real is near.
 */
#define QR_MAX_VERSION 6
#define QR_BUF_LEN     qrcodegen_BUFFER_LEN_FOR_VERSION(QR_MAX_VERSION)

/** The panel is 240 wide; this is what the QR is allowed of it. */
#define QR_TARGET_PX 168

static uint8_t s_qr[QR_BUF_LEN];
static bool s_qr_valid;

#define COLOR_FG   0xFFFF /* white, in the panel's own byte order */
#define COLOR_BG   0x0000

/** A pixel, if it is inside this strip. */
static inline void
put(toripixel_t* pixels, int y0, int rows, int width, int x, int y, toripixel_t c)
{
    if( x < 0 || x >= width )
        return;
    if( y < y0 || y >= y0 + rows )
        return;

    pixels[(size_t)(y - y0) * (size_t)width + (size_t)x] = c;
}

static void
fill_rect(toripixel_t* pixels, int y0, int rows, int width, int x, int y, int w, int h,
          toripixel_t c)
{
    for( int yy = y; yy < y + h; yy++ )
    {
        if( yy < y0 || yy >= y0 + rows )
            continue;
        for( int xx = x; xx < x + w; xx++ )
            put(pixels, y0, rows, width, xx, yy, c);
    }
}

/** One glyph at `scale`. Returns the x advance. */
static int
draw_char(toripixel_t* pixels, int y0, int rows, int width, int x, int y, char ch, int scale,
          toripixel_t c)
{
    if( ch >= 'a' && ch <= 'z' )
        ch = (char)(ch - 'a' + 'A');

    if( ch < FONT_FIRST || ch > FONT_LAST )
        ch = '?';

    const uint8_t* glyph = k_font[ch - FONT_FIRST];

    for( int col = 0; col < FONT_W; col++ )
    {
        for( int row = 0; row < FONT_H; row++ )
        {
            if( !((glyph[col] >> row) & 1) )
                continue;

            fill_rect(pixels, y0, rows, width, x + col * scale, y + row * scale, scale, scale, c);
        }
    }

    return (FONT_W + 1) * scale;
}

static int
text_width(const char* text, int scale)
{
    return (int)strlen(text) * (FONT_W + 1) * scale;
}

/** A line of text, horizontally centred on the panel. */
static void
draw_text_centered(toripixel_t* pixels, int y0, int rows, int width, int y, const char* text,
                   int scale, toripixel_t c)
{
    int x;

    if( !text || !*text )
        return;

    x = (width - text_width(text, scale)) / 2;
    for( const char* p = text; *p; p++ )
        x += draw_char(pixels, y0, rows, width, x, y, *p, scale, c);
}

bool
ui_screen_prepare(const struct UiScreen* screen)
{
    static uint8_t temp[QR_BUF_LEN];

    s_qr_valid = false;

    if( !screen || !screen->qr_payload || !screen->qr_payload[0] )
        return true;

    /*
     * Low error correction, deliberately.
     *
     * The alternative buys redundancy against a damaged or dirty code, which
     * is a paper problem. This code is drawn on a lit panel a phone is held
     * ten centimetres from, and every level above LOW costs modules -- which
     * at a fixed 168 pixels means smaller ones, which is the thing that
     * actually stops a camera reading it.
     */
    if( !qrcodegen_encodeText(screen->qr_payload, temp, s_qr, qrcodegen_Ecc_LOW,
                              qrcodegen_VERSION_MIN, QR_MAX_VERSION, qrcodegen_Mask_AUTO, true) )
    {
        ESP_LOGE(TAG, "payload of %u chars will not fit a version %d QR",
                 (unsigned)strlen(screen->qr_payload), QR_MAX_VERSION);
        return false;
    }

    s_qr_valid = true;
    return true;
}

void
ui_screen_paint(const struct UiScreen* screen, toripixel_t* pixels, int y0, int rows, int width)
{
    int qr_px = 0;
    int qr_x = 0;
    int qr_y = 0;
    int quiet = 0;
    int text_y;

    for( int i = 0; i < rows * width; i++ )
        pixels[i] = COLOR_BG;

    if( !screen )
        return;

    draw_text_centered(pixels, y0, rows, width, 8, screen->title, 2, COLOR_FG);

    if( s_qr_valid )
    {
        int modules = qrcodegen_getSize(s_qr);
        int scale = QR_TARGET_PX / modules;

        if( scale < 1 )
            scale = 1;

        qr_px = modules * scale;
        /*
         * The quiet zone is part of the code, not decoration. A scanner looks
         * for a clear margin around the finder patterns, and against this
         * panel's black background there is none -- the code runs straight
         * into it and many cameras will not lock on. Four modules is what the
         * specification asks for; two is what fits beside everything else and
         * is enough in practice for a screen held close.
         */
        quiet = 2 * scale;
        qr_x = (width - qr_px) / 2;
        qr_y = 34;

        fill_rect(pixels, y0, rows, width, qr_x - quiet, qr_y - quiet, qr_px + 2 * quiet,
                  qr_px + 2 * quiet, COLOR_FG);

        for( int my = 0; my < modules; my++ )
        {
            /* Whole module rows outside this strip are skipped without asking
             * the encoder about any of their modules. */
            if( qr_y + (my + 1) * scale <= y0 || qr_y + my * scale >= y0 + rows )
                continue;

            for( int mx = 0; mx < modules; mx++ )
            {
                if( !qrcodegen_getModule(s_qr, mx, my) )
                    continue;

                fill_rect(pixels, y0, rows, width, qr_x + mx * scale, qr_y + my * scale, scale,
                          scale, COLOR_BG);
            }
        }
    }

    text_y = s_qr_valid ? qr_y + qr_px + quiet + 10 : 60;

    draw_text_centered(pixels, y0, rows, width, text_y, screen->line1, 2, COLOR_FG);
    draw_text_centered(pixels, y0, rows, width, text_y + 20, screen->line2, 2, COLOR_FG);
    draw_text_centered(pixels, y0, rows, width, text_y + 40, screen->line3, 1, COLOR_FG);
}
