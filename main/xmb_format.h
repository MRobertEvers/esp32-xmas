#ifndef XMB_FORMAT_H
#define XMB_FORMAT_H

/*
 * The model bundle: a baked model, its animation and its textures, as one flat
 * file the device stores in flash and reads in place.
 *
 * ## Why a bundle and not a decode
 *
 * Decoding an OSRS model on the device does not fit, and never did -- 406 KB
 * against 311 KB of internal DRAM, which is the whole reason the current model
 * is baked into the firmware as const C (see tools/bake_model/bake_model.c).
 * Choosing a model from a phone cannot mean decoding one on the device.
 *
 * So the bake stays, and moves to the server. This is the bake's OUTPUT in
 * binary: already lit, already resampled, already packed, with every array in
 * the layout ToriDraw wants. The device writes it to a flash partition and
 * maps it -- so the model lives in flash exactly as .rodata did, costs the
 * same nothing in RAM, and is read by the raster at the same cost as the baked
 * one. What stays in RAM is what always did: three live vertex arrays, the
 * face alphas, and the handful of small structs that carry pointers.
 *
 * ## Why the pointers are not in the file
 *
 * ToriDraw's model, animation base and frames are structs of POINTERS. A file
 * cannot hold those, and rewriting them in place would mean making the mapped
 * flash writable, which it is not. So the file holds flat arrays plus the
 * lengths, and the loader builds the small pointer-carrying structs in RAM
 * pointing INTO the mapping. For the spirit tree that is about 4 KB of structs
 * against 300 KB of mapped data.
 *
 * ## Versioning
 *
 * `magic` and `version` are checked before anything else is read. A partition
 * holding a bundle from an older layout says so and is refused, rather than
 * being walked as though its offsets meant what they mean now -- which is the
 * same discipline tools/extract_model/pack_anim.c settled on, for the same
 * reason: a blob the device cannot read must fail loudly on a serial line, not
 * quietly on a panel.
 *
 * Little-endian throughout. Both ends are.
 */

#include <stdint.h>

#define XMB_MAGIC    0x31424D58u /* "XMB1" */
#define XMB_VERSION  1

#define XMB_NAME_MAX 32

/*
 * The sections, in one enum used by the writer and the reader.
 *
 * A section may be empty -- a model with no textured faces has no texture
 * coordinates, a static model has no frames -- and an empty section is size
 * zero, not an absent one, so the table is always the same shape.
 */
enum XmbSection
{
    XMB_SEC_ORIG_VX = 0,      /* int16  * vertex_count */
    XMB_SEC_ORIG_VY,
    XMB_SEC_ORIG_VZ,
    XMB_SEC_FACE_A,           /* int16  * face_count */
    XMB_SEC_FACE_B,
    XMB_SEC_FACE_C,
    XMB_SEC_FACE_COLOR_A,     /* uint16 * face_count */
    XMB_SEC_FACE_COLOR_B,
    XMB_SEC_FACE_COLOR_C,
    XMB_SEC_FACE_TEXTURE,     /* int16  * face_count */
    XMB_SEC_FACE_ALPHA,       /* uint8  * face_count */
    XMB_SEC_FACE_INFO,        /* int32  * face_count */
    XMB_SEC_FACE_PRIORITY,    /* uint8  * (face_count + 1) / 2 */
    XMB_SEC_FACE_COLOR,       /* uint16 * face_count */
    XMB_SEC_TEX_P,            /* int16  * textured_face_count */
    XMB_SEC_TEX_M,
    XMB_SEC_TEX_N,

    /* Bone maps. `_SIZES` is one length per group; `_DATA` is every group's
     * members concatenated in group order, so a group's members start at the
     * sum of the sizes before it. The face map is NOT the vertex map: a
     * type-5 transform walks the face one, and a model that has face alphas
     * but no face bones animates perfectly except that nothing ever fades. */
    XMB_SEC_FACE_BONE_SIZES,  /* uint16 * face_bone_count */
    XMB_SEC_FACE_BONE_DATA,   /* uint16 */
    XMB_SEC_VERT_BONE_SIZES,  /* uint16 * vertex_bone_count */
    XMB_SEC_VERT_BONE_DATA,   /* uint16 */

