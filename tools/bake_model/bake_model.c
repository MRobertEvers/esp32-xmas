/*
 * Emit a model and its animation as const C, for the ESP32's flash.
 *
 *   bake_model <cache_dir> --rev <name> --model ID --frames id,id,... <out.c>
 *
 * ## Why this exists
 *
 * Decoding at boot does not fit. Measured on the part, for the animated spirit
 * tree (870 vertices, 1438 faces, 99 frames):
 *
 *   model on the heap        ~50 KB
 *   99 decoded frames        ~82 KB
 *   the mini view (scratch)  159 KB
 *   the framebuffer          115 KB
 *   -------------------------------
 *                            406 KB   against 311 KB of internal DRAM
 *
 * and worse than the total, decoding fragments the one large region: 229 KB
 * free in a largest block of 55 KB, which holds neither the view nor the
 * framebuffer. Baking removes both problems at once -- the 132 KB moves to
 * .rodata, and the region it used to be carved out of stays whole.
 *
 * This is the same move `TORIDRAW_TABLES_PRECOMPUTED` makes for the palette,
 * for the same reason, and it is why this board needs no PSRAM.
 *
 * ## What stays in RAM, and why only that
 *
 * ToriDraw_ModelAnimateFrame writes vertices_x/y/z and nothing else: it
 * restores the bind pose from original_vertices_* and applies the keyframe to
 * the live copy. So the bind pose, every face array, the bone map and all 99
 * frames are read-only for the life of the program and go to flash; the live
 * vertices are three int16 arrays -- about 5 KB for this model -- and are all
 * the caller has to provide.
 *
 * ## What it does NOT bake
 *
 * The lighting. Face colours come out of ToriDraw_RSCacheModelLight, which
 * runs here, so the emitted colours are already lit -- the device does not
 * light, and must not, because lighting a model whose colours are already lit
 * darkens it twice.
 */

#include "asset_access.h"
#include "tool_profile.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dat2_sprites.h"
#include "dat2_texture.h"

#include <toridraw_rscache.h>
#include "toridraw.h"
#include "toridraw_animation.h"
#include "toridraw_model_transform.h"

static FILE* g_out;

/* Distinct textures one model may name. The models this bakes carry one or
 * two; the cap exists so the scan is a fixed array rather than a growable one,
 * and it reports rather than truncates silently. */
#define TEX_MAX 32
static int g_tex_ids[TEX_MAX];

/*
 * One array, or nothing at all.
 *
 * A NULL field in the source model must stay NULL in the baked one -- an
 * absent face_textures means "no textured faces", and emitting a zero-filled
 * array in its place would make every face claim texture 0. So the emitters
 * return whether they wrote anything, and the struct initialiser below uses
 * that to choose between the symbol and NULL.
 */
/*
 * Functions rather than a macro, because the per-frame arrays are named at
 * RUNTIME ("k_f37_x") and a macro that pastes the name as a string literal
 * cannot express that.
 */
#define EMIT_BODY(ctype, fmt)                                                                      \
    do                                                                                             \
    {                                                                                              \
        if( !ptr || count <= 0 )                                                                   \
            return 0;                                                                              \
        fprintf(g_out, "static const " ctype " %s[] = {", name);                                   \
        for( int i = 0; i < count; i++ )                                                           \
            fprintf(g_out, "%s" fmt, (i % 16) ? "," : (i ? ",\n" : "\n"), ptr[i]);                 \
        fprintf(g_out, "};\n");                                                                    \
        return 1;                                                                                  \
    } while( 0 )

static int emit_i16(const char* name, const int16_t* ptr, int count) { EMIT_BODY("int16_t", "%d"); }
static int emit_u16(const char* name, const uint16_t* ptr, int count) { EMIT_BODY("uint16_t", "%u"); }
static int emit_u8(const char* name, const uint8_t* ptr, int count) { EMIT_BODY("uint8_t", "%u"); }
static int emit_i32(const char* name, const int* ptr, int count) { EMIT_BODY("int", "%d"); }

/*
 * A NULL field in the source model must stay NULL in the baked one. An absent
 * face_textures means "no textured faces", and a zero-filled array in its
 * place would make every face claim texture 0 -- so the field is emitted as
 * the symbol only when there was something to emit.
 */
static const char*
ref(const char* name, int emitted)
{
    static char buf[64];
    if( !emitted )
        return "NULL";
    snprintf(buf, sizeof(buf), "(void*)%s", name);
    return buf;
}


/* ------------------------------------------------------------- contrast --- */

/**
 * Re-curve the baked lightness.
 *
 * ## Why the lighting knobs are not enough
 *
 * ambient/attenuation set the RANGE, and on this model they already reach
 * 2..122 of the 2..126 the raster clamp allows. There is no range left to win.
 * What is left is the DISTRIBUTION: the mean sits near 37, so the faces are
 * bunched into the bottom third of that range and most of the span carries
 * almost no faces. Widening the range further only moves the few faces at the
 * ends and clips them.
 *
 * A gamma spreads the crowded region instead. `out = 126 * (in/126)^g` with
 * g < 1 stretches the dark end apart and compresses the bright end -- which is
 * where the faces actually are, so it buys separation where there is something
 * to separate. g = 1 is off.
 *
 * This runs after lighting and before the emit, so the device still receives
 * plain colours and pays nothing for it.
 */
