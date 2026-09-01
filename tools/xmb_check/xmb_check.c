/*
 * Read a .xmb bundle with the device's own loader, and draw it.
 *
 *   xmb_check <bundle.xmb>
 *
 * ## Why this exists
 *
 * A bundle the device cannot read is far cheaper to diagnose here than as a
 * blank panel over a serial line -- the same reasoning that put a decode step
 * in tools/extract_model/pack_anim.c. What is different is that this does not
 * write a second reader: it compiles main/model_bundle.c, the one the firmware
 * runs, against a shim for the four ESP-IDF things it uses. A round-trip test
 * whose reader and writer are both written from the same misunderstanding
 * passes while the device fails, which is the failure mode worth designing
 * out.
 *
 * It draws, too, and reports how many pixels landed. Validation proves the
 * offsets are inside the file; only a render proves they point at the right
 * arrays. A frame that comes out empty, or identical across every pose, is a
 * bundle whose vertices or framemap are wired wrong.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "toridraw.h"
#include "toridraw_mini.h"
#include "toridraw_model_transform.h"

#include "model_bundle.h"

/* MinGW's runtime has no aligned_alloc, so over-allocate and align by hand.
 * The alignment matters: the device maps a flash partition, and every section
 * offset is relative to a base that is aligned far past 4. A test whose base
 * happened to be odd would fail differently than the device does. */
static void*
aligned_block(size_t bytes, size_t align, void** raw)
{
    uintptr_t p;

    *raw = malloc(bytes + align);
    if( !*raw )
        return NULL;

    p = ((uintptr_t)*raw + (align - 1)) & ~(uintptr_t)(align - 1);
    return (void*)p;
}

#define WIDTH  240
#define HEIGHT 240