    /* The animation's framemap. Same concatenated-group shape as the bones. */
    XMB_SEC_BASE_TYPES,       /* uint8  * base_length */
    XMB_SEC_BASE_GROUP_SIZES, /* uint16 * base_length */
    XMB_SEC_BASE_GROUP_DATA,  /* uint8 */

    XMB_SEC_FRAMES,           /* struct XmbFrame * frame_count */
    XMB_SEC_FRAME_DATA,       /* int16, indexed by the frame descriptors */

    XMB_SEC_TEXTURES,         /* struct XmbTexture * texture_count */
    XMB_SEC_TEXEL_DATA,       /* int32 ARGB, indexed by the descriptors */

    XMB_SECTION_COUNT
};

/*
 * One animation frame.
 *
 * `first` is the index into XMB_SEC_FRAME_DATA of this frame's four parallel
 * arrays -- groups, x, y and z -- each `length` int16 long and stored one
 * after another. Four offsets would be three redundant additions.
 *
 * `delay` is the hold time in 50 Hz ticks and comes from the SEQ CONFIG, not
 * from the frame archive, which does not carry it. A packer that drops it
 * leaves every frame at zero and the sequence plays at fifty poses a second.
 */
struct XmbFrame
{
    int32_t id;
    int32_t length;
    int32_t first;
    int32_t delay;
};

/**
 * One texture, resampled to a square at bake time.
 *
 * `first` indexes XMB_SEC_TEXEL_DATA; the run is width * height int32 ARGB.
 *
 * `id` is the model's own texture id, NOT an index. A face names its texture
 * by cache id, and registering them under 0..n-1 would draw the wrong texture
 * on every textured face.
 */
struct XmbTexture
{
    int32_t id;
    int32_t width;
    int32_t height;
    int32_t first;
    int32_t opaque;
    int32_t animation_direction;
    int32_t animation_speed;
};

struct XmbHeader
{
    uint32_t magic;
    uint32_t version;
    /** Bytes in the whole bundle, header included. */
    uint32_t total_size;
    uint32_t flags;

    /** The dat2 model id and a human label, for the catalogue and the logs. */
    int32_t model_id;
    char name[XMB_NAME_MAX];
    char seq[XMB_NAME_MAX];

    int32_t vertex_count;
    int32_t face_count;
    int32_t textured_face_count;
    int32_t face_bone_count;
    int32_t vertex_bone_count;
    int32_t base_length;
    int32_t frame_count;
    int32_t texture_count;

    /*
     * The bounds cylinder of the WIDEST pose, measured at bake time.
     *
     * The device used to find this by animating through every frame at boot
     * and taking the widest, which costs a pass over the whole sequence and
     * has to happen before the first frame can be drawn. It is a property of
     * the model and its animation, so it belongs in the bundle -- and it must
     * be one fixed cylinder for the whole loop, because recomputing it per
     * frame moves the camera and throws the model off the panel.
     */
    int32_t bounds_center_to_top_edge;
    int32_t bounds_center_to_bottom_edge;
    int32_t bounds_min_y;
    int32_t bounds_max_y;
    int32_t bounds_radius;
    int32_t bounds_min_z_depth_any_rotation;

    /*
     * WHAT THIS MODEL WILL COST THE VIEW, computed by the baker with the same
     * layout function the device uses.
     *
     * The device's view arena is a fixed size in .bss, and the arena's
     * dominant term is face count: 1000 faces needs about 73 KB, 2000 about
     * 124 KB, 4000 about 248 KB -- which no configuration of this part can
     * hold beside a framebuffer. Carrying the number means the device can
     * refuse a model it cannot draw BEFORE it erases the slot holding the one
     * it can, and the catalogue can grey out what will not fit rather than
     * offering a download that ends in a black panel.
     */
    uint32_t view_bytes;
    int32_t limit_max_faces;
    int32_t limit_max_vertices;
    int32_t limit_depth_levels;
    uint32_t limit_textures;

    uint32_t section_offset[XMB_SECTION_COUNT];
    uint32_t section_size[XMB_SECTION_COUNT];

    /** SHA-256 of everything after this header. Checked after a download, so
     *  a truncated or corrupted transfer is refused before it is mapped. */
    uint8_t sha256[32];
};

#endif /* XMB_FORMAT_H */