static void
apply_lightness_gamma(struct ToriDraw_Model* m, double gamma)
{
    hsl16_t* sets[3];

    if( gamma == 1.0 )
        return;

    sets[0] = m->face_colors_a;
    sets[1] = m->face_colors_b;
    sets[2] = m->face_colors_c;

    for( int s = 0; s < 3; s++ )
    {
        if( !sets[s] )
            continue;
        for( int i = 0; i < m->face_count; i++ )
        {
            int v = sets[s][i];
            int l;

            /* -2 is the skip-face sentinel for textured faces, not a colour;
             * re-curving it would turn a skipped face into a drawn one. */
            if( (int16_t)v < 0 )
                continue;

            l = (int)(126.0 * pow((double)(v & 0x7F) / 126.0, gamma) + 0.5);
            if( l < 2 )
                l = 2;
            if( l > 126 )
                l = 126;

            /* Hue and saturation are the top 9 bits and do not move. */
            sets[s][i] = (hsl16_t)((v & 0xFF80) | l);
        }
    }
}


/**
 * What the lighting actually did, as numbers rather than a guess.
 *
 * The knobs are two integers with a divide between them and a clamp on the
 * end, so their effect on a particular model is not something to reason about
 * -- it is something to measure. This prints the spread of per-corner
 * lightness the raster will receive, and how much of it landed on the clamps.
 *
 * A high `clipped` share means the contrast asked for is not being delivered:
 * the extra swing is being thrown away at 2 and 126, and the model is losing
 * shading detail at the ends rather than gaining separation.
 */
static void
report_contrast(const struct ToriDraw_Model* m, const struct ToriDraw_RSCacheLight* light)
{
    long sum = 0;
    long n = 0;
    int lo = 127;
    int hi = 0;
    long clipped = 0;

    const hsl16_t* sets[3];
    sets[0] = m->face_colors_a;
    sets[1] = m->face_colors_b;
    sets[2] = m->face_colors_c;

    for( int s = 0; s < 3; s++ )
    {
        if( !sets[s] )
            continue;
        for( int i = 0; i < m->face_count; i++ )
        {
            /* The bottom 7 bits are the lightness the raster interpolates;
             * the top 9 are hue and saturation and do not move under
             * lighting. */
            int v = sets[s][i] & 0x7F;

            /* -2 is the skip-face sentinel for textured faces, not a colour. */
            if( (int16_t)sets[s][i] < 0 )
                continue;
            if( v < lo )
                lo = v;
            if( v > hi )
                hi = v;
            if( v <= 2 || v >= 126 )
                clipped++;
            sum += v;
            n++;
        }
    }

    if( n == 0 )
        return;

    /* Percentiles, not just the extremes. The range was already near its
     * ceiling while the model still looked flat, because the extremes are two
     * faces and the quartiles are the thousand in between -- which is what the
     * eye reads as contrast. */
    {
        long hist[128];
        long seen = 0;
        int p10 = 0;
        int p50 = 0;
        int p90 = 0;

        memset(hist, 0, sizeof(hist));
        for( int s = 0; s < 3; s++ )
        {
            if( !sets[s] )
                continue;
            for( int i = 0; i < m->face_count; i++ )
                if( (int16_t)sets[s][i] >= 0 )
                    hist[sets[s][i] & 0x7F]++;
        }
        for( int v = 0; v < 128; v++ )
        {
            long before = seen;
            seen += hist[v];
            if( before < n / 10 && seen >= n / 10 )
                p10 = v;
            if( before < n / 2 && seen >= n / 2 )
                p50 = v;
            if( before < (n * 9) / 10 && seen >= (n * 9) / 10 )
                p90 = v;
        }
        fprintf(stderr, "  p10 %d, p50 %d, p90 %d (inner spread %d)\n", p10, p50, p90,
                p90 - p10);
    }

    fprintf(stderr,
            "lighting: ambient %d, attenuation %d, dir %d,%d,%d\n"
            "  lightness %d..%d (spread %d), mean %ld, %ld%% on the clamps\n",
            light->ambient, light->attenuation, light->x, light->y, light->z, lo, hi,
            hi - lo, sum / n, (clipped * 100) / n);
}

/* ------------------------------------------------------------- textures --- */

