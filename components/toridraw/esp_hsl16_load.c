/*
 * Load precomputed HSL16->RGB table from flash instead of running pow() on-device.
 */

#include "graphics/shared_tables.h"

#include <string.h>

extern const uint8_t hsl16_rgb_table_bin_start[] asm("_binary_hsl16_rgb_table_bin_start");
extern const uint8_t hsl16_rgb_table_bin_end[] asm("_binary_hsl16_rgb_table_bin_end");

void
__wrap_init_hsl16_to_rgb_table(void)
{
    const size_t table_bytes = (size_t)(hsl16_rgb_table_bin_end - hsl16_rgb_table_bin_start);
    if( table_bytes != sizeof(g_hsl16_to_rgb_table) )
        return;

    memcpy(g_hsl16_to_rgb_table, hsl16_rgb_table_bin_start, table_bytes);
}