int
main(int argc, char** argv)
{
    const char* path;
    FILE* f;
    long size;
    uint8_t* buf;
    void* buf_raw;
    void* arena_raw;
    struct ModelBundle bundle;
    struct ToriDraw_ModelHandle hnd;
    struct ToriDraw_MiniLimits limits;
    size_t view_bytes;
    void* arena;
    struct ToriDraw_MiniView* view;
    toripixel_t* pixels;
    struct ToriDraw_MiniTarget target;
    struct ToriDraw_MiniPose pose = TORIDRAW_MINI_POSE_DEFAULT;
    esp_err_t err;
    int empty_poses = 0;
    int rc = 0;

    if( argc < 2 )
    {
        fprintf(stderr, "usage: %s <bundle.xmb>\n", argv[0]);
        return 2;
    }
    path = argv[1];

    f = fopen(path, "rb");
    if( !f )
    {
        fprintf(stderr, "cannot open %s\n", path);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);

    /*
     * Aligned like the flash mapping it stands in for. The device maps a
     * partition, so every section lands where the writer put it relative to a
     * 4-aligned base; a malloc that happened to be odd here would fail
     * differently than the device does, in either direction.
     */
    buf = aligned_block((size_t)size, 16, &buf_raw);
    if( !buf || fread(buf, 1, (size_t)size, f) != (size_t)size )
    {
        fprintf(stderr, "cannot read %s\n", path);
        return 1;
    }
    fclose(f);

    ToriDraw_Init();

    err = model_bundle_open(buf, (size_t)size, &bundle);
    if( err != ESP_OK )
    {
        fprintf(stderr, "model_bundle_open failed: 0x%x\n", err);
        return 1;
    }

    printf("bundle      %s\n", path);
    printf("  name      \"%.*s\" (model %d, seq \"%.*s\")\n", XMB_NAME_MAX, bundle.header->name,
           (int)bundle.header->model_id, XMB_NAME_MAX, bundle.header->seq);
    printf("  size      %u bytes\n", (unsigned)bundle.header->total_size);
    printf("  geometry  %d vertices, %d faces, %d textured\n", (int)bundle.header->vertex_count,
           (int)bundle.header->face_count, (int)bundle.header->textured_face_count);
    printf("  animation %d frames, %d framemap groups\n", (int)bundle.header->frame_count,
           (int)bundle.header->base_length);
    printf("  bones     %d face, %d vertex\n", (int)bundle.header->face_bone_count,
           (int)bundle.header->vertex_bone_count);
    printf("  textures  %d\n", (int)bundle.header->texture_count);
    printf("  view      %u bytes claimed\n", (unsigned)bundle.header->view_bytes);

    hnd = ToriDraw_ModelHandleOwned(&bundle.model);

    /*
     * The limits are recomputed rather than taken from the header, and then
     * compared. The device sizes its arena from a number in the file, so if
     * that number can disagree with what the model actually needs, it can
     * disagree in the direction that overruns.
     */
    ToriDraw_MiniLimitsForModel(hnd, &limits);
    /* batched_raster is left as this build's own answer, matching the baker:
     * the number in the header is for the firmware's kernel lane, and the
     * firmware currently builds without the Xtensa kernels. See the note in
     * bake_model.c -- the device recomputes this for itself regardless. */
    limits.scene.max_faces = bundle.header->limit_max_faces;
    limits.scene.max_vertices = bundle.header->limit_max_vertices;
    limits.scene.depth_levels = bundle.header->limit_depth_levels;
    limits.scene.textures = bundle.header->limit_textures != 0;
    view_bytes = ToriDraw_MiniViewBytes(&limits);

    if( view_bytes != bundle.header->view_bytes )
    {
        printf("  MISMATCH  header says %u, these limits cost %u\n",
               (unsigned)bundle.header->view_bytes, (unsigned)view_bytes);
        rc = 1;
    }

    arena = aligned_block(view_bytes, TORIDRAW_ARENA_ALIGN, &arena_raw);
    view = ToriDraw_MiniViewInit(arena, view_bytes, &limits);

    pixels = calloc(WIDTH * HEIGHT, sizeof(toripixel_t));
    target.pixels = pixels;
    target.width = WIDTH;
    target.height = HEIGHT;
    target.stride = WIDTH;

    for( int i = 0; i < bundle.texture_count; i++ )
    {
        int id = bundle.texture_descs[i].id;

        if( id < 0 || id >= TORIDRAW_TEXTURE_ID_CAPACITY )
        {
            printf("  texture id %d is past the %d-entry map: its faces will not draw\n", id,
                   TORIDRAW_TEXTURE_ID_CAPACITY);
            rc = 1;
            continue;
        }
        ToriDraw_MiniSetTexture(view, id, &bundle.textures[i]);
    }

    /*
     * Every pose, because a sequence that drives two models hides one of them
     * by collapsing its vertices onto a point -- which draws nothing, and looks
     * like flicker on the panel rather than like the bad choice of sequence it
     * is. The device reports this at boot; catching it here means never
     * flashing that bundle.
     */
    {
        int frames = bundle.has_animation ? bundle.animation.frame_count : 1;

        for( int fr = 0; fr < frames; fr++ )
        {
            long drawn = 0;

            memset(pixels, 0, (size_t)WIDTH * HEIGHT * sizeof(toripixel_t));

            if( bundle.has_animation )
            {
                ToriDraw_ModelAnimateReset(&bundle.model);
                ToriDraw_ModelAnimateFrame(&bundle.model, bundle.animation.base,
                                           &bundle.animation.frames[fr]);
            }

            ToriDraw_MiniDrawModel(view, hnd, &target, &pose);

            for( int i = 0; i < WIDTH * HEIGHT; i++ )
                if( pixels[i] != 0 )
                    drawn++;

            if( drawn == 0 )
                empty_poses++;

            if( fr == 0 )
                printf("  render    pose 0 drew %ld pixels\n", drawn);
        }

        if( empty_poses )
        {
            printf("  WARNING   %d of %d poses draw nothing\n", empty_poses, frames);
            rc = 1;
        }
        else
        {
            printf("  render    all %d poses draw\n", frames);
        }
    }

    model_bundle_close(&bundle);
    free(pixels);
    free(arena_raw);
    free(buf_raw);

    printf(rc ? "FAILED\n" : "OK\n");
    return rc;
}
