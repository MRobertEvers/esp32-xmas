/*
 * Host tool: bundle a model, its rig and a sequence's frames into the single
 * blob the ESP32 flashes to its `model` partition.
 *
 *   pack_anim <cache_dir> --rev <name> --model ID --frames id,id,... <out.bin>
 *
 * WHY A CONTAINER AND NOT THREE PARTITIONS. The device needs four things that
 * only make sense together -- geometry, a framemap, N frames, and the codec
 * version they were written with -- and a partition table entry for each is
 * four things to keep in step across a reflash. One blob with a header is one
 * thing, and a version field in it means a device holding an older layout says
 * so instead of decoding garbage.
 *
 * WHAT IS *NOT* DONE HERE. The frames are stored as their raw archive bytes,
 * not pre-decoded. Decoding needs the framemap to know how many transforms
 * each entry carries, so a pre-decoded frame is a second serialisation format
 * to design, version and keep in step with RSCache's own. The device already
 * links the decoder; handing it the bytes RSCache wrote is strictly less code
 * and strictly fewer things to get wrong.
 *
 * The frame ids matter and are carried: a frame's id is not its index. The
 * sequence lists them in play order and the device replays that order.
 */

#include "asset_access.h"
#include "tool_profile.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Bumped whenever the layout below changes. The device refuses a blob whose
 * version it does not know rather than reading past the end of a section. */
#define XMAS_BLOB_MAGIC   0x584D4131u /* "XMA1" */
#define XMAS_BLOB_VERSION 2

