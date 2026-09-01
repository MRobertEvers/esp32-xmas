#ifndef XMB_CHECK_ESP_SHIM_H
#define XMB_CHECK_ESP_SHIM_H

/*
 * Just enough of ESP-IDF to compile main/model_bundle.c on a host.
 *
 * The point is that xmb_check runs the DEVICE'S loader, not a second
 * implementation of it written to agree with the writer. A format bug that
 * both a host reader and a host writer share is exactly the bug a round-trip
 * test is supposed to catch and would not.
 */

#include <stdio.h>

typedef int esp_err_t;

#define ESP_OK                  0
#define ESP_FAIL               -1
#define ESP_ERR_NO_MEM          0x101
#define ESP_ERR_INVALID_ARG     0x102
#define ESP_ERR_INVALID_SIZE    0x104
#define ESP_ERR_NOT_FOUND       0x105
#define ESP_ERR_INVALID_VERSION 0x10A

#define ESP_LOGE(tag, fmt, ...) fprintf(stderr, "E %s: " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) fprintf(stderr, "W %s: " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) fprintf(stderr, "I %s: " fmt "\n", tag, ##__VA_ARGS__)

#endif /* XMB_CHECK_ESP_SHIM_H */
