#ifndef UI_H
#define UI_H

/*
 * The screens that are not the model: a QR code and a few lines of text.
 *
 * These exist because this device has no other way to say anything. It has no
 * buttons beyond BOOT, no serial console in the room it will live in, and one
 * 240x240 panel -- so the panel is where provisioning happens. A phone camera
 * pointed at it can join the device's access point, or open its control page,
 * without the user typing an address they have no way to learn.
 *
 * Painting is stateless and per-strip, because the framebuffer is (see
 * s_strip in main.c). ui_screen_prepare encodes the QR once into a buffer that
 * outlives the strip loop; ui_screen_paint is then called for each strip and
 * draws whatever part of the screen falls inside it.
 */

#include <stdbool.h>

#include "toridraw.h"

struct UiScreen
{
    /** Heading, drawn small at the top. May be NULL. */
    const char* title;
    /** What the QR encodes. NULL draws the text only. */
    const char* qr_payload;
    /** Up to three lines under the QR: the SSID and password, or the address.
     *  Any may be NULL. */
    const char* line1;
    const char* line2;
    const char* line3;
};

/**
 * Encode `screen->qr_payload` ready for painting.
 *
 * Returns false if the payload will not fit the version cap, which is a
 * programming error rather than a runtime condition -- the payloads this
 * device builds are all under 40 characters. The screen still paints, without
 * the QR, so a device that cannot encode still shows its address as text.
 */
bool ui_screen_prepare(const struct UiScreen* screen);

/**
 * Paint the rows [y0, y0 + rows) of the prepared screen into `pixels`, which
 * is `rows` rows of TORIDRAW_MINI target width.
 */
void ui_screen_paint(const struct UiScreen* screen, toripixel_t* pixels, int y0, int rows,
                     int width);

#endif /* UI_H */
