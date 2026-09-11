#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "xmas_spatial.h"

static EventGroupHandle_t connected;
static void wifi_event(void *arg,esp_event_base_t base,int32_t id,void *data)
{
    (void)arg; (void)data;
    if (base==WIFI_EVENT && id==WIFI_EVENT_STA_START) esp_wifi_connect();
    else if (base==WIFI_EVENT && id==WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(connected,BIT0); esp_wifi_connect();
    } else if (base==IP_EVENT && id==IP_EVENT_STA_GOT_IP) xEventGroupSetBits(connected,BIT0);
}
void app_main(void)
{
    if (!strlen(CONFIG_XS_WIFI_SSID)) {
        ESP_LOGE("spatial_demo","Set Spatial layout demo Wi-Fi credentials in menuconfig"); return;
    }
    /* No automatic NVS erase: a demo should not delete other application data. */
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta=esp_netif_create_default_wifi_sta();
    connected=xEventGroupCreate();
    if (!sta || !connected) { ESP_LOGE("spatial_demo","Out of memory"); return; }
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,wifi_event,NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,wifi_event,NULL));
    wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    wifi_config_t config={0};
    if (strlen(CONFIG_XS_WIFI_SSID)>sizeof(config.sta.ssid) ||
        strlen(CONFIG_XS_WIFI_PASSWORD)>sizeof(config.sta.password)) {
        ESP_LOGE("spatial_demo","Wi-Fi credentials exceed ESP-IDF field lengths"); return;
    }
    memcpy(config.sta.ssid,CONFIG_XS_WIFI_SSID,strlen(CONFIG_XS_WIFI_SSID));
    memcpy(config.sta.password,CONFIG_XS_WIFI_PASSWORD,strlen(CONFIG_XS_WIFI_PASSWORD));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA,&config));
    ESP_ERROR_CHECK(esp_wifi_start());
    xEventGroupWaitBits(connected,BIT0,pdFALSE,pdTRUE,portMAX_DELAY);
    xs_service_config_t options=xs_service_defaults(sta);
    options.cluster.capacity=CONFIG_XS_MAX_BOARDS; options.cluster.group=CONFIG_XS_GROUP;
    xs_node_t *nodes=calloc(options.cluster.capacity,sizeof(*nodes));
    if (!nodes) { ESP_LOGE("spatial_demo","Out of memory for layout"); return; }
    xs_service_t *service;
    ESP_ERROR_CHECK(xs_service_start(&options,&service));
    uint32_t printed=0;
    for (;;) {
        xs_info_t info;
        esp_err_t result=xs_service_copy(service,nodes,options.cluster.capacity,&info);
        if (result==ESP_OK && info.version!=printed) {
            printed=info.version;
            ESP_LOGI("spatial_demo","Layout %"PRIu32": %u nodes; rank=%u RMS=%.3fm max=%.3fm converged=%d",
                info.version,(unsigned)info.count,info.quality.rank,(double)info.quality.rms_m,
                (double)info.quality.max_residual_m,info.quality.converged);
            for (size_t i=0;i<info.count;i++)
                ESP_LOGI("spatial_demo",MACSTR" x=%.3f y=%.3f z=%.3f m",MAC2STR(nodes[i].id.mac),
                    (double)nodes[i].position.x,(double)nodes[i].position.y,(double)nodes[i].position.z);
        } else if (result!=ESP_OK) {
            ESP_LOGI("spatial_demo","State=%d discovered=%u pairs=%u/%u coordinator=%d",info.state,
                (unsigned)info.discovered,(unsigned)info.quality.measured_pairs,
                (unsigned)info.quality.required_pairs,info.coordinator);
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
