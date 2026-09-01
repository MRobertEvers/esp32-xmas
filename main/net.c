#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "mdns.h"

#include "net.h"

static const char *TAG = "net";

#define AP_CHANNEL          6
#define AP_MAX_CONNECTIONS  2

#define BIT_GOT_IP      BIT0
#define BIT_STA_FAILED  BIT1

/*
 * The scan taken before the access point came up.
 *
 * Sixteen networks: more than a picker shows before someone stops scrolling,
 * and a fixed array rather than a growable one because it is filled once, at
 * boot, and never again. See scan_into_cache for why it is not refreshed while
 * a phone is connected.
 */
#define SCAN_CACHE_MAX 16

static struct NetScanEntry s_scan[SCAN_CACHE_MAX];
static int s_scan_count;

static EventGroupHandle_t s_events;
static esp_netif_t *s_netif_sta;
static esp_netif_t *s_netif_ap;
static esp_ip4_addr_t s_ip;
static int s_sta_retries;

/*
 * How many times a failed association is retried before net_start_sta gives
 * up and the caller falls back to provisioning.
 *
 * A wrong password and a router that is briefly busy look identical from here
 * -- both arrive as WIFI_EVENT_STA_DISCONNECTED -- so the only way to tell
 * them apart is to try again a few times. Retrying forever is the failure mode
 * that matters: it leaves a device that was given the wrong password sitting
 * on a blank screen with no way to be told the right one.
 */
#define STA_MAX_RETRIES 5

void
net_heap_report(const char* when)
{
    ESP_LOGI(TAG, "DRAM %s: %u free, %u largest block",
             when,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
}

void
net_ap_credentials(char* ssid, size_t ssid_len, char* pass, size_t pass_len)
{
    uint8_t mac[6] = { 0 };

    /*
     * ESP_MAC_WIFI_SOFTAP, not the station MAC: this is the name the AP will
     * actually broadcast, and the two differ by one in the last byte. A QR
     * naming the station's MAC sends the phone looking for a network that does
     * not exist.
     */
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);

    if( ssid && ssid_len )
        snprintf(ssid, ssid_len, "XMAS-%02X%02X", mac[4], mac[5]);

    if( pass && pass_len )
    {
        /*
         * Eight characters from the MAC, in an alphabet chosen for two
         * separate reasons.
         *
         * It excludes 0/O and 1/I/L so a password read off a 240-pixel panel
         * and typed by hand is not ambiguous -- the QR is the fast path, not
         * the only one. And it excludes the five characters a `WIFI:` QR
         * payload has to escape (backslash, semicolon, comma, colon, quote),
         * so the payload can be built by concatenation.
         *
         * Derived rather than random so it survives an NVS erase: a password
         * printed on the back of a device should not stop being true.
         */
        static const char alphabet[] = "23456789ABCDEFGHJKMNPQRSTUVWXYZ";
        const uint32_t n = (uint32_t)(sizeof(alphabet) - 1);
        uint32_t a = ((uint32_t)mac[0] << 16) | ((uint32_t)mac[1] << 8) | mac[2];
        uint32_t b = ((uint32_t)mac[3] << 16) | ((uint32_t)mac[4] << 8) | mac[5];
        size_t i;

        for( i = 0; i + 1 < pass_len && i < 8; i++ )
        {
            uint32_t v = (i < 4) ? a : b;
            pass[i] = alphabet[(v >> (6 * (i % 4))) % n];
        }
        pass[i] = '\0';
    }
}

static void
on_wifi_event(void* arg, esp_event_base_t base, int32_t id, void* data)
{
    (void)arg;
    (void)base;

    switch( id )
    {
    case WIFI_EVENT_STA_START:
        esp_wifi_connect();
        break;

    case WIFI_EVENT_STA_DISCONNECTED:
    {
        const wifi_event_sta_disconnected_t* d = data;

        if( s_sta_retries < STA_MAX_RETRIES )
        {
            s_sta_retries++;
            ESP_LOGW(TAG, "disconnected (reason %d), retry %d of %d",
                     d ? d->reason : -1, s_sta_retries, STA_MAX_RETRIES);
            esp_wifi_connect();
        }
        else
        {
            ESP_LOGE(TAG, "giving up after %d attempts (reason %d)",
                     s_sta_retries, d ? d->reason : -1);
            xEventGroupSetBits(s_events, BIT_STA_FAILED);
        }
        break;
    }

    case WIFI_EVENT_AP_STACONNECTED:
        ESP_LOGI(TAG, "a phone joined the provisioning AP");
        break;

    default:
        break;
    }
}