static void
put_u32(FILE* f, uint32_t v)
{
    uint8_t b[4];
    b[0] = (uint8_t)(v & 0xFF);
    b[1] = (uint8_t)((v >> 8) & 0xFF);
    b[2] = (uint8_t)((v >> 16) & 0xFF);
    b[3] = (uint8_t)((v >> 24) & 0xFF);
    fwrite(b, 1, 4, f);
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
    int frame_n = 0;

    struct RSCache_Dat2DiskArchive* model_arch = NULL;
    struct RSCache_Dat2Framemap* framemap = NULL;
    struct RSCache_Dat2DiskArchive* framemap_arch = NULL;
    struct RSCache_Dat2DiskArchive* frame_arch = NULL;
    int framemap_id = -1;
    int frames_table, models_table, framemaps_table;
    FILE* out;
    int kept = 0;

    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--rev") == 0 && i + 1 < argc )
            rev = argv[++i];
        else if( strcmp(argv[i], "--model") == 0 && i + 1 < argc )
            model_id = atoi(argv[++i]);
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

    if( !cache_dir || !rev || !out_path || model_id < 0 || frame_n == 0 )
    {
        fprintf(stderr,
                "usage: %s <cache_dir> --rev <name> --model ID "
                "--frames id,id,... <out.bin>\n",
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

    models_table = RSCache_Dat2DiskTableId(cache.disk, RSCACHE_DAT2_TABLE_MODELS);
    frames_table = RSCache_Dat2DiskTableId(cache.disk, RSCACHE_DAT2_TABLE_ANIMATIONS);
    framemaps_table = RSCache_Dat2DiskTableId(cache.disk, RSCACHE_DAT2_TABLE_SKELETONS);

    model_arch = RSCache_Dat2DiskArchiveNewLoad(cache.disk, models_table, model_id);
    if( !model_arch )
    {
        fprintf(stderr, "model %d: absent\n", model_id);
        return 1;
    }

    /* The rig is named by the frame ARCHIVE, in its first two bytes. Reading it
     * from the archive rather than from a decoded frame is what lets the
     * framemap be fetched before any frame is decoded -- which is the order the
     * decoder requires. */
    frame_arch =
        RSCache_Dat2DiskArchiveNewLoad(cache.disk, frames_table, frame_ids[0] >> 16);
    if( !frame_arch )
    {
        fprintf(stderr, "frame archive %d: absent\n", frame_ids[0] >> 16);
        return 1;
    }
    framemap_id =
        RSCache_Dat2FramemapIdFromFrameArchive(frame_arch->data, frame_arch->data_size);
    framemap = tool_dat2_framemap_load(&cache, framemap_id);
    if( !framemap )
    {
        fprintf(stderr, "framemap %d: did not decode\n", framemap_id);
        return 1;
    }
    framemap_arch =
        RSCache_Dat2DiskArchiveNewLoad(cache.disk, framemaps_table, framemap_id);
    if( !framemap_arch )
    {
        fprintf(stderr, "framemap archive %d: absent\n", framemap_id);
        return 1;
    }

    out = fopen(out_path, "wb");
    if( !out )
    {
        fprintf(stderr, "cannot write %s\n", out_path);
        return 1;
    }

    /*
     * Header, then sections. Every section is length-prefixed so the device
     * walks the blob without a table of contents it could disagree with.
     *
     *   u32 magic, u32 version, u32 frame_codec, u32 framemap_codec
     *   u32 framemap_id
     *   u32 model_len,    model bytes
     *   u32 framemap_len, framemap bytes
     *   u32 frame_count,  then per frame: u32 id, u32 len, bytes
     */
    put_u32(out, XMAS_BLOB_MAGIC);
    put_u32(out, XMAS_BLOB_VERSION);
    put_u32(out, (uint32_t)RSCache_Dat2FrameCodecVersion(&profile));
    put_u32(out, (uint32_t)RSCache_Dat2FramemapCodecVersion(&profile));
    /* The frames name their rig by id in their first two bytes and the decoder
     * asserts the framemap it is handed agrees. Decoding the framemap under
     * any other id -- 0, say -- fails that assert on the first frame, which is
     * a boot loop rather than a bad picture. */
    put_u32(out, (uint32_t)framemap_id);

    put_u32(out, (uint32_t)model_arch->data_size);
    fwrite(model_arch->data, 1, (size_t)model_arch->data_size, out);

    put_u32(out, (uint32_t)framemap_arch->data_size);
    fwrite(framemap_arch->data, 1, (size_t)framemap_arch->data_size, out);

    /* Count first, so the reader can size its array before it walks. The
     * frames are written after, in play order. */
    put_u32(out, (uint32_t)frame_n);

    /*
     * A frame archive is a GROUP: its bytes are several files with the split
     * table in the tail, and file_count comes from the reference table rather
     * than the group itself -- hence InitMetadata before the split. This is
     * tool_dat2_frame_load's sequence, stopping one step short of the decode
     * because what goes in the blob is the bytes, not a decoded frame.
     */
    for( int i = 0; i < frame_n; i++ )
    {
        struct RSCache_Dat2DiskArchive* a;
        struct RSCache_FileList* files;
        int arch_id = (frame_ids[i] >> 16) & 0xFFFF;
        int file_id = frame_ids[i] & 0xFFFF;
        int pos;

        a = RSCache_Dat2DiskArchiveNewLoad(cache.disk, frames_table, arch_id);
        if( !a )
        {
            fprintf(stderr, "frame 0x%08X: archive absent\n", frame_ids[i]);
            return 1;
        }
        if( !RSCache_Dat2DiskArchiveInitMetadata(cache.disk, a) || a->file_count <= 0 )
        {
            fprintf(stderr, "frame 0x%08X: archive %d has no file table\n", frame_ids[i],
                    arch_id);
            RSCache_Dat2DiskArchiveFree(a);
            return 1;
        }
        files = RSCache_FileListNewFromDecode(a->data, a->data_size, a->file_count);
        if( !files )
        {
            fprintf(stderr, "frame 0x%08X: archive %d did not split\n", frame_ids[i],
                    arch_id);
            RSCache_Dat2DiskArchiveFree(a);
            return 1;
        }

        pos = tool_archive_file_position(a, file_id);
        if( pos < 0 || pos >= files->file_count || files->file_sizes[pos] <= 0 )
        {
            fprintf(stderr, "frame 0x%08X: file %d not in archive %d\n", frame_ids[i],
                    file_id, arch_id);
            RSCache_FileListFree(files);
            RSCache_Dat2DiskArchiveFree(a);
            return 1;
        }

        put_u32(out, (uint32_t)frame_ids[i]);
        put_u32(out, (uint32_t)files->file_sizes[pos]);
        fwrite(files->files[pos], 1, (size_t)files->file_sizes[pos], out);
        kept++;
        RSCache_FileListFree(files);
        RSCache_Dat2DiskArchiveFree(a);
    }

    fclose(out);

    printf("model %d: %d bytes\n", model_id, model_arch->data_size);
    printf("framemap %d: %d bytes, %d bones\n", framemap_id, framemap_arch->data_size,
           framemap->length);
    printf("frames: %d of %d packed\n", kept, frame_n);
    printf("wrote %s\n", out_path);

    RSCache_Dat2DiskArchiveFree(model_arch);
    RSCache_Dat2DiskArchiveFree(framemap_arch);
    RSCache_Dat2DiskArchiveFree(frame_arch);
    RSCache_Dat2FramemapFree(framemap);
    tool_dat2_close(&cache);
    return 0;
}
