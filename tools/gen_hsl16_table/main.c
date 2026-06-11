/*
 * Host tool: precompute g_hsl16_to_rgb_table for embedding on ESP32.
 *
 * Usage: gen_hsl16_table <out.bin>
 */

#include "graphics/shared_tables.h"

#include <stdio.h>
#include <stdlib.h>

int
main(int argc, char** argv)
{
    if( argc != 2 )
    {
        fprintf(stderr, "usage: %s <out.bin>\n", argv[0]);
        return 1;
    }

    init_hsl16_to_rgb_table();

    FILE* f = fopen(argv[1], "wb");
    if( !f )
    {
        perror("fopen");
        return 2;
    }

    size_t written = fwrite(g_hsl16_to_rgb_table, sizeof(int), 65536, f);
    fclose(f);

    if( written != 65536 )
    {
        fprintf(stderr, "short write: %zu entries\n", written);
        return 3;
    }

    printf("wrote %s (262144 bytes)\n", argv[1]);
    return 0;
}