static void
on_ip_event(void* arg, esp_event_base_t base, int32_t id, void* data)
{
    (void)arg;
    (void)base;

    if( id == IP_EVENT_STA_GOT_IP )
    {
        const ip_event_got_ip_t* e = data;

        s_ip = e->ip_info.ip;
        s_sta_retries = 0;
        ESP_LOGI(TAG, "got address " IPSTR, IP2STR(&s_ip));
        xEventGroupSetBits(s_events, BIT_GOT_IP);
    }
    else if( id == IP_EVENT_STA_LOST_IP )
    {
        s_ip.addr = 0;
        xEventGroupClearBits(s_events, BIT_GOT_IP);
    }
}

esp_err_t
net_init(void)
{
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    s_events = xEventGroupCreate();
    if( !s_events )
        return ESP_ERR_NO_MEM;

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    s_netif_sta = esp_netif_create_default_wifi_sta();
    s_netif_ap = esp_netif_create_default_wifi_ap();

    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /*
     * WIFI_STORAGE_RAM: settings.c owns the credentials.
     *
     * The default writes every config we set into esp_wifi's own NVS entries,
     * which then win at the next boot over whatever we pass to
     * esp_wifi_set_config. Two records of the same fact, and the one we cannot
     * see is the one that decides -- so "forget this network" appears to work
     * and the device rejoins the old one anyway.
     */
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, ESP_EVENT_ANY_ID, on_ip_event, NULL, NULL));

    return ESP_OK;
}

/**
 * Scan once, into the cache, with the radio to ourselves.
 *
 * Runs in station mode before the access point exists, so there is no client
 * to disturb -- see the note on net_scan_results. Costs about two seconds of
 * boot, spent while the panel is still showing its splash.
 */
static void
scan_into_cache(void)
{
    static wifi_ap_record_t records[SCAN_CACHE_MAX * 2];
    wifi_scan_config_t scan = { .show_hidden = false };
    uint16_t count = (uint16_t)(sizeof(records) / sizeof(records[0]));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    if( esp_wifi_scan_start(&scan, true) != ESP_OK )
    {
        ESP_LOGW(TAG, "scan failed; the setup page will offer a text field");
        esp_wifi_stop();
        return;
    }

    esp_wifi_scan_get_ap_records(&count, records);

    /* Strongest first: the setup form pre-selects the top entry, and the
     * loudest network reaching an ornament in the house is almost always the
     * house. esp_wifi promises no order. */
    for( uint16_t i = 1; i < count; i++ )
    {
        wifi_ap_record_t key = records[i];
        int j = (int)i - 1;

        while( j >= 0 && records[j].rssi < key.rssi )
        {
            records[j + 1] = records[j];
            j--;
        }
        records[j + 1] = key;
    }

    s_scan_count = 0;
    for( uint16_t i = 0; i < count && s_scan_count < SCAN_CACHE_MAX; i++ )
    {
        bool seen = false;

        if( records[i].ssid[0] == '\0' )
            continue;

        /* A mesh, or one router publishing 2.4 and 5 GHz under a single name,
         * answers several times. A picker listing "Home" four times looks
         * broken and is a coin toss to pre-select from. */
        for( int k = 0; k < s_scan_count; k++ )
            if( strcmp(s_scan[k].ssid, (const char*)records[i].ssid) == 0 )
                seen = true;
        if( seen )
            continue;

        strlcpy(s_scan[s_scan_count].ssid, (const char*)records[i].ssid,
                sizeof(s_scan[0].ssid));
        s_scan[s_scan_count].rssi = records[i].rssi;
        s_scan_count++;
    }

    esp_wifi_stop();
    ESP_LOGI(TAG, "scanned %d network%s before starting the AP", s_scan_count,
             s_scan_count == 1 ? "" : "s");
}

int
net_scan_results(struct NetScanEntry* out, int max)
{
    int n = s_scan_count < max ? s_scan_count : max;

    if( !out || max <= 0 )
        return 0;

    memcpy(out, s_scan, (size_t)n * sizeof(*out));
    return n;
}

