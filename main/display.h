#ifndef DISPLAY_H
#define DISPLAY_H

/*
 * The panel controls the web page is allowed to reach.
 *
 * Deliberately narrow. main.c owns the framebuffer, the view and the render
 * loop, and none of that is safe to touch from an HTTP handler running on
 * another task. What is exposed here is state the render loop merely READS
 * once a frame -- a backlight duty and a camera pose -- so a handler can
 * change it without any locking.
 */

#include <stdbool.h>

/** Set the backlight, 0-100. Applied immediately; persistence is settings.c's. */
void display_set_brightness(int pct);

/**
 * Where the camera is, in the units ToriDraw uses.
 *
 * `yaw`, `pitch` and `roll` are 0..2047 for a full turn, not degrees: a
 * quarter turn is 512. `zoom` is a distance along the camera axis, so LARGER
 * is further away -- it is not a scale factor, and doubling it does not halve
 * the model. `offset_y` shifts the model in destination pixels.
 *
 * `spin` replaces `yaw` with the automatic sweep the ornament runs when nobody
 * is driving it. The manual yaw is remembered while it spins, so turning the
 * sweep off puts the model back where it was left.
 */
struct DisplayPose
{
    int yaw;
    int pitch;
    int zoom;
    int offset_y;
    bool spin;
};

/**
 * Read and write the live pose.
 *
 * NOT LOCKED, and it does not need to be. The render loop reads this once a
 * frame and an HTTP handler writes it; every field is a naturally aligned
 * 32-bit word, so a reader sees old or new values but never half of one. The
 * worst case is a single frame drawn with a new yaw and an old zoom, which is
 * a camera moving -- and a mutex shared with the raster would be a worse
 * trade than that.
 */
void display_get_pose(struct DisplayPose* out);
void display_set_pose(const struct DisplayPose* pose);

/** The zoom the boot framing chose for this model: what "reset" returns to,
 *  and the scale the page's zoom slider is expressed relative to. */
int display_default_zoom(void);

#endif /* DISPLAY_H */
