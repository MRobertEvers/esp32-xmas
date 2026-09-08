#include "xmas_spatial.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/inet.h"

static const char *TAG="xmas_spatial";
typedef struct { uint32_t token; bool success; float distance; } radio_result_t;
struct xs_service {
    xs_service_config_t cfg;
    xs_cluster_t *cluster;
    int socket;
    SemaphoreHandle_t lock,done;
    QueueHandle_t reports;
    TaskHandle_t task;
    esp_event_handler_instance_t handler;
    bool handler_registered,mode_changed,ps_changed;
    wifi_ps_type_t previous_ps;
    portMUX_TYPE radio_lock;
    bool radio_busy,stop,online;
    uint32_t radio_token;
    uint8_t radio_peer[6],channel;
    esp_netif_ip_info_t ip;
};
static uint64_t now_ms(void) { return (uint64_t)esp_timer_get_time()/1000; }
static uint32_t nonce(void) { uint32_t n; do { n=esp_random(); } while (!n); return n; }
static void udp_send(void *context,uint32_t ip,const uint8_t *data,size_t length)
{
    xs_service_t *s=context;
    if (!s->online) return;
    struct sockaddr_in address={.sin_family=AF_INET,.sin_port=htons(s->cfg.port)};
    address.sin_addr.s_addr=ip ? ip : (s->ip.ip.addr | ~s->ip.netmask.addr);
    /* UDP is intentionally nonblocking; retries live in the portable protocol.
     * The directed broadcast follows the STA subnet route, not a SoftAP route. */
    (void)sendto(s->socket,data,length,0,(struct sockaddr *)&address,sizeof(address));
}
static bool start_range(void *context,const uint8_t peer[6],uint8_t channel,uint32_t token)
{
    xs_service_t *s=context;
    portENTER_CRITICAL(&s->radio_lock);
    if (s->radio_busy) { portEXIT_CRITICAL(&s->radio_lock); return false; }
    s->radio_busy=true; s->radio_token=token; memcpy(s->radio_peer,peer,6);
    portEXIT_CRITICAL(&s->radio_lock);
    wifi_ftm_initiator_cfg_t cfg={.channel=channel,.frm_count=16,.burst_period=2,.use_get_report_api=true};
    memcpy(cfg.resp_mac,peer,6);
    esp_err_t err=esp_wifi_ftm_initiate_session(&cfg);
    if (err!=ESP_OK) {
        portENTER_CRITICAL(&s->radio_lock); s->radio_busy=false; portEXIT_CRITICAL(&s->radio_lock);
        ESP_LOGD(TAG,"FTM start failed: %s",esp_err_to_name(err)); return false;
    }
    return true;
}
static void cancel_range(void *context)
{
    xs_service_t *s=context;
    portENTER_CRITICAL(&s->radio_lock); bool busy=s->radio_busy; portEXIT_CRITICAL(&s->radio_lock);
    if (busy) (void)esp_wifi_ftm_end_session();
    /* Do NOT release radio_busy until its terminal event is consumed. IDF's
     * FTM event has a peer MAC but no user token: starting another session to
     * the same peer early could attribute the old result to the new request.
     * If a driver never delivers termination, fail closed instead of inventing
     * ranges. Stop/restart the service/Wi-Fi to recover such a driver fault. */
}
static void ftm_event(void *context,esp_event_base_t base,int32_t id,void *data)
{
    (void)base; (void)id;
    xs_service_t *s=context; wifi_event_ftm_report_t *event=data;
    radio_result_t result={0}; bool ours;
    portENTER_CRITICAL(&s->radio_lock);
    ours=s->radio_busy && !memcmp(s->radio_peer,event->peer_mac,6);
    result.token=s->radio_token;
    portEXIT_CRITICAL(&s->radio_lock);
    /* IDF 5.1+: get_report(NULL,0) releases the driver's retained report. The
     * distance estimate is in cm (NOT the raw RTT field's nanoseconds). A
     * complete FTM burst contributes one sample to the median/MAD filter. */
    (void)esp_wifi_ftm_get_report(NULL,0);
    if (!ours) return;
    result.success=event->status==FTM_STATUS_SUCCESS && event->ftm_report_num_entries>=4 &&
        event->dist_est>0 && event->dist_est<=1000000;
    result.distance=event->dist_est*0.01f;
    ESP_LOGD(TAG,"FTM token=%lu status=%u samples=%u distance=%.2fm",
        (unsigned long)result.token,(unsigned)event->status,event->ftm_report_num_entries,(double)result.distance);
    if (xQueueSend(s->reports,&result,0)!=pdTRUE)
        ESP_LOGE(TAG,"FTM event queue full; ranging remains blocked until restart");
}
static void update_link(xs_service_t *s,uint64_t now)
{
    wifi_ap_record_t ap; esp_netif_ip_info_t ip;
    bool online=esp_wifi_sta_get_ap_info(&ap)==ESP_OK &&
        esp_netif_get_ip_info(s->cfg.station,&ip)==ESP_OK && ip.ip.addr!=0;
    if (!online) {
        if (s->online) xs_cluster_rejoin(s->cluster,nonce(),s->channel,now);
        s->online=false; return;
    }
    if (!s->online || ip.ip.addr!=s->ip.ip.addr || ip.netmask.addr!=s->ip.netmask.addr || ap.primary!=s->channel) {
        s->ip=ip; s->channel=ap.primary; s->online=true;
        xs_cluster_rejoin(s->cluster,nonce(),ap.primary,now);
        ESP_LOGI(TAG,"Discovery active on channel %u, UDP port %u",s->channel,s->cfg.port);
    }
}
static void worker(void *arg)
{
    xs_service_t *s=arg; uint64_t checked=0; bool checked_once=false;
    for (;;) {
        portENTER_CRITICAL(&s->radio_lock); bool stop=s->stop; portEXIT_CRITICAL(&s->radio_lock);
        if (stop) break;
        uint64_t now=now_ms();
        xSemaphoreTake(s->lock,portMAX_DELAY);
        if (!checked_once || now-checked>=1000) { update_link(s,now); checked=now; checked_once=true; }
        radio_result_t r;
        while (xQueueReceive(s->reports,&r,0)==pdTRUE) {
            portENTER_CRITICAL(&s->radio_lock);
            bool current=s->radio_busy && s->radio_token==r.token;
            if (current) s->radio_busy=false;
            portEXIT_CRITICAL(&s->radio_lock);
            if (current) xs_cluster_range_result(s->cluster,r.token,r.success,r.distance,now);
        }
        /* Bound work per loop: a noisy LAN must not starve expiry/timers. An
         * extra byte lets the parser reject oversized/truncated UDP datagrams. */
        for (unsigned i=0;i<64;i++) {
            uint8_t packet[97]; struct sockaddr_in from; socklen_t len=sizeof(from);
            int got=recvfrom(s->socket,packet,sizeof(packet),0,(struct sockaddr *)&from,&len);
            if (got<0) break;
            if (s->online && from.sin_family==AF_INET && from.sin_port==htons(s->cfg.port) &&
                (from.sin_addr.s_addr&s->ip.netmask.addr)==(s->ip.ip.addr&s->ip.netmask.addr))
                xs_cluster_receive(s->cluster,from.sin_addr.s_addr,packet,(size_t)got,now);
        }
        if (s->online) xs_cluster_tick(s->cluster,now);
        xSemaphoreGive(s->lock);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    cancel_range(s);
    /* Keep the event handler alive long enough to free the final driver report.
     * This also prevents an old callback from retaining a pointer to a service
     * that the application has already destroyed. No worker task is killed in
     * the middle of a solve/socket operation. */
    portENTER_CRITICAL(&s->radio_lock); bool busy=s->radio_busy; portEXIT_CRITICAL(&s->radio_lock);
    if (busy) {
        radio_result_t terminal;
        if (xQueueReceive(s->reports,&terminal,pdMS_TO_TICKS(s->cfg.cluster.range_timeout_ms))==pdTRUE) {
            portENTER_CRITICAL(&s->radio_lock); s->radio_busy=false; portEXIT_CRITICAL(&s->radio_lock);
        } else ESP_LOGW(TAG,"Driver did not report FTM termination before shutdown");
    }
    xSemaphoreGive(s->done);
    vTaskDelete(NULL);
}
xs_service_config_t xs_service_defaults(esp_netif_t *station)
{
    return (xs_service_config_t){xs_cluster_defaults(),station,42173,8192,2};
}
esp_err_t xs_service_start(const xs_service_config_t *cfg,xs_service_t **out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    *out=NULL;
#if !CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT || !CONFIG_ESP_WIFI_FTM_RESPONDER_SUPPORT
    (void)cfg;
    ESP_LOGE(TAG,"Enable both ESP_WIFI_FTM_INITIATOR_SUPPORT and ESP_WIFI_FTM_RESPONDER_SUPPORT");
    return ESP_ERR_NOT_SUPPORTED;
#endif
    if (!cfg || !cfg->station || !cfg->port || cfg->task_stack_bytes<4096 || cfg->task_priority>=configMAX_PRIORITIES)
        return ESP_ERR_INVALID_ARG;
    wifi_mode_t mode; wifi_ap_record_t upstream;
    if (esp_wifi_get_mode(&mode)!=ESP_OK || mode!=WIFI_MODE_STA || esp_wifi_sta_get_ap_info(&upstream)!=ESP_OK)
        return ESP_ERR_INVALID_STATE;
    xs_service_t *s=calloc(1,sizeof(*s)); if (!s) return ESP_ERR_NO_MEM;
    s->socket=-1; s->cfg=*cfg; s->channel=upstream.primary;
    s->radio_lock=(portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    s->lock=xSemaphoreCreateMutex(); s->done=xSemaphoreCreateBinary();
    s->reports=xQueueCreate(4,sizeof(radio_result_t));
    esp_err_t err=ESP_ERR_NO_MEM;
    if (!s->lock || !s->done || !s->reports) goto fail;
    xs_id_t self; uint8_t ap[6];
    err=esp_wifi_get_mac(WIFI_IF_STA,self.mac); if (err!=ESP_OK) goto fail;
    err=esp_wifi_get_mac(WIFI_IF_AP,ap); if (err!=ESP_OK) goto fail;
    xs_transport_t transport={udp_send,start_range,cancel_range,s};
    s->cluster=xs_cluster_create(&cfg->cluster,&transport,self,nonce(),ap,upstream.primary,now_ms());
    if (!s->cluster) { err=ESP_ERR_NO_MEM; goto fail; }
    err=esp_wifi_get_ps(&s->previous_ps); if (err!=ESP_OK) goto fail;
    err=esp_wifi_set_mode(WIFI_MODE_APSTA); if (err!=ESP_OK) goto fail; s->mode_changed=true;
    wifi_config_t responder={0};
    snprintf((char *)responder.ap.ssid,sizeof(responder.ap.ssid),"XMAS-FTM-%02x%02x%02x%02x%02x%02x",
        self.mac[0],self.mac[1],self.mac[2],self.mac[3],self.mac[4],self.mac[5]);
    snprintf((char *)responder.ap.password,sizeof(responder.ap.password),"%08lx%08lx",
        (unsigned long)nonce(),(unsigned long)nonce());
    responder.ap.ssid_len=(uint8_t)strlen((char *)responder.ap.ssid);
    responder.ap.channel=upstream.primary; responder.ap.ssid_hidden=1;
    responder.ap.max_connection=1; responder.ap.authmode=WIFI_AUTH_WPA2_PSK;
    responder.ap.ftm_responder=true;
    err=esp_wifi_set_config(WIFI_IF_AP,&responder); if (err!=ESP_OK) goto fail;
    err=esp_wifi_set_ps(WIFI_PS_NONE); if (err!=ESP_OK) goto fail; s->ps_changed=true;
    err=esp_event_handler_instance_register(WIFI_EVENT,WIFI_EVENT_FTM_REPORT,ftm_event,s,&s->handler);
    if (err!=ESP_OK) goto fail;
    s->handler_registered=true;
    s->socket=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    if (s->socket<0) { err=ESP_FAIL; goto fail; }
    int yes=1;
    if (setsockopt(s->socket,SOL_SOCKET,SO_BROADCAST,&yes,sizeof(yes))<0 ||
        fcntl(s->socket,F_SETFL,O_NONBLOCK)<0) { err=ESP_FAIL; goto fail; }
    struct sockaddr_in bind_address={.sin_family=AF_INET,.sin_port=htons(cfg->port),.sin_addr={.s_addr=INADDR_ANY}};
    if (bind(s->socket,(struct sockaddr *)&bind_address,sizeof(bind_address))<0) { err=ESP_FAIL; goto fail; }
    if (xTaskCreate(worker,"xmas_spatial",cfg->task_stack_bytes,s,cfg->task_priority,&s->task)!=pdPASS) {
        s->task=NULL; err=ESP_ERR_NO_MEM; goto fail;
    }
    *out=s; return ESP_OK;
fail:
    xs_service_stop(s); return err;
}
esp_err_t xs_service_copy(xs_service_t *s,xs_node_t *nodes,size_t capacity,xs_info_t *info)
{
    if (!s || !info || (!nodes && capacity)) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s->lock,portMAX_DELAY);
    size_t n=xs_cluster_copy(s->cluster,nodes,capacity,info,now_ms());
    bool online=s->online;
    if (!online) { info->valid=false; info->count=0; info->state=XS_WAITING; }
    xSemaphoreGive(s->lock);
    if (!online || !n) return ESP_ERR_INVALID_STATE;
    return capacity<n ? ESP_ERR_INVALID_SIZE : ESP_OK;
}
void xs_service_stop(xs_service_t *s)
{
    if (!s) return;
    if (s->task) {
        portENTER_CRITICAL(&s->radio_lock); s->stop=true; portEXIT_CRITICAL(&s->radio_lock);
        xSemaphoreTake(s->done,portMAX_DELAY);
    }
    if (s->handler_registered) esp_event_handler_instance_unregister(WIFI_EVENT,WIFI_EVENT_FTM_REPORT,s->handler);
    xs_cluster_destroy(s->cluster);
    if (s->socket>=0) close(s->socket);
    if (s->mode_changed) (void)esp_wifi_set_mode(WIFI_MODE_STA);
    if (s->ps_changed) (void)esp_wifi_set_ps(s->previous_ps);
    if (s->reports) vQueueDelete(s->reports);
    if (s->lock) vSemaphoreDelete(s->lock);
    if (s->done) vSemaphoreDelete(s->done);
    free(s);
}