esp_err_t
net_start_ap(void)
{
    wifi_config_t cfg = { 0 };
    char ssid[NET_AP_SSID_MAX];
    char pass[NET_AP_PASS_MAX];

    scan_into_cache();

    net_ap_credentials(ssid, sizeof(ssid), pass, sizeof(pass));

    strlcpy((char*)cfg.ap.ssid, ssid, sizeof(cfg.ap.ssid));
    strlcpy((char*)cfg.ap.password, pass, sizeof(cfg.ap.password));
    cfg.ap.ssid_len = strlen(ssid);
    cfg.ap.channel = AP_CHANNEL;
    cfg.ap.max_connection = AP_MAX_CONNECTIONS;

    /*
     * WPA2_PSK, and not one of the mixed modes.
     *
     * iOS 16 and later flag WPA and TKIP as weak security, which turns joining
     * this network into a warning the user has to argue past -- on a flow whose
     * whole point is that scanning a QR just works. WPA2-only costs nothing:
     * anything with a camera capable of scanning the QR supports it.
     */
    cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    cfg.ap.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "provisioning AP \"%s\" up, password \"%s\"", ssid, pass);
    net_heap_report("with the AP up");

    return ESP_OK;
}

esp_err_t
net_start_sta(const char* ssid, const char* pass, uint32_t timeout_ms)
{
    wifi_config_t cfg = { 0 };
    EventBits_t bits;

    if( !ssid || !ssid[0] )
        return ESP_ERR_INVALID_ARG;

    strlcpy((char*)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid));
    strlcpy((char*)cfg.sta.password, pass ? pass : "", sizeof(cfg.sta.password));

    /*
     * No authmode threshold.
     *
     * The default (WPA2) refuses to associate with an open or WEP network,
     * which is a posture this device is in no position to take: it joins the
     * network it was told to join, and refusing looks to the user exactly like
     * a wrong password.
     */
    cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;

    s_sta_retries = 0;
    xEventGroupClearBits(s_events, BIT_GOT_IP | BIT_STA_FAILED);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "joining \"%s\"", ssid);

    bits = xEventGroupWaitBits(s_events, BIT_GOT_IP | BIT_STA_FAILED, pdFALSE, pdFALSE,
                               pdMS_TO_TICKS(timeout_ms));

    if( bits & BIT_GOT_IP )
    {
        net_heap_report("with the station up");
        return ESP_OK;
    }

    ESP_LOGE(TAG, "could not join \"%s\" (%s)", ssid,
             (bits & BIT_STA_FAILED) ? "rejected" : "timed out");
    esp_wifi_stop();
    return ESP_FAIL;
}

void
net_mdns_host(char* out, size_t out_len)
{
    uint8_t mac[6] = { 0 };

    /*
     * The same suffix as the access point, from the same MAC, so a device that
     * announced itself as XMAS-8959 during setup answers to xmas-8959.local
     * afterwards. Two different names for one ornament is a way to lose it on
     * a network.
     */
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(out, out_len, "xmas-%02x%02x", mac[4], mac[5]);
}

esp_err_t
net_start_mdns(char* out_host, size_t out_len)
{
    char host[NET_AP_SSID_MAX];
    esp_err_t err;

    net_mdns_host(host, sizeof(host));

    err = mdns_init();
    if( err != ESP_OK )
    {
        ESP_LOGW(TAG, "mDNS did not start (%s); the address QR still works",
                 esp_err_to_name(err));
        return err;
    }

    mdns_hostname_set(host);
    mdns_instance_name_set("OSRS model display");

    /*
     * TWO SERVICES, and the second is the one that matters here.
     *
     * `_http._tcp` is the polite advertisement: it is what a generic Bonjour
     * browser shows, and what makes the display appear alongside printers in
     * whatever discovery app someone happens to have.
     *
     * `_xmasdisp._tcp` is ours. Browsing for _http._tcp on a home network
     * returns routers, NAS boxes and printers, and a page that offered to jump
     * to a printer would be a page nobody trusts. A private service type means
     * a query returns displays and nothing else.
     */
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    mdns_service_add(NULL, "_xmasdisp", "_tcp", 80, NULL, 0);
    mdns_service_txt_item_set("_xmasdisp", "_tcp", "model", "");

    if( out_host && out_len )
        strlcpy(out_host, host, out_len);

    ESP_LOGI(TAG, "advertising as %s.local", host);
    return ESP_OK;
}

void
net_mdns_set_model(const char* model)
{
    /* Advertised so a peer list can say what each display is showing without
     * opening a connection to every one of them. */
    mdns_service_txt_item_set("_xmasdisp", "_tcp", "model", model ? model : "");
}

/* --- who else is out there ---------------------------------------------- */

