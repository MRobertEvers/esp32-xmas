#ifndef XMAS_SPATIAL_H
#define XMAS_SPATIAL_H
#include "esp_err.h"
#include "esp_netif.h"
#include "xmas_spatial_cluster.h"

#ifdef __cplusplus
extern "C" {
#endif
typedef struct xs_service xs_service_t;
typedef struct {
    xs_cluster_config_t cluster;
    esp_netif_t *station;        /* initialized STA netif with a DHCP/static IPv4 */
    uint16_t port;              /* identical on every board, default 42173 */
    unsigned task_stack_bytes;  /* default 8192; allocations for matrices use heap */
    unsigned task_priority;     /* default 2, below latency-sensitive rendering */
} xs_service_config_t;
xs_service_config_t xs_service_defaults(esp_netif_t *station);
/* Wi-Fi must already be initialized, started, and connected in WIFI_MODE_STA.
 * Owns the FTM engine and SoftAP until stopped; switches radio to APSTA, adds a
 * password-protected responder-only AP (no AP IP netif/DHCP service), disables
 * modem sleep. Does not own upstream credentials, STA reconnects, NVS, or the
 * default event loop. Configure WIFI_STORAGE_RAM before calling if desired.
 * Requires CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT and ...RESPONDER_SUPPORT.
 * Same IPv4 broadcast domain, client isolation off, same 2.4 GHz channel.
 * No third-party dependencies: SDK Wi-Fi/FreeRTOS/lwIP plus C runtime only. */
esp_err_t xs_service_start(const xs_service_config_t *config,xs_service_t **out);
/* Serialized internally. ESP_ERR_INVALID_SIZE means capacity too small; info
 * contains the required count. ESP_ERR_INVALID_STATE means no fresh map yet.
 * A successful map can still be inaccurate: inspect info.quality, especially
 * rank and residuals, and compare repeated scans under real tree conditions. */
esp_err_t xs_service_copy(xs_service_t *service,xs_node_t *nodes,size_t capacity,xs_info_t *info);
/* Stop from an application task, never an event callback. Restores STA mode
 * and the prior power-save setting; keeps the application's Wi-Fi connection. */
void xs_service_stop(xs_service_t *service);
#ifdef __cplusplus
}
#endif
#endif
