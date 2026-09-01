#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

#include "toridraw_arena.h"
#include "toridraw_mini.h"
/* Defines TORIDRAW_RASTER_BATCH when this lane has a whole-model door, which
 * is what decides whether the arena carries the batched walk's stash. */
#include "toridraw_raster_batch.h"

#include "model_bundle.h"

static const char *TAG = "bundle";

/*
 * Sanity bounds on the counts, checked before anything is sized from them.
 *
 * ToriDraw stores a face index in a faceint_t and reads a vertex index out of
 * one, so both are bounded at 32767 by the library -- past that the sort
 * truncates silently rather than overrunning anything a check could see. The
 * rest are here because every one of these numbers becomes a malloc size and
 * a loop bound, and a corrupt header should fail as a refused bundle rather
 * than as an allocation the size of the header's garbage.
 */
#define MAX_VERTICES 32767
#define MAX_FACES    32767
#define MAX_FRAMES   1024
#define MAX_TEXTURES 64
#define MAX_GROUPS   4096

/**
 * A section, checked against the mapping and against the size it must be.
 *
 * `want` of zero means "whatever length it is", for the flattened group and
 * texel blobs whose size is the sum of a table this cannot see. Everything
 * else states its expected length, because a section that is the wrong size
 * for the count in the header is a bundle whose header and body disagree --
 * and believing either one of them is how a raster ends up walking flash.
 */
static const void*
section(const struct ModelBundle* b, size_t mapped_size, enum XmbSection sec, size_t want)
{
    uint32_t off = b->header->section_offset[sec];
    uint32_t len = b->header->section_size[sec];

    if( len == 0 )
        return NULL;

    if( off < sizeof(struct XmbHeader) || off > mapped_size || len > mapped_size - off )
    {
        ESP_LOGE(TAG, "section %d runs outside the bundle (offset %u, %u bytes)", (int)sec,
                 (unsigned)off, (unsigned)len);
        return NULL;
    }

    if( off & 3u )
    {
        /* Sections are 4-aligned by the writer precisely so ToriDraw's int32
         * arrays can be pointed at them; an unaligned load on Xtensa faults. */
        ESP_LOGE(TAG, "section %d is not 4-aligned", (int)sec);
        return NULL;
    }

    if( want && len != want )
    {
        ESP_LOGE(TAG, "section %d is %u bytes, expected %u", (int)sec, (unsigned)len,
                 (unsigned)want);
        return NULL;
    }

    return b->base + off;
}

/** Total elements in a flattened group blob, from its size table. */
static size_t
group_total(const uint16_t* sizes, int count)
{
    size_t total = 0;

    for( int i = 0; i < count; i++ )
        total += sizes[i];

    return total;
}

size_t
model_bundle_view_bytes(const struct XmbHeader* header)
{
    struct ToriDraw_SceneLimits limits;

    if( !header )
        return 0;

    memset(&limits, 0, sizeof(limits));
    limits.max_vertices = header->limit_max_vertices;
    limits.max_faces = header->limit_max_faces;
    limits.depth_levels = header->limit_depth_levels;
    limits.textures = header->limit_textures != 0;

    /* The same predicate ToriDraw_MiniLimitsForModel uses, read from the same
     * header, so this cannot drift from what MiniViewInit will actually
     * consume. */
#ifdef TORIDRAW_RASTER_BATCH
    limits.batched_raster = true;
#else
    limits.batched_raster = false;
#endif

    return ToriDraw_SceneArenaBytes(&limits);
}

