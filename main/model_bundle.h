#ifndef MODEL_BUNDLE_H
#define MODEL_BUNDLE_H

/*
 * A downloaded model, read in place out of mapped flash.
 *
 * The bundle's bulk -- vertices, faces, frames, texels -- is never copied. It
 * is mapped from a flash partition and ToriDraw's arrays are pointed straight
 * at it, which is exactly what the firmware's own baked model does with
 * .rodata. What this costs in RAM is only the structs that carry pointers and
 * the live state the animation writes:
 *
 *   frame descriptors   frame_count * sizeof(ToriDraw_AnimFrame)  ~2.8 KB
 *   bone group tables   (face + vertex + framemap groups) * 4     ~0.6 KB
 *   live vertices       3 * int16 * vertex_count                  ~3.2 KB
 *   live face alphas    face_count                                ~1.0 KB
 *
 * against ~100 KB of mapped bundle. See main/xmb_format.h for the file layout
 * and why the pointers cannot simply be in the file.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "toridraw.h"
#include "toridraw_animation.h"
#include "xmb_format.h"

struct ModelBundle
{
    const struct XmbHeader* header;
    const uint8_t* base;

    struct ToriDraw_Model model;
    struct ToriDraw_Animation animation;
    struct ToriDraw_AnimBase anim_base;
    struct ToriDraw_Bones face_bones;
    struct ToriDraw_Bones vertex_bones;

    /** Whether `animation` is worth playing: a static bundle has no frames. */
    bool has_animation;

    /* Everything below is allocated by model_bundle_open and freed by
     * model_bundle_close. */
    struct ToriDraw_AnimFrame* frames;
    boneint_t** face_bone_ptrs;
    boneint_t** vertex_bone_ptrs;
    uint8_t** anim_group_ptrs;
    struct ToriDraw_Texture* textures;
    /** Parallel to `textures`, and the reason it is kept: a face names its
     *  texture by CACHE ID, not by index, so registering them under 0..n-1
     *  draws the wrong texture on every textured face. Points into the
     *  mapping. */
    const struct XmbTexture* texture_descs;
    int texture_count;

    int16_t* live_vx;
    int16_t* live_vy;
    int16_t* live_vz;
    alphaint_t* live_face_alphas;
};

/**
 * Validate a mapped bundle and build the structs that point into it.
 *
 * `mapped` must stay mapped and unwritten for as long as `out` is used: every
 * array in the model points into it, so unmapping or erasing the partition
 * under a live bundle is a crash in the raster rather than an error here.
 *
 * Validation is not a formality. The offsets in the header decide where
 * ToriDraw will read, so a bundle that is truncated, from a different version,
 * or simply wrong points the raster at whatever follows in flash. Everything
 * is bounds-checked against `mapped_size` before any of it is believed.
 */
esp_err_t model_bundle_open(const void* mapped, size_t mapped_size, struct ModelBundle* out);

/**
 * What this bundle's view will cost ON THIS BUILD.
 *
 * NOT the header's `view_bytes`, and the difference is the reason this
 * function exists. The arena's size depends on build flags, not only on the
 * model: this firmware arms the Xtensa presorted-run kernels, which arms
 * TORIDRAW_RASTER_BATCH, which provisions a 32-byte-per-face batched stash
 * that a host tool built without those kernels never sizes. Measured, the same
 * 1000-face model came to 75,600 bytes in the baker and 106,492 here.
 *
 * So the header's figure is advisory -- it is what the catalogue shows a phone
 * -- and this is what the device checks its arena against. A number computed
 * on another machine is not a safe bound for a buffer on this one.
 */
size_t model_bundle_view_bytes(const struct XmbHeader* header);

/** Release what model_bundle_open allocated. Does not unmap. */
void model_bundle_close(struct ModelBundle* b);

#endif /* MODEL_BUNDLE_H */
