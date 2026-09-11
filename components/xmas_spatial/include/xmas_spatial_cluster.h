#ifndef XMAS_SPATIAL_CLUSTER_H
#define XMAS_SPATIAL_CLUSTER_H
#include "xmas_spatial_math.h"

#ifdef __cplusplus
extern "C" {
#endif
/* Platform-independent discovery/coordinator state machine. Calls must be
 * serialized. Useful with the ESP-IDF adapter or a simulated lossy network. */
typedef struct xs_cluster xs_cluster_t;
typedef enum {
    XS_WAITING, XS_SETTLING, XS_COLLECTING, XS_LAYOUT, XS_CAPACITY, XS_BAD_GEOMETRY
} xs_state_t;
typedef struct { xs_id_t id; xs_vec3_t position; } xs_node_t;
typedef struct {
    xs_state_t state;
    size_t discovered, count;
    bool coordinator, valid;
    uint32_t version;
    uint64_t age_ms;
    xs_quality_t quality;
} xs_info_t;
typedef struct {
    size_t capacity;             /* >=4, <=65535, subject to available memory */
    uint32_t group;              /* same on all boards; isolation, NOT authentication */
    uint32_t hello_ms, peer_timeout_ms, settle_ms;
    uint32_t range_timeout_ms, retry_ms, publish_ms, layout_ttl_ms;
    xs_options_t solver;
} xs_cluster_config_t;
typedef struct {
    /* ipv4 uses the transport's representation; 0 means LAN broadcast. */
    void (*send)(void *context,uint32_t ipv4,const uint8_t *data,size_t length);
    bool (*range)(void *context,const uint8_t ap_mac[6],uint8_t channel,uint32_t token);
    void (*cancel)(void *context);
    void *context;
} xs_transport_t;

xs_cluster_config_t xs_cluster_defaults(void);
xs_cluster_t *xs_cluster_create(const xs_cluster_config_t *config,
    const xs_transport_t *transport,xs_id_t self,uint32_t boot_nonce,
    const uint8_t ap_mac[6],uint8_t channel,uint64_t now_ms);
void xs_cluster_destroy(xs_cluster_t *cluster);
void xs_cluster_tick(xs_cluster_t *cluster,uint64_t now_ms);
void xs_cluster_receive(xs_cluster_t *cluster,uint32_t source_ipv4,
                        const uint8_t *data,size_t length,uint64_t now_ms);
void xs_cluster_range_result(xs_cluster_t *cluster,uint32_t token,
                              bool success,float distance_m,uint64_t now_ms);
/* On Wi-Fi IP/channel/reconnection changes, discard the old membership/map.
 * boot_nonce MUST be freshly generated, nonzero; invalidates delayed packets. */
void xs_cluster_rejoin(xs_cluster_t *cluster,uint32_t boot_nonce,
                       uint8_t channel,uint64_t now_ms);
/* Returns required number of output entries (0 when no valid layout).
 * Copies all or none; info is always written. A map is valid only while fresh
 * and membership is unchanged; rank/error remain caller-visible qualifications. */
size_t xs_cluster_copy(xs_cluster_t *cluster,xs_node_t *nodes,size_t capacity,
                        xs_info_t *info,uint64_t now_ms);
#ifdef __cplusplus
}
#endif
#endif