esp_err_t
model_bundle_open(const void* mapped, size_t mapped_size, struct ModelBundle* out)
{
    const struct XmbHeader* h;
    struct ModelBundle* b = out;

    if( !mapped || !out )
        return ESP_ERR_INVALID_ARG;

    memset(b, 0, sizeof(*b));

    if( mapped_size < sizeof(struct XmbHeader) )
    {
        ESP_LOGE(TAG, "only %u bytes mapped; a header is %u", (unsigned)mapped_size,
                 (unsigned)sizeof(struct XmbHeader));
        return ESP_ERR_INVALID_SIZE;
    }

    h = (const struct XmbHeader*)mapped;
    b->header = h;
    b->base = (const uint8_t*)mapped;

    if( h->magic != XMB_MAGIC )
    {
        /* An empty slot reads as 0xFF everywhere, which lands here. That is
         * the ordinary "no model downloaded yet" case, not a corruption. */
        ESP_LOGI(TAG, "no bundle in this slot");
        return ESP_ERR_NOT_FOUND;
    }

    if( h->version != XMB_VERSION )
    {
        ESP_LOGE(TAG, "bundle is version %u, this firmware reads %u -- refusing rather "
                      "than guessing at its layout",
                 (unsigned)h->version, (unsigned)XMB_VERSION);
        return ESP_ERR_INVALID_VERSION;
    }

    if( h->total_size > mapped_size )
    {
        ESP_LOGE(TAG, "bundle says %u bytes, only %u are here", (unsigned)h->total_size,
                 (unsigned)mapped_size);
        return ESP_ERR_INVALID_SIZE;
    }

    if( h->vertex_count <= 0 || h->vertex_count > MAX_VERTICES || h->face_count <= 0 ||
        h->face_count > MAX_FACES || h->frame_count < 0 || h->frame_count > MAX_FRAMES ||
        h->texture_count < 0 || h->texture_count > MAX_TEXTURES ||
        h->face_bone_count < 0 || h->face_bone_count > MAX_GROUPS ||
        h->vertex_bone_count < 0 || h->vertex_bone_count > MAX_GROUPS ||
        h->base_length < 0 || h->base_length > MAX_GROUPS )
    {
        ESP_LOGE(TAG, "bundle counts are not credible (%d vertices, %d faces, %d frames)",
                 (int)h->vertex_count, (int)h->face_count, (int)h->frame_count);
        return ESP_ERR_INVALID_ARG;
    }

    const size_t vn = (size_t)h->vertex_count;
    const size_t fn = (size_t)h->face_count;
    const size_t tn = (size_t)h->textured_face_count;

    /* ---- the model ---------------------------------------------------- */

    const int16_t* ovx = section(b, mapped_size, XMB_SEC_ORIG_VX, vn * 2);
    const int16_t* ovy = section(b, mapped_size, XMB_SEC_ORIG_VY, vn * 2);
    const int16_t* ovz = section(b, mapped_size, XMB_SEC_ORIG_VZ, vn * 2);
    const uint8_t* fal = section(b, mapped_size, XMB_SEC_FACE_ALPHA, fn);

    if( !ovx || !ovy || !ovz )
    {
        ESP_LOGE(TAG, "bundle has no vertices");
        return ESP_ERR_INVALID_ARG;
    }

    struct ToriDraw_Model* m = &b->model;

    memset(m, 0, sizeof(*m));
    m->vertex_count = h->vertex_count;
    m->face_count = h->face_count;
    m->textured_face_count = h->textured_face_count;

    m->original_vertices_x = (int16_t*)ovx;
    m->original_vertices_y = (int16_t*)ovy;
    m->original_vertices_z = (int16_t*)ovz;

    m->face_indices_a = (int16_t*)section(b, mapped_size, XMB_SEC_FACE_A, fn * 2);
    m->face_indices_b = (int16_t*)section(b, mapped_size, XMB_SEC_FACE_B, fn * 2);
    m->face_indices_c = (int16_t*)section(b, mapped_size, XMB_SEC_FACE_C, fn * 2);
    m->face_colors_a = (uint16_t*)section(b, mapped_size, XMB_SEC_FACE_COLOR_A, fn * 2);
    m->face_colors_b = (uint16_t*)section(b, mapped_size, XMB_SEC_FACE_COLOR_B, fn * 2);
    m->face_colors_c = (uint16_t*)section(b, mapped_size, XMB_SEC_FACE_COLOR_C, fn * 2);
    m->face_textures = (int16_t*)section(b, mapped_size, XMB_SEC_FACE_TEXTURE, fn * 2);
    m->face_infos = (int*)section(b, mapped_size, XMB_SEC_FACE_INFO, fn * 4);
    m->face_priorities = (uint8_t*)section(b, mapped_size, XMB_SEC_FACE_PRIORITY, (fn + 1) / 2);
    m->face_colors = (uint16_t*)section(b, mapped_size, XMB_SEC_FACE_COLOR, fn * 2);
    m->textured_p_coordinate = (int16_t*)section(b, mapped_size, XMB_SEC_TEX_P, tn * 2);
    m->textured_m_coordinate = (int16_t*)section(b, mapped_size, XMB_SEC_TEX_M, tn * 2);
    m->textured_n_coordinate = (int16_t*)section(b, mapped_size, XMB_SEC_TEX_N, tn * 2);

    if( !m->face_indices_a || !m->face_indices_b || !m->face_indices_c )
    {
        ESP_LOGE(TAG, "bundle has no faces");
        return ESP_ERR_INVALID_ARG;
    }

    m->has_bounds_cylinder = true;
    m->bounds_cylinder.center_to_top_edge = h->bounds_center_to_top_edge;
    m->bounds_cylinder.center_to_bottom_edge = h->bounds_center_to_bottom_edge;
    m->bounds_cylinder.min_y = h->bounds_min_y;
    m->bounds_cylinder.max_y = h->bounds_max_y;
    m->bounds_cylinder.radius = h->bounds_radius;
    m->bounds_cylinder.min_z_depth_any_rotation = h->bounds_min_z_depth_any_rotation;

    /* ---- the live state ----------------------------------------------- */

    /*
     * The only things this model owns in RAM.
     *
     * ToriDraw_ModelAnimateFrame writes vertices_x/y/z, and a type-5 framemap
     * transform writes face_alphas -- so both are seeded from the bind values
     * in flash and then written every frame. Pointing face_alphas at the
     * mapping instead puts a per-frame write into read-only memory, and the
     * model then animates correctly in every respect except that nothing ever
     * fades.
     */
    b->live_vx = malloc(vn * sizeof(int16_t));
    b->live_vy = malloc(vn * sizeof(int16_t));
    b->live_vz = malloc(vn * sizeof(int16_t));
    b->live_face_alphas = fal ? malloc(fn) : NULL;

    if( !b->live_vx || !b->live_vy || !b->live_vz || (fal && !b->live_face_alphas) )
    {
        ESP_LOGE(TAG, "no room for %u live vertices", (unsigned)vn);
        model_bundle_close(b);
        return ESP_ERR_NO_MEM;
    }

    memcpy(b->live_vx, ovx, vn * sizeof(int16_t));
    memcpy(b->live_vy, ovy, vn * sizeof(int16_t));
    memcpy(b->live_vz, ovz, vn * sizeof(int16_t));
    m->vertices_x = b->live_vx;
    m->vertices_y = b->live_vy;
    m->vertices_z = b->live_vz;

    if( fal )
    {
        memcpy(b->live_face_alphas, fal, fn);
        m->original_face_alphas = (alphaint_t*)fal;
        m->face_alphas = b->live_face_alphas;
    }

    /* ---- the bone maps ------------------------------------------------ */

    /*
     * Two of them, and they are not interchangeable. A type-5 transform walks
     * the FACE map to fade faces; every other transform walks the vertex one.
     * A model with face alphas but no face bones animates perfectly and never
     * fades, silently, which is exactly the bug the baked path had.
     */
    struct
    {
        int count;
        enum XmbSection sizes_sec;
        enum XmbSection data_sec;
        struct ToriDraw_Bones* bones;
        boneint_t*** ptrs;
    } maps[2] = {
        { h->face_bone_count, XMB_SEC_FACE_BONE_SIZES, XMB_SEC_FACE_BONE_DATA, &b->face_bones,
          &b->face_bone_ptrs },
        { h->vertex_bone_count, XMB_SEC_VERT_BONE_SIZES, XMB_SEC_VERT_BONE_DATA,
          &b->vertex_bones, &b->vertex_bone_ptrs },
    };

    for( int mi = 0; mi < 2; mi++ )
    {
        int count = maps[mi].count;
        const uint16_t* sizes;
        const uint16_t* data;
        boneint_t** ptrs;
        size_t o = 0;

        if( count <= 0 )
            continue;

        sizes = section(b, mapped_size, maps[mi].sizes_sec, (size_t)count * 2);
        data = section(b, mapped_size, maps[mi].data_sec, 0);
        if( !sizes )
            continue;

        if( b->header->section_size[maps[mi].data_sec] != group_total(sizes, count) * 2 )
        {
            ESP_LOGE(TAG, "bone group data does not match its size table");
            model_bundle_close(b);
            return ESP_ERR_INVALID_ARG;
        }

        ptrs = calloc((size_t)count, sizeof(*ptrs));
        if( !ptrs )
        {
            model_bundle_close(b);
            return ESP_ERR_NO_MEM;
        }

        for( int i = 0; i < count; i++ )
        {
            ptrs[i] = sizes[i] ? (boneint_t*)(data + o) : NULL;
            o += sizes[i];
        }

        *maps[mi].ptrs = ptrs;
        maps[mi].bones->bones_count = count;
        maps[mi].bones->bones = ptrs;
        maps[mi].bones->bones_sizes = (boneint_t*)sizes;
    }

    if( h->face_bone_count > 0 )
        m->face_bones = &b->face_bones;
    if( h->vertex_bone_count > 0 )
        m->vertex_bones = &b->vertex_bones;

    /* ---- the animation ------------------------------------------------ */

    if( h->frame_count > 0 && h->base_length > 0 )
    {
        const uint8_t* types = section(b, mapped_size, XMB_SEC_BASE_TYPES,
                                       (size_t)h->base_length);
        const uint16_t* glen = section(b, mapped_size, XMB_SEC_BASE_GROUP_SIZES,
                                       (size_t)h->base_length * 2);
        const uint8_t* gdata = section(b, mapped_size, XMB_SEC_BASE_GROUP_DATA, 0);
        const struct XmbFrame* descs = section(b, mapped_size, XMB_SEC_FRAMES,
                                               (size_t)h->frame_count * sizeof(struct XmbFrame));
        const int16_t* fdata = section(b, mapped_size, XMB_SEC_FRAME_DATA, 0);
        size_t fdata_len = b->header->section_size[XMB_SEC_FRAME_DATA] / 2;
        size_t o = 0;

        if( !types || !glen || !descs || !fdata )
        {
            ESP_LOGW(TAG, "bundle has frames but an incomplete framemap; drawing it static");
        }
        else if( b->header->section_size[XMB_SEC_BASE_GROUP_DATA] !=
                 group_total(glen, h->base_length) )
        {
            ESP_LOGE(TAG, "framemap group data does not match its size table");
            model_bundle_close(b);
            return ESP_ERR_INVALID_ARG;
        }
        else
        {
            b->anim_group_ptrs = calloc((size_t)h->base_length, sizeof(uint8_t*));
            b->frames = calloc((size_t)h->frame_count, sizeof(*b->frames));

            if( !b->anim_group_ptrs || !b->frames )
            {
                model_bundle_close(b);
                return ESP_ERR_NO_MEM;
            }

            for( int i = 0; i < h->base_length; i++ )
            {
                b->anim_group_ptrs[i] = glen[i] ? (uint8_t*)(gdata + o) : NULL;
                o += glen[i];
            }

            b->anim_base.length = h->base_length;
            b->anim_base.types = (uint8_t*)types;
            b->anim_base.bone_groups = b->anim_group_ptrs;
            b->anim_base.bone_group_lengths = (uint16_t*)glen;

            for( int i = 0; i < h->frame_count; i++ )
            {
                size_t n = (size_t)descs[i].length;
                size_t first = (size_t)descs[i].first;

                /*
                 * Each frame's four arrays live end to end at `first`. The
                 * bound is checked per frame rather than trusted, because
                 * these offsets come out of the file and every one of them
                 * becomes a pointer the animation dereferences 50 times a
                 * second.
                 */
                if( descs[i].length < 0 || descs[i].first < 0 || n * 4 > fdata_len ||
                    first > fdata_len - n * 4 )
                {
                    ESP_LOGE(TAG, "frame %d points outside the frame data", i);
                    model_bundle_close(b);
                    return ESP_ERR_INVALID_ARG;
                }

                b->frames[i].id = descs[i].id;
                b->frames[i].length = descs[i].length;
                b->frames[i].delay = descs[i].delay;
                b->frames[i].groups = (int16_t*)(fdata + first);
                b->frames[i].x = (int16_t*)(fdata + first + n);
                b->frames[i].y = (int16_t*)(fdata + first + n * 2);
                b->frames[i].z = (int16_t*)(fdata + first + n * 3);
            }

            memset(&b->animation, 0, sizeof(b->animation));
            b->animation.base = &b->anim_base;
            b->animation.frames = b->frames;
            b->animation.frame_count = h->frame_count;
            b->has_animation = true;
        }
    }

    /* ---- the textures -------------------------------------------------- */

    if( h->texture_count > 0 )
    {
        const struct XmbTexture* descs =
            section(b, mapped_size, XMB_SEC_TEXTURES,
                    (size_t)h->texture_count * sizeof(struct XmbTexture));
        const int32_t* texels = section(b, mapped_size, XMB_SEC_TEXEL_DATA, 0);
        size_t texel_len = b->header->section_size[XMB_SEC_TEXEL_DATA] / 4;

        if( descs && texels )
        {
            b->textures = calloc((size_t)h->texture_count, sizeof(*b->textures));
            if( !b->textures )
            {
                model_bundle_close(b);
                return ESP_ERR_NO_MEM;
            }

            for( int i = 0; i < h->texture_count; i++ )
            {
                size_t n = (size_t)descs[i].width * (size_t)descs[i].height;
                size_t first = (size_t)descs[i].first;

                if( descs[i].width <= 0 || descs[i].height <= 0 || descs[i].first < 0 ||
                    n > texel_len || first > texel_len - n )
                {
                    ESP_LOGE(TAG, "texture %d points outside the texel data", i);
                    model_bundle_close(b);
                    return ESP_ERR_INVALID_ARG;
                }

                b->textures[i].texels = (int*)(texels + first);
                b->textures[i].width = descs[i].width;
                b->textures[i].height = descs[i].height;
                b->textures[i].opaque = descs[i].opaque != 0;
                /* The texels are in flash and belong to the mapping: the view
                 * must never free them. */
                b->textures[i].borrowed_texels = true;
                b->textures[i].animation_direction = descs[i].animation_direction;
                b->textures[i].animation_speed = descs[i].animation_speed;
            }

            b->texture_descs = descs;
            b->texture_count = h->texture_count;
        }
    }

    ESP_LOGI(TAG, "bundle \"%.*s\": model %d, %d vertices, %d faces, %d frames, %d textures",
             XMB_NAME_MAX, h->name, (int)h->model_id, (int)h->vertex_count, (int)h->face_count,
             (int)h->frame_count, (int)h->texture_count);

    return ESP_OK;
}

void
model_bundle_close(struct ModelBundle* b)
{
    if( !b )
        return;

    free(b->frames);
    free(b->face_bone_ptrs);
    free(b->vertex_bone_ptrs);
    free(b->anim_group_ptrs);
    free(b->textures);
    free(b->live_vx);
    free(b->live_vy);
    free(b->live_vz);
    free(b->live_face_alphas);

    memset(b, 0, sizeof(*b));
}