/**
 * Bake every texture the model's faces name.
 *
 * ## Why this is here and not on the device
 *
 * A textured face carries a texture ID, not texels. Registering that ID is the
 * caller's job -- ToriDraw's texture map starts empty and its raster SKIPS a
 * face whose texture is absent, silently, which is exactly what a model with
 * invisible textured faces looks like. Nothing was registering them.
 *
 * Doing it on the device would mean linking the sprite decoder and the texture
 * config codec, decoding a palette-indexed sprite, and resampling it to 128x128
 * -- for a result that never changes. It is the same argument as the geometry:
 * constant input, constant output, so it belongs in .rodata.
 *
 * ## The cost
 *
 * ToriDraw texels are `int` ARGB8888, so a 128x128 texture is 64 KB of flash.
 * That is affordable here (8 MB part, and .rodata is not DRAM) and it is why
 * the size is not negotiable upward: 64 is the only other size the resampler
 * implements, and it quarters the flash at the cost of visible blockiness on a
 * face that fills a fifth of this panel.
 */
static int
emit_textures(struct Tool_Dat2Cache* cache, const struct RSCache* profile,
              const struct ToriDraw_Model* m, int dest_size)
{
    struct RSCache_Dat2DiskArchive* tex_arch = NULL;
    struct RSCache_FileList* tex_files = NULL;
    struct RSCache local = *profile;
    int wanted[TEX_MAX];
    int wanted_n = 0;
    int emitted = 0;
    int table;

    if( !m->face_textures )
        return 0;

    /* The distinct ids, not one entry per textured face: a model reuses a
     * texture across faces and baking it twice would double the flash. */
    for( int i = 0; i < m->face_count; i++ )
    {
        int id = m->face_textures[i];
        int seen = 0;

        if( id < 0 )
            continue;
        for( int j = 0; j < wanted_n; j++ )
            if( wanted[j] == id )
                seen = 1;
        if( !seen && wanted_n < TEX_MAX )
            wanted[wanted_n++] = id;
    }
    if( wanted_n == 0 )
        return 0;

    table = RSCache_Dat2DiskTableId(cache->disk, RSCACHE_DAT2_TABLE_TEXTURES);
    if( table == RSCACHE_DAT2_DISK_TABLE_ABSENT )
    {
        fprintf(stderr, "textures: no texture table in this cache\n");
        return 0;
    }
    /* Every texture def is a FILE in archive 0 of the texture table, keyed by
     * texture id. */
    tex_arch = RSCache_Dat2DiskArchiveNewLoad(cache->disk, table, 0);
    if( !tex_arch || !RSCache_Dat2DiskArchiveInitMetadata(cache->disk, tex_arch) ||
        tex_arch->file_count <= 0 )
    {
        fprintf(stderr, "textures: archive 0 has no file table\n");
        return 0;
    }
    /* The group's own revision picks the record codec -- the modern
     * single-sprite layout against the older multi-sprite one. */
    RSCache_ProfileSetGroupRevision(&local, RSCACHE_TYPE_TEXTURE, tex_arch->revision);
    tex_files =
        RSCache_FileListNewFromDecode(tex_arch->data, tex_arch->data_size, tex_arch->file_count);
    if( !tex_files )
    {
        fprintf(stderr, "textures: archive 0 did not split\n");
        return 0;
    }

    for( int w = 0; w < wanted_n; w++ )
    {
        int id = wanted[w];
        int pos = tool_archive_file_position(tex_arch, id);
        struct RSCache_Dat2Texture* def = NULL;
        struct RSCache_Dat2DiskArchive* sprite_arch = NULL;
        struct RSCache_Dat2SpritePack* pack = NULL;
        struct ToriDraw_Texture* tex = NULL;
        int sprites_table;
        int n;

        if( pos < 0 || pos >= tex_files->file_count || tex_files->file_sizes[pos] <= 0 )
        {
            fprintf(stderr, "texture %d: not in the texture table\n", id);
            continue;
        }
        def = RSCache_Dat2TextureNewDecodeProfile(&local, tex_files->files[pos],
                                                  tex_files->file_sizes[pos]);
        if( !def || def->sprite_ids_count <= 0 )
        {
            fprintf(stderr, "texture %d: no sprite\n", id);
            if( def )
                RSCache_Dat2TextureFree(def);
            continue;
        }
        /* Only the first layer. Multi-sprite defs need a blend stack that
         * ToriDraw_RSCacheTextureFromSprite does not implement; taking layer 0
         * is what it does for them, and saying so here beats a silent one. */
        if( def->sprite_ids_count > 1 )
            fprintf(stderr, "texture %d: %d layers, baking the first\n", id,
                    def->sprite_ids_count);

        sprites_table = RSCache_Dat2DiskTableId(cache->disk, RSCACHE_DAT2_TABLE_SPRITES);
        sprite_arch =
            RSCache_Dat2DiskArchiveNewLoad(cache->disk, sprites_table, def->sprite_ids[0]);
        /*
         * NORMALIZE is not optional here.
         *
         * A sprite is stored cropped: `width`/`height` are the sprite's size in
         * memory and `crop_width`/`crop_height` the size actually on disk, with
         * `palette_pixels` holding only the cropped region. The texture baker
         * walks `width * height` -- so on an un-normalised pack it reads past
         * the end of the pixel buffer, which is the out-of-range palette index
         * that trips its assert. Normalising expands the sprite to its memory
         * dimensions and zeroes the offsets, which is the shape it wants.
         */
        if( sprite_arch )
            pack = RSCache_Dat2SpritePackNewDecode((const unsigned char*)sprite_arch->data,
                                                   sprite_arch->data_size,
                                                   RSCache_Dat2SpriteFlags(&local) |
                                                       RSCACHE_SPRITELOAD_FLAG_NORMALIZE);
        if( pack )
            tex = ToriDraw_RSCacheTextureFromSprite(pack, 0, def, dest_size);

        if( !tex )
        {
            fprintf(stderr, "texture %d: sprite %d did not bake\n", id, def->sprite_ids[0]);
        }
        else
        {
            n = tex->width * tex->height;
            fprintf(g_out, "static const int32_t k_tex%d[] = {", id);
            for( int i = 0; i < n; i++ )
                fprintf(g_out, "%s%d", (i && (i % 16) == 0) ? ",\n" : (i ? "," : ""),
                        tex->texels[i]);
            fprintf(g_out, "};\n");
            fprintf(g_out,
                    "static const struct ToriDraw_Texture k_texdef%d = { (int*)k_tex%d, "
                    "%d, %d, %d, /*borrowed*/ 1, %d, %d };\n",
                    id, id, tex->width, tex->height, (int)tex->opaque,
                    tex->animation_direction, tex->animation_speed);
            g_tex_ids[emitted++] = id;
            fprintf(stderr, "texture %d: %dx%d, %s\n", id, tex->width, tex->height,
                    tex->opaque ? "opaque" : "with alpha");
            ToriDraw_TextureFree(tex);
        }
        if( pack )
            RSCache_Dat2SpritePackFree(pack);
        if( sprite_arch )
            RSCache_Dat2DiskArchiveFree(sprite_arch);
        RSCache_Dat2TextureFree(def);
    }

    RSCache_FileListFree(tex_files);
    RSCache_Dat2DiskArchiveFree(tex_arch);
    return emitted;
}