/*
 * A LIVE TABLE, FED BY mDNS, RATHER THAN A POLL.
 *
 * This used to run mdns_query_ptr every thirty seconds. That works -- an mDNS
 * query IS a UDP multicast to 224.0.0.251, so the wire traffic was already
 * what a hand-rolled announce protocol would send -- but it is a poll, and it
 * has the two faults every poll has: it costs a query and N answers on a timer
 * whether or not anything changed, and it is stale by up to its own period. A
 * display unplugged from the tree kept its place in the list for half a minute.
 *
 * mdns_browse_new subscribes instead. The mDNS responder already receives the
 * unsolicited announcements a device sends when it appears, and the goodbye
 * packet (ttl 0) it sends when it leaves; this just asks to be told. No timer,
 * no query, and departures arrive as events rather than as a timeout.
 *
 * A raw UDP broadcast protocol was the alternative and would be a second
 * discovery system for the same facts. It is also not better supported:
 * broadcast wakes every device on the network, so access points rate-limit and
 * suppress it, while link-local multicast is what every phone, printer and
 * speaker on the network already depends on. And the decisive part -- this
 * device needs mDNS multicast working REGARDLESS, because that is what makes
 * `xmas.local` resolve. If it were blocked, displays could still agree on a
 * name over broadcast, and nothing could look it up.
 */
#define PEERS_MAX 8

static struct NetPeer s_peers[PEERS_MAX];
static bool s_peer_used[PEERS_MAX];
static SemaphoreHandle_t s_peer_lock;
static TaskHandle_t s_alias_task;
static mdns_browse_t* s_browse;

/** Copy one browse result into the table, or drop it if this is a goodbye. */
static void
peer_record(mdns_result_t* r)
{
    int slot = -1;
    int free_slot = -1;

    if( !r->hostname )
        return;

    xSemaphoreTake(s_peer_lock, portMAX_DELAY);

    for( int i = 0; i < PEERS_MAX; i++ )
    {
        if( s_peer_used[i] && strcmp(s_peers[i].host, r->hostname) == 0 )
            slot = i;
        else if( !s_peer_used[i] && free_slot < 0 )
            free_slot = i;
    }

    /* ttl 0 is a goodbye: the display was unplugged, or is restarting to swap
     * its model. Either way it is gone now rather than in thirty seconds. */
    if( r->ttl == 0 )
    {
        if( slot >= 0 )
            s_peer_used[slot] = false;
        xSemaphoreGive(s_peer_lock);
        return;
    }

    if( slot < 0 )
        slot = free_slot;
    if( slot < 0 )
    {
        xSemaphoreGive(s_peer_lock);
        return;
    }

    memset(&s_peers[slot], 0, sizeof(s_peers[slot]));
    strlcpy(s_peers[slot].host, r->hostname, sizeof(s_peers[slot].host));

    for( mdns_ip_addr_t* a = r->addr; a; a = a->next )
    {
        if( a->addr.type == ESP_IPADDR_TYPE_V4 )
        {
            snprintf(s_peers[slot].ip, sizeof(s_peers[slot].ip), IPSTR,
                     IP2STR(&a->addr.u_addr.ip4));
            break;
        }
    }

    for( size_t i = 0; i < r->txt_count; i++ )
        if( r->txt[i].key && strcmp(r->txt[i].key, "model") == 0 && r->txt[i].value )
            strlcpy(s_peers[slot].model, r->txt[i].value, sizeof(s_peers[slot].model));

    s_peer_used[slot] = true;
    xSemaphoreGive(s_peer_lock);
}

/**
 * Called by the mDNS task when a browse result changes.
 *
 * Two constraints from the component, and both shape this function: it runs
 * holding the mDNS service lock, so it must not call any mDNS API -- which
 * includes the delegate-hostname calls the election makes -- and `result`
 * stops being valid when it returns, so anything kept has to be copied. So
 * this copies into the table and wakes the task that is allowed to act.
 *
 * Only `result` is examined. Its `next` links other cached instances rather
 * than other things that changed, so walking it would re-process peers that
 * did not.
 */
static void
browse_notify(mdns_result_t* result)
{
    if( !result )
        return;

    peer_record(result);

    if( s_alias_task )
        xTaskNotifyGive(s_alias_task);
}

int
net_find_peers(struct NetPeer* out, int max)
{
    int n = 0;

    if( !out || max <= 0 || !s_peer_lock )
        return 0;

    /* A table read, so this no longer blocks its caller for a query timeout.
     * /api/peers used to stall the HTTP task for over a second. */
    xSemaphoreTake(s_peer_lock, portMAX_DELAY);
    for( int i = 0; i < PEERS_MAX && n < max; i++ )
        if( s_peer_used[i] )
            out[n++] = s_peers[i];
    xSemaphoreGive(s_peer_lock);

    return n;
}

/** The shared name, without the `.local`. */
#define ALIAS_HOST "xmas"

static bool s_holds_alias;

bool
net_holds_alias(void)
{
    return s_holds_alias;
}

