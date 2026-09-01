#ifndef MODEL_STORE_H
#define MODEL_STORE_H

/*
 * The two model slots in flash, and the download that fills one.
 *
 * ## The swap is a restart, and that is the simple half of a hard problem
 *
 * A live bundle is a set of pointers into mapped flash that the raster
 * dereferences every frame. Swapping one for another while the loop runs means
 * unmapping under those pointers, resizing the view arena for a model with a
 * different face count, re-registering textures, and replacing the bounds
 * cylinder the camera is framed from -- each of which is a way to leave the
 * device drawing garbage or crashing in a kernel.
 *
 * So it does not do that. The download writes the inactive slot, records the
 * swap in NVS, and restarts. Boot takes about a second, and everything comes
 * up from the newly active slot by the ordinary path with no special case. The
 * provisioning flow already ends in a restart for the same reason.
 *
 * ## The download runs on the render task
 *
 * Erasing and writing flash stalls the instruction cache, and the render path
 * reads its model out of mapped flash. A download from the HTTP task would
 * interleave with a raster reading the very region being erased. So the HTTP
 * handler only REQUESTS a download; the render loop picks the request up
 * between frames, stops drawing the model, and does the work itself.
 */

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "model_bundle.h"

enum ModelStoreState
{
    MODEL_STORE_IDLE = 0,
    MODEL_STORE_DOWNLOADING,
    MODEL_STORE_VERIFYING,
    MODEL_STORE_FAILED,
};

struct ModelProgress
{
    enum ModelStoreState state;
    /** Bytes written so far, and the total the server declared (0 if it did
     *  not, which is legal and means the bar cannot be drawn). */
    int received;
    int total;
    /** Why the last attempt failed, for the phone to show. Empty on success. */
    char error[64];
};

/**
 * Find the slots, and map whichever one holds the active bundle.
 *
 * `view_arena_bytes` is what the renderer has: a bundle claiming more than
 * this is refused at download time, before anything is erased.
 */
esp_err_t model_store_init(size_t view_arena_bytes);

/** What the renderer's view arena holds, as told to model_store_init. The
 *  phone shows it so a model too complex for this device can be greyed out
 *  rather than offered and then refused. */
size_t model_store_view_arena(void);

/** The name in the active bundle's header, or "" if the built-in model is
 *  showing. */
const char* model_store_active_name(void);

/** The active bundle, or NULL if no slot holds a valid one. */
const struct ModelBundle* model_store_active(void);

/** Ask for `url` to be downloaded into the inactive slot. Returns immediately;
 *  the render loop does the work. Safe to call from an HTTP handler. */
esp_err_t model_store_request(const char* url);

/** Whether a request is waiting, and if so what it is. Render task only. */
bool model_store_take_request(char* url, size_t url_len);

/**
 * Carry out a request: download, verify, activate, restart.
 *
 * Only returns on FAILURE -- success restarts the device. Render task only,
 * and only with the model no longer being drawn.
 */
esp_err_t model_store_apply(const char* url);

/** A snapshot of where the download has got to, for /api/progress. */
void model_store_progress(struct ModelProgress* out);

#endif /* MODEL_STORE_H */