int
main(int argc, char** argv)
{
    struct RSCache profile;
    struct Tool_Dat2Cache cache;
    const char* cache_dir = NULL;
    const char* rev = NULL;
    const char* out_path = NULL;
    int model_id = -1;
    int frame_ids[256];
    int frame_delays[256];
    int frame_n = 0;
    int delay_n = 0;
    /*
     * The reference rig, and the knobs that move away from it.
     *
     * lightness = ambient + (L.N) / (attenuation * face_count), and that
     * lightness then SCALES the face's own base lightness: the raster gets
     * `base * lightness / 128`, clamped to [2,126].
     *
     * So the two do different jobs. `ambient` is the floor -- it sets how
     * bright an unlit face is, and moving it slides the whole model up or
     * down. `attenuation` DIVIDES the directional term, so lowering it widens
     * the swing on both sides of the floor without moving the floor: that is
     * contrast, and it is the knob to reach for first. Raising it flattens the
     * model toward a single ambient-lit colour.
     *
     * Both clamp at 2 and 126, so past a point more contrast is just clipping,
     * and the histogram printed after the bake says when that has happened.
     */
    struct ToriDraw_RSCacheLight light = TORIDRAW_RSCACHE_LIGHT_DEFAULT;
    double gamma = 1.0;

    struct RSCache_Model* raw;
    struct ToriDraw_Model* m;
    struct ToriDraw_Animation* anim = NULL;
    struct RSCache_Dat2Framemap* framemap = NULL;

    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--rev") == 0 && i + 1 < argc )
            rev = argv[++i];
        else if( strcmp(argv[i], "--model") == 0 && i + 1 < argc )
            model_id = atoi(argv[++i]);
        else if( strcmp(argv[i], "--delays") == 0 && i + 1 < argc )
        {
            /*
             * THE HOLD TIMES, which do not come from the cache.
             *
             * A frame archive carries the POSE. How long to hold it is in the
             * seq config -- `frame=<id>,<ticks>` in all.seq -- and nothing in
             * the frame data knows it. So a frame decoded straight out of the
             * cache has delay 0, and a player that falls back to "one tick"
             * for a zero runs the whole sequence at 50 poses a second.
             *
             * That is what this animation did. The script that reads all.seq
             * was taking field 0 and dropping field 1 on the floor.
             */
            char* list = argv[++i];
            for( char* tok = strtok(list, ","); tok && delay_n < 256;
                 tok = strtok(NULL, ",") )
                frame_delays[delay_n++] = (int)strtoul(tok, NULL, 0);
        }
        else if( strcmp(argv[i], "--gamma") == 0 && i + 1 < argc )
            gamma = atof(argv[++i]);
        else if( strcmp(argv[i], "--ambient") == 0 && i + 1 < argc )
            light.ambient = atoi(argv[++i]);
        else if( strcmp(argv[i], "--attenuation") == 0 && i + 1 < argc )
            light.attenuation = atoi(argv[++i]);
        else if( strcmp(argv[i], "--light") == 0 && i + 1 < argc )
        {
            char* v = argv[++i];
            char* tok = strtok(v, ",");
            if( tok )
                light.x = atoi(tok);
            tok = strtok(NULL, ",");
            if( tok )
                light.y = atoi(tok);
            tok = strtok(NULL, ",");
            if( tok )
                light.z = atoi(tok);
        }
        else if( strcmp(argv[i], "--frames") == 0 && i + 1 < argc )
        {
            char* list = argv[++i];
            for( char* tok = strtok(list, ","); tok && frame_n < 256;
                 tok = strtok(NULL, ",") )
                frame_ids[frame_n++] = (int)strtoul(tok, NULL, 0);
        }
        else if( !cache_dir )
            cache_dir = argv[i];
        else
            out_path = argv[i];
    }

    if( !cache_dir || !rev || !out_path || model_id < 0 )
    {
        fprintf(stderr,
                "usage: %s <cache_dir> --rev <name> --model ID "
                "[--frames id,id,...] [--delays n,n,...] [--ambient N] "
                "[--attenuation N] [--light x,y,z] [--gamma G] <out.c>\n",
                argv[0]);
        return 2;
    }
    if( !tool_resolve_profile(rev, NULL, NULL, NULL, NULL, &profile) )
        return 2;
    if( !tool_dat2_open(cache_dir, &profile, &cache) )
    {
        fprintf(stderr, "cannot open %s\n", cache_dir);
        return 1;
    }

    ToriDraw_Init();

    raw = tool_dat2_model_load(&cache, model_id);
    if( !raw )
    {
        fprintf(stderr, "model %d: absent\n", model_id);
        return 1;
    }
    m = ToriDraw_RSCacheModelSteal(raw);
    RSCache_ModelFree(raw);
    if( !m )
    {
        fprintf(stderr, "model %d: conversion failed\n", model_id);
        return 1;
    }

    /* Lit HERE, once. The device receives colours, not normals. */
    ToriDraw_RSCacheModelLight(m, &light);
    apply_lightness_gamma(m, gamma);
    fprintf(stderr, "gamma %.2f\n", gamma);
    report_contrast(m, &light);
    ToriDraw_ModelCaptureOriginalVertices(m);
    ToriDraw_ModelSetBoundsCylinder(m);

    if( frame_n > 0 )
    {
        struct RSCache_Dat2DiskArchive* a0 = RSCache_Dat2DiskArchiveNewLoad(
            cache.disk,
            RSCache_Dat2DiskTableId(cache.disk, RSCACHE_DAT2_TABLE_ANIMATIONS),
            (frame_ids[0] >> 16) & 0xFFFF);
        struct RSCache_Dat2Frame** frames = calloc((size_t)frame_n, sizeof(*frames));
        int decoded = 0;

        if( !a0 )
        {
            fprintf(stderr, "frame archive absent\n");
            return 1;
        }
        framemap = tool_dat2_framemap_load(
            &cache, RSCache_Dat2FramemapIdFromFrameArchive(a0->data, a0->data_size));
        RSCache_Dat2DiskArchiveFree(a0);
        if( !framemap )
        {
            fprintf(stderr, "framemap did not decode\n");
            return 1;
        }
        for( int i = 0; i < frame_n; i++ )
        {
            frames[decoded] = tool_dat2_frame_load(&cache, framemap, frame_ids[i]);
            if( frames[decoded] )
                decoded++;
        }
        if( decoded > 0 )
            anim = ToriDraw_RSCacheAnimationNew(
                framemap, (const struct RSCache_Dat2Frame* const*)frames, decoded);
        for( int i = 0; i < decoded; i++ )
            RSCache_Dat2FrameFree(frames[i]);
        free(frames);
        fprintf(stderr, "baked %d of %d frames\n", decoded, frame_n);
    }

    g_out = fopen(out_path, "wb");
    if( !g_out )
    {
        fprintf(stderr, "cannot write %s\n", out_path);
        return 1;
    }

    fprintf(g_out,
            "/*\n"
            " * GENERATED by tools/bake_model/bake_model.c. Do not edit.\n"
            " *\n"
            " * model %d, %d vertices, %d faces%s.\n"
            " * Colours are ALREADY LIT: do not light this model again.\n"
            " */\n\n"
            "#include \"toridraw.h\"\n"
            "#include \"toridraw_animation.h\"\n"
            "#include \"xmas_baked.h\"\n\n"
            "#include <string.h>\n\n",
            model_id, m->vertex_count, m->face_count,
            anim ? ", with animation" : "");

    /* ---- the model -------------------------------------------------- */
    int have_ovx = emit_i16("k_ovx", (const void*)m->original_vertices_x, m->vertex_count);
    int have_ovy = emit_i16("k_ovy", (const void*)m->original_vertices_y, m->vertex_count);
    int have_ovz = emit_i16("k_ovz", (const void*)m->original_vertices_z, m->vertex_count);
    int have_fia = emit_i16("k_fia", (const void*)m->face_indices_a, m->face_count);
    int have_fib = emit_i16("k_fib", (const void*)m->face_indices_b, m->face_count);
    int have_fic = emit_i16("k_fic", (const void*)m->face_indices_c, m->face_count);
    int have_fca = emit_u16("k_fca", (const void*)m->face_colors_a, m->face_count);
    int have_fcb = emit_u16("k_fcb", (const void*)m->face_colors_b, m->face_count);
    int have_fcc = emit_u16("k_fcc", (const void*)m->face_colors_c, m->face_count);
    int have_ftx = emit_i16("k_ftx", (const void*)m->face_textures, m->face_count);
    int have_fal = emit_u8("k_fal", (const void*)m->face_alphas, m->face_count);
    int have_fnf = emit_i32("k_fnf", (const void*)m->face_infos, m->face_count);
    int have_fpr = emit_u8("k_fpr", (const void*)m->face_priorities, (m->face_count + 1) / 2);
    int have_fcl = emit_u16("k_fcl", (const void*)m->face_colors, m->face_count);
    int have_tpc = emit_i16("k_tpc", (const void*)m->textured_p_coordinate, m->textured_face_count);
    int have_tmc = emit_i16("k_tmc", (const void*)m->textured_m_coordinate, m->textured_face_count);
    int have_tnc = emit_i16("k_tnc", (const void*)m->textured_n_coordinate, m->textured_face_count);

    /*
     * THE FACE BONE MAP, which is not the vertex one.
     *
     * A framemap transform of type 5 is a TRANSPARENCY op: it walks
     * `face_bones` and adds to `face_alphas`, which is how the spirit tree's
     * orbs fade in and out. It is the only transform that touches faces rather
     * than vertices, and it is why this map has to be baked separately --
     * ToriDraw_ModelApplyTransform returns immediately on a model that has
     * face_alphas but no face_bones, silently, so a model missing it animates
     * perfectly except that nothing ever fades.
     */
    if( m->face_bones && m->face_bones->bones_count > 0 )
    {
        int bc = m->face_bones->bones_count;
        for( int b = 0; b < bc; b++ )
        {
            char name[32];
            snprintf(name, sizeof(name), "k_fb_%d", b);
            if( m->face_bones->bones_sizes[b] > 0 )
            {
                fprintf(g_out, "static const uint16_t %s[] = {", name);
                for( int i = 0; i < m->face_bones->bones_sizes[b]; i++ )
                    fprintf(g_out, "%s%u", i ? "," : "", m->face_bones->bones[b][i]);
                fprintf(g_out, "};\n");
            }
        }
        fprintf(g_out, "static const uint16_t* const k_fb_tab[] = {");
        for( int b = 0; b < bc; b++ )
        {
            if( m->face_bones->bones_sizes[b] > 0 )
                fprintf(g_out, "%sk_fb_%d", b ? "," : "", b);
            else
                fprintf(g_out, "%sNULL", b ? "," : "");
        }
        fprintf(g_out, "};\n");
        emit_u16("k_fb_sz", m->face_bones->bones_sizes, bc);
        fprintf(g_out,
                "static const struct ToriDraw_Bones k_face_bones = { %d, "
                "(uint16_t**)k_fb_tab, (uint16_t*)k_fb_sz };\n",
                bc);
    }
    fprintf(stderr, "face bones: %d groups%s\n",
            m->face_bones ? m->face_bones->bones_count : 0,
            (m->face_bones && m->face_bones->bones_count > 0)
                ? ""
                : "  <-- no alpha animation possible");

    /* The bone map is an array OF arrays; each group is emitted, then a table
     * of pointers to them. */
    if( m->vertex_bones && m->vertex_bones->bones_count > 0 )
    {
        int bc = m->vertex_bones->bones_count;
        for( int b = 0; b < bc; b++ )
        {
            char name[32];
            snprintf(name, sizeof(name), "k_vb_%d", b);
            if( m->vertex_bones->bones_sizes[b] > 0 )
            {
                fprintf(g_out, "static const uint16_t %s[] = {", name);
                for( int i = 0; i < m->vertex_bones->bones_sizes[b]; i++ )
                    fprintf(g_out, "%s%u", i ? "," : "", m->vertex_bones->bones[b][i]);
                fprintf(g_out, "};\n");
            }
        }
        fprintf(g_out, "static const uint16_t* const k_vb_tab[] = {");
        for( int b = 0; b < bc; b++ )
        {
            if( m->vertex_bones->bones_sizes[b] > 0 )
                fprintf(g_out, "%sk_vb_%d", b ? "," : "", b);
            else
                fprintf(g_out, "%sNULL", b ? "," : "");
        }
        fprintf(g_out, "};\n");
        emit_u16("k_vb_sz", m->vertex_bones->bones_sizes, bc);
        fprintf(g_out,
                "static const struct ToriDraw_Bones k_bones = { %d, (uint16_t**)k_vb_tab, "
                "(uint16_t*)k_vb_sz };\n",
                bc);
    }

    fprintf(g_out,
            "\nconst int xmas_baked_vertex_count = %d;\n"
            "const int xmas_baked_face_count = %d;\n\n"
            "void\nxmas_baked_model_init(struct ToriDraw_Model* m,\n"
            "                      int16_t* vx, int16_t* vy, int16_t* vz,\n"
            "                      uint8_t* fa)\n{\n"
            "    memset(m, 0, sizeof(*m));\n"
            "    m->vertex_count = %d;\n"
            "    m->face_count = %d;\n"
            "    m->textured_face_count = %d;\n"
            "    m->model_priority = %u;\n",
            m->vertex_count, m->face_count, m->vertex_count, m->face_count,
            m->textured_face_count, m->model_priority);

    fprintf(g_out,
            "    /* The bind pose is const; the live vertices are the caller's. */\n"
            "    m->original_vertices_x = (int16_t*)k_ovx;\n"
            "    m->original_vertices_y = (int16_t*)k_ovy;\n"
            "    m->original_vertices_z = (int16_t*)k_ovz;\n"
            "    memcpy(vx, k_ovx, sizeof(k_ovx));\n"
            "    memcpy(vy, k_ovy, sizeof(k_ovy));\n"
            "    memcpy(vz, k_ovz, sizeof(k_ovz));\n"
            "    m->vertices_x = vx;\n"
            "    m->vertices_y = vy;\n"
            "    m->vertices_z = vz;\n");

    fprintf(g_out, "    m->face_indices_a = %s;\n",
            ref("k_fia", have_fia));
    fprintf(g_out, "    m->face_indices_b = %s;\n",
            ref("k_fib", have_fib));
    fprintf(g_out, "    m->face_indices_c = %s;\n",
            ref("k_fic", have_fic));
    fprintf(g_out, "    m->face_colors_a = %s;\n",
            ref("k_fca", have_fca));
    fprintf(g_out, "    m->face_colors_b = %s;\n",
            ref("k_fcb", have_fcb));
    fprintf(g_out, "    m->face_colors_c = %s;\n",
            ref("k_fcc", have_fcc));
    fprintf(g_out, "    m->face_textures = %s;\n",
            ref("k_ftx", have_ftx));
    /*
     * ALPHA IS LIVE STATE, exactly like the vertices.
     *
     * A type-5 transform WRITES face_alphas, and ToriDraw_ModelAnimateReset
     * restores them from original_face_alphas every frame -- the same
     * bind-copy/live-copy pair the vertices use. Pointing face_alphas straight
     * at the const array (which is what this did) puts a per-frame write into
     * flash, so the fade is dropped and the model animates correctly in every
     * respect except that nothing ever fades.
     */
    if( have_fal )
        fprintf(g_out,
                "    m->original_face_alphas = (void*)k_fal;\n"
                "    memcpy(fa, k_fal, sizeof(k_fal));\n"
                "    m->face_alphas = fa;\n");
    else
        fprintf(g_out,
                "    m->original_face_alphas = NULL;\n"
                "    m->face_alphas = NULL;\n");
    fprintf(g_out, "    m->face_infos = %s;\n",
            ref("k_fnf", have_fnf));
    fprintf(g_out, "    m->face_priorities = %s;\n",
            ref("k_fpr", have_fpr));
    fprintf(g_out, "    m->face_colors = %s;\n",
            ref("k_fcl", have_fcl));
    fprintf(g_out, "    m->textured_p_coordinate = %s;\n",
            ref("k_tpc", have_tpc));
    fprintf(g_out, "    m->textured_m_coordinate = %s;\n",
            ref("k_tmc", have_tmc));
    fprintf(g_out, "    m->textured_n_coordinate = %s;\n",
            ref("k_tnc", have_tnc));

    if( m->vertex_bones && m->vertex_bones->bones_count > 0 )
        fprintf(g_out, "    m->vertex_bones = (struct ToriDraw_Bones*)&k_bones;\n");
    if( m->face_bones && m->face_bones->bones_count > 0 )
        fprintf(g_out, "    m->face_bones = (struct ToriDraw_Bones*)&k_face_bones;\n");

    fprintf(g_out,
            "    /* Computed at bake time, from the bind pose. */\n"
            "    m->has_bounds_cylinder = %d;\n"
            "    m->bounds_cylinder.center_to_top_edge = %d;\n"
            "    m->bounds_cylinder.center_to_bottom_edge = %d;\n"
            "    m->bounds_cylinder.min_y = %d;\n"
            "    m->bounds_cylinder.max_y = %d;\n"
            "    m->bounds_cylinder.radius = %d;\n"
            "    m->bounds_cylinder.min_z_depth_any_rotation = %d;\n"
            "}\n",
            (int)m->has_bounds_cylinder, m->bounds_cylinder.center_to_top_edge,
            m->bounds_cylinder.center_to_bottom_edge, m->bounds_cylinder.min_y,
            m->bounds_cylinder.max_y, m->bounds_cylinder.radius,
            m->bounds_cylinder.min_z_depth_any_rotation);

    /* ---- the animation ---------------------------------------------- */
    if( anim && anim->base && anim->frames )
    {
        const struct ToriDraw_AnimBase* b = anim->base;

        fprintf(g_out, "\n");
        emit_u8("k_ab_types", b->types, b->length);
        for( int g = 0; g < b->length; g++ )
        {
            if( b->bone_group_lengths[g] > 0 )
            {
                fprintf(g_out, "static const uint8_t k_ab_g%d[] = {", g);
                for( int i = 0; i < b->bone_group_lengths[g]; i++ )
                    fprintf(g_out, "%s%u", i ? "," : "", b->bone_groups[g][i]);
                fprintf(g_out, "};\n");
            }
        }
        fprintf(g_out, "static const uint8_t* const k_ab_gtab[] = {");
        for( int g = 0; g < b->length; g++ )
        {
            if( b->bone_group_lengths[g] > 0 )
                fprintf(g_out, "%sk_ab_g%d", g ? "," : "", g);
            else
                fprintf(g_out, "%sNULL", g ? "," : "");
        }
        fprintf(g_out, "};\n");
        emit_u16("k_ab_glen", b->bone_group_lengths, b->length);
        fprintf(g_out,
                "static const struct ToriDraw_AnimBase k_ab = { %d, (uint8_t*)k_ab_types, "
                "(uint8_t**)k_ab_gtab, (uint16_t*)k_ab_glen };\n\n",
                b->length);

        for( int f = 0; f < anim->frame_count; f++ )
        {
            const struct ToriDraw_AnimFrame* fr = &anim->frames[f];
            char n[40];
            snprintf(n, sizeof(n), "k_f%d_g", f);
            emit_i16(n, fr->groups, fr->length);
            snprintf(n, sizeof(n), "k_f%d_x", f);
            emit_i16(n, fr->x, fr->length);
            snprintf(n, sizeof(n), "k_f%d_y", f);
            emit_i16(n, fr->y, fr->length);
            snprintf(n, sizeof(n), "k_f%d_z", f);
            emit_i16(n, fr->z, fr->length);
        }

        fprintf(g_out, "static const struct ToriDraw_AnimFrame k_frames[] = {\n");
        for( int f = 0; f < anim->frame_count; f++ )
        {
            const struct ToriDraw_AnimFrame* fr = &anim->frames[f];
            int hold = (f < delay_n && frame_delays[f] > 0) ? frame_delays[f] : fr->delay;

            fprintf(g_out, "  { %d, %d, ", fr->id, fr->length);
            if( fr->length > 0 && fr->groups )
                fprintf(g_out, "(int16_t*)k_f%d_g, (int16_t*)k_f%d_x, "
                               "(int16_t*)k_f%d_y, (int16_t*)k_f%d_z, %d },\n",
                        f, f, f, f, hold);
            else
                fprintf(g_out, "NULL, NULL, NULL, NULL, %d },\n", hold);
        }
        fprintf(g_out, "};\n\n");

        fprintf(g_out,
                "static const struct ToriDraw_Animation k_anim = {\n"
                "  (struct ToriDraw_AnimBase*)&k_ab,\n"
                "  (struct ToriDraw_AnimFrame*)k_frames,\n"
                "  %d, %d,\n"
                "};\n\n"
                "const struct ToriDraw_Animation* xmas_baked_animation(void)\n"
                "{\n    return &k_anim;\n}\n",
                anim->frame_count, anim->frame_step);
    }
    else
    {
        fprintf(g_out,
                "\nconst struct ToriDraw_Animation* xmas_baked_animation(void)\n"
                "{\n    return NULL;\n}\n");
    }

    /* ---- the textures ------------------------------------------------ */
    {
        int tex_n = emit_textures(&cache, &profile, m, 128);

        fprintf(g_out, "\n");
        if( tex_n > 0 )
        {
            fprintf(g_out, "static const struct XmasBakedTexture k_textures[] = {\n");
            for( int i = 0; i < tex_n; i++ )
                fprintf(g_out, "  { %d, (struct ToriDraw_Texture*)&k_texdef%d },\n",
                        g_tex_ids[i], g_tex_ids[i]);
            fprintf(g_out, "};\n\n");
        }
        fprintf(g_out,
                "int xmas_baked_texture_count(void)\n{\n    return %d;\n}\n\n"
                "const struct XmasBakedTexture* xmas_baked_textures(void)\n"
                "{\n    return %s;\n}\n",
                tex_n, tex_n > 0 ? "k_textures" : "NULL");
    }

    fclose(g_out);
    fprintf(stderr, "wrote %s\n", out_path);

    if( framemap )
        RSCache_Dat2FramemapFree(framemap);
    tool_dat2_close(&cache);
    return 0;
}
