/*
 * Host tool: pull one dat1 model archive out of an OSRS cache and write the
 * blob the ESP32 flashes.
 *
 *   extract_model <cache_dir> <model_archive_id> <out.bin>
 *
 * The output is a 4-byte little-endian payload length followed by the
 * DECOMPRESSED archive bytes -- the same buffer main.c hands to
 * ToriDraw_RSCacheModelFromBlob on the device. Nothing here decodes the
 * model beyond checking that it CAN be decoded: shipping a blob the device
 * will choke on is the one failure that is expensive to diagnose over a
 * serial line, so it is caught on the host where the error can say why.
 */

#include <rscache.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * ONE CONTAINER, ANIMATED OR NOT. pack_anim.c writes a header, a model, a
 * framemap and N frames; this writes the same header and the same model
 * section, then a framemap of length zero and a frame count of zero.
 *
 * The alternative -- a bare [len][bytes] for the static case -- meant the
 * device had to sniff which layout it had been given, and a partition holding
 * the other one would decode as garbage rather than say so. One layout with
 * empty sections costs eight bytes and removes that entirely.
 */
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

static int
write_model_bin(const char* path, const unsigned char* data, int size)
{
    FILE* f = fopen(path, "wb");

    if( !f )
    {
        fprintf(stderr, "cannot open %s for writing\n", path);
        return -1;
    }

    put_u32(f, XMAS_BLOB_MAGIC);
    put_u32(f, XMAS_BLOB_VERSION);
    put_u32(f, 0); /* frame codec: no frames  */
    put_u32(f, 0); /* framemap codec: no rig  */
    put_u32(f, 0); /* framemap id: no rig     */

    put_u32(f, (uint32_t)size);
    if( fwrite(data, 1, (size_t)size, f) != (size_t)size )
    {
        fprintf(stderr, "short write to %s\n", path);
        fclose(f);
        return -1;
    }

    put_u32(f, 0); /* framemap length */
    put_u32(f, 0); /* frame count     */

    fclose(f);
    return 0;
}

/*
 * The other source of a model: a raw archive already on disk.
 *
 * OSRS-Content stores every model as its DECOMPRESSED archive under a name --
 * models/loc/spirit_tree_11.model -- which is the same bytes this tool would
 * pull out of a cache and hand to the decoder, minus the cache lookup. When
 * the model you want has a name and not an id, that is the shorter road, and
 * it needs no cache at all.
 */
static int
from_file(const char* path, const char* out_path)
{
    FILE* fh = fopen(path, "rb");
    unsigned char* buf;
    long size;
    struct RSCache_Model* model;
    int rc;

    if( !fh )
    {
        fprintf(stderr, "cannot read %s\n", path);
        return 1;
    }
    fseek(fh, 0, SEEK_END);
    size = ftell(fh);
    fseek(fh, 0, SEEK_SET);
    if( size <= 0 )
    {
        fprintf(stderr, "%s is empty\n", path);
        fclose(fh);
        return 1;
    }
    buf = malloc((size_t)size);
    if( fread(buf, 1, (size_t)size, fh) != (size_t)size )
    {
        fprintf(stderr, "short read from %s\n", path);
        fclose(fh);
        free(buf);
        return 1;
    }
    fclose(fh);

    model = RSCache_ModelNewDecode(buf, (int)size);
    if( !model )
    {
        fprintf(stderr, "%s (%ld bytes) is not a model this codec decodes\n", path, size);
        free(buf);
        return 1;
    }
    printf("%s: %d vertices, %d faces, %ld byte payload\n", path, model->vertex_count,
           model->face_count, size);
    RSCache_ModelFree(model);

    rc = write_model_bin(out_path, buf, (int)size);
    free(buf);
    if( rc != 0 )
        return 1;
    printf("wrote %s\n", out_path);
    return 0;
}

int
main(int argc, char** argv)
{
    struct RSCache_Dat1Disk* cache;
    struct RSCache_Dat1DiskArchive* archive;
    struct RSCache_Model* model;
    const char* cache_dir;
    const char* out_path;
    int model_id;

    if( argc == 4 && strcmp(argv[1], "--file") == 0 )
        return from_file(argv[2], argv[3]);

    if( argc != 4 )
    {
        fprintf(stderr,
                "usage: %s <cache_dir> <model_archive_id> <out.bin>\n"
                "       %s --file <model_archive.model> <out.bin>\n",
                argv[0], argv[0]);
        return 2;
    }

    cache_dir = argv[1];
    model_id = atoi(argv[2]);
    out_path = argv[3];

    cache = RSCache_Dat1DiskNewFromDirectory(cache_dir);
    if( !cache )
    {
        fprintf(stderr, "failed to open dat1 cache at %s\n", cache_dir);
        return 1;
    }

    archive = RSCache_Dat1DiskArchiveNewLoad(cache, RSCACHE_DAT1_DISK_TABLE_MODELS, model_id);
    if( !archive )
    {
        fprintf(stderr, "failed to load model archive %d\n", model_id);
        RSCache_Dat1DiskFree(cache);
        return 1;
    }

    /* Decoded and thrown away: this is a check, not the payload. The device
     * decodes the same bytes with the same codec. */
    model = RSCache_ModelNewDecode((uint8_t*)archive->data, archive->data_size);
    if( !model )
    {
        fprintf(stderr, "archive %d (%d bytes) is not a model this codec decodes\n",
                model_id, archive->data_size);
        RSCache_Dat1DiskArchiveFree(archive);
        RSCache_Dat1DiskFree(cache);
        return 1;
    }

    printf("model %d: %d vertices, %d faces, %d byte payload\n", model_id,
           model->vertex_count, model->face_count, archive->data_size);
    RSCache_ModelFree(model);

    if( write_model_bin(out_path, (const unsigned char*)archive->data,
                        archive->data_size) != 0 )
    {
        RSCache_Dat1DiskArchiveFree(archive);
        RSCache_Dat1DiskFree(cache);
        return 1;
    }

    printf("wrote %s\n", out_path);
    RSCache_Dat1DiskArchiveFree(archive);
    RSCache_Dat1DiskFree(cache);
    return 0;
}
