/*
 * Host tool: extract a decompressed dat1 model archive for flashing to ESP32.
 *
 * Usage: extract_model <cache_dir> <model_archive_id> <out.bin>
 *
 * Output format: 4-byte little-endian payload size, then raw model bytes
 * (same buffer passed to model_new_decode on device).
 */

#include "osrs/rscache/cache_dat.h"
#include "osrs/rscache/tables/model.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int
write_model_bin(const char* out_path, const unsigned char* data, int size)
{
    FILE* f = fopen(out_path, "wb");
    if( !f )
        return -1;

    uint32_t hdr = (uint32_t)size;
    if( fwrite(&hdr, 1, sizeof(hdr), f) != sizeof(hdr) )
    {
        fclose(f);
        return -1;
    }
    if( fwrite(data, 1, (size_t)size, f) != (size_t)size )
    {
        fclose(f);
        return -1;
    }

    fclose(f);
    return 0;
}

int
main(int argc, char** argv)
{
    if( argc != 4 )
    {
        fprintf(stderr, "usage: %s <cache_dir> <model_archive_id> <out.bin>\n", argv[0]);
        return 1;
    }

    const char* cache_dir = argv[1];
    int model_id = atoi(argv[2]);
    const char* out_path = argv[3];

    struct CacheDat* cache = cache_dat_new_from_directory(cache_dir);
    if( !cache )
    {
        fprintf(stderr, "failed to open cache at %s\n", cache_dir);
        return 2;
    }

    struct CacheDatArchive* archive =
        cache_dat_archive_new_load(cache, CACHE_DAT_MODELS, model_id);
    if( !archive )
    {
        fprintf(stderr, "failed to load model archive %d\n", model_id);
        cache_dat_free(cache);
        return 3;
    }

    struct CacheModel* model =
        model_new_decode((const unsigned char*)archive->data, archive->data_size);
    if( !model )
    {
        fprintf(stderr, "failed to decode model archive %d (%d bytes)\n", model_id, archive->data_size);
        cache_dat_archive_free(archive);
        cache_dat_free(cache);
        return 4;
    }

    printf(
        "model %d: %d vertices, %d faces (%d bytes)\n",
        model_id,
        model->vertex_count,
        model->face_count,
        archive->data_size);
    model_free(model);

    if( write_model_bin(out_path, (const unsigned char*)archive->data, archive->data_size) != 0 )
    {
        fprintf(stderr, "failed to write %s\n", out_path);
        cache_dat_archive_free(archive);
        cache_dat_free(cache);
        return 5;
    }

    printf("wrote %s (%d byte payload)\n", out_path, archive->data_size);
    cache_dat_archive_free(archive);
    cache_dat_free(cache);
    return 0;
}
