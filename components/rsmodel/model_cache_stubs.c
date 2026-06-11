/*
 * Stubs for dat2 cache loaders referenced by model.c but unused on-device.
 */

#include "osrs/rscache/cache.h"

struct CacheArchive*
cache_archive_new_load(
    struct Cache* cache,
    int table_id,
    int archive_id)
{
    (void)cache;
    (void)table_id;
    (void)archive_id;
    return NULL;
}

void
cache_archive_free(struct CacheArchive* archive)
{
    (void)archive;
}