/**
 * Decide whether this device should answer to `xmas.local`, and take or
 * release the name accordingly.
 *
 * The rule is "lowest hostname wins", which needs no negotiation: every device
 * sees the same set and applies the same comparison, so they agree without
 * talking to each other about it. Names come from MAC addresses, so there are
 * no ties.
 *
 * Runs on its own task, NOT in the browse callback, because the callback holds
 * the mDNS service lock and the delegate-hostname calls below take it.
 */
static void
alias_update(void)
{
    struct NetPeer peers[PEERS_MAX];
    char self[NET_AP_SSID_MAX];
    bool lowest = true;
    int n;

    net_mdns_host(self, sizeof(self));
    n = net_find_peers(peers, (int)(sizeof(peers) / sizeof(peers[0])));

    for( int i = 0; i < n; i++ )
        if( strcmp(peers[i].host, self) < 0 )
            lowest = false;

    if( lowest && !s_holds_alias )
    {
        mdns_ip_addr_t addr;

        memset(&addr, 0, sizeof(addr));
        addr.addr.type = ESP_IPADDR_TYPE_V4;
        addr.addr.u_addr.ip4.addr = s_ip.addr;
        addr.next = NULL;

        if( mdns_delegate_hostname_add(ALIAS_HOST, &addr) == ESP_OK )
        {
            s_holds_alias = true;
            ESP_LOGI(TAG, "answering to %s.local as well (%d other display%s)", ALIAS_HOST,
                     n, n == 1 ? "" : "s");
        }
    }
    else if( !lowest && s_holds_alias )
    {
        /* Someone lower turned up. Step aside rather than let two devices
         * answer one name, which is the fight this exists to avoid. */
        mdns_delegate_hostname_remove(ALIAS_HOST);
        s_holds_alias = false;
        ESP_LOGI(TAG, "handing %s.local to a lower-numbered display", ALIAS_HOST);
    }
}

static void
alias_task(void* arg)
{
    (void)arg;

    /*
     * Waits to be told, rather than waking on a timer.
     *
     * The browse notifies this task whenever a display appears or says
     * goodbye, so the election runs when the answer might have changed and
     * not otherwise. The timeout is a backstop, not the mechanism: it covers
     * a first election on a network where this is the only display and
     * nothing will ever notify, and re-asserts the name if a delegated record
     * were ever lost.
     *
     * The first pass is late on purpose. A device that elects itself the
     * instant it boots claims the name before anyone else has answered, so two
     * displays powered on together would both take it.
     */
    vTaskDelay(pdMS_TO_TICKS(4000));

    while( true )
    {
        alias_update();
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(60000));
    }
}

esp_err_t
net_start_alias_election(void)
{
    s_peer_lock = xSemaphoreCreateMutex();
    if( !s_peer_lock )
        return ESP_ERR_NO_MEM;

    /* 3 KB: this task compares strings and calls two mDNS functions. */
    if( xTaskCreate(alias_task, "xmas_alias", 3072, NULL, 3, &s_alias_task) != pdPASS )
        return ESP_ERR_NO_MEM;

    s_browse = mdns_browse_new("_xmasdisp", "_tcp", browse_notify);
    if( !s_browse )
    {
        ESP_LOGW(TAG, "could not browse for other displays; %s.local will still work here",
                 ALIAS_HOST);
        return ESP_FAIL;
    }

    return ESP_OK;
}

bool
net_find_model_server(char* out, size_t out_len, uint32_t timeout_ms)
{
    mdns_result_t* results = NULL;
    bool found = false;

    if( !out || !out_len )
        return false;

    if( mdns_query_ptr("_xmasmodels", "_tcp", timeout_ms, 4, &results) != ESP_OK )
        return false;

    for( mdns_result_t* r = results; r && !found; r = r->next )
    {
        for( mdns_ip_addr_t* a = r->addr; a; a = a->next )
        {
            if( a->addr.type != ESP_IPADDR_TYPE_V4 )
                continue;

            snprintf(out, out_len, "http://" IPSTR ":%u", IP2STR(&a->addr.u_addr.ip4),
                     (unsigned)r->port);
            found = true;
            break;
        }
    }

    mdns_query_results_free(results);

    if( found )
        ESP_LOGI(TAG, "found a model server at %s", out);

    return found;
}


bool
net_have_ip(void)
{
    return s_ip.addr != 0;
}

void
net_ip_str(char* out, size_t out_len)
{
    if( !out || !out_len )
        return;

    snprintf(out, out_len, IPSTR, IP2STR(&s_ip));
}
