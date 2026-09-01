#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"

#include "display.h"
#include "http_api.h"
#include "model_store.h"
#include "net.h"
#include "settings.h"

static const char *TAG = "http";

static httpd_handle_t s_server;
static bool s_provisioning;

/* The pages, linked in as .rodata by EMBED_TXTFILES; see main/CMakeLists.txt. */
extern const char portal_html_start[] asm("_binary_portal_html_start");
extern const char portal_html_end[] asm("_binary_portal_html_end");
extern const char app_html_start[] asm("_binary_app_html_start");
extern const char app_html_end[] asm("_binary_app_html_end");

/*
 * REQUESTS ARE FORM-ENCODED, RESPONSES ARE JSON, AND THAT IS NOT AN OVERSIGHT.
 *
 * Reading JSON needs a parser; cJSON is the one to hand and it allocates per
 * document, on a heap that has 86 KB to cover WiFi, this server, and a model
 * download. Writing JSON needs snprintf. So the direction that needs a parser
 * is the one that does not use JSON: bodies arrive as
 * application/x-www-form-urlencoded, which httpd already splits and this file
 * decodes in twenty lines.
 *
 * It also happens to be what the provisioning page wants. Inside the iOS
 * captive-network sheet a plain <form method="post"> works with no JavaScript
 * at all, which is one less thing that has to survive a stripped-down WebView.
 */
#define BODY_MAX 512

/* The largest catalogue the device will relay. It is a list of names and
 * numbers, and this device is not the right place to hold a bigger one. */
#define CATALOG_MAX 4096

static int
hexval(char c)
{
    if( c >= '0' && c <= '9' )
        return c - '0';
    if( c >= 'a' && c <= 'f' )
        return c - 'a' + 10;
    if( c >= 'A' && c <= 'F' )
        return c - 'A' + 10;
    return -1;
}

/** Percent-decode in place. `+` is a space: form encoding, not URL encoding. */
static void
url_decode(char* s)
{
    char* out = s;

    for( char* in = s; *in; in++ )
    {
        if( *in == '+' )
        {
            *out++ = ' ';
        }
        else if( *in == '%' && hexval(in[1]) >= 0 && hexval(in[2]) >= 0 )
        {
            *out++ = (char)(hexval(in[1]) * 16 + hexval(in[2]));
            in += 2;
        }
        else
        {
            *out++ = *in;
        }
    }

    *out = '\0';
}

/** Read the whole body into `buf`. Returns ESP_FAIL if it does not fit. */
static esp_err_t
read_body(httpd_req_t* req, char* buf, size_t cap)
{
    size_t total = 0;

    if( req->content_len >= cap )
    {
        ESP_LOGW(TAG, "body of %u bytes is past the %u cap",
                 (unsigned)req->content_len, (unsigned)cap);
        return ESP_FAIL;
    }

    while( total < req->content_len )
    {
        int n = httpd_req_recv(req, buf + total, req->content_len - total);

        if( n == HTTPD_SOCK_ERR_TIMEOUT )
            continue;
        if( n <= 0 )
            return ESP_FAIL;

        total += (size_t)n;
    }

    buf[total] = '\0';
    return ESP_OK;
}

/** One decoded field from a form body, or "" if it was not sent. */
static void
form_field(const char* body, const char* key, char* out, size_t out_len)
{
    out[0] = '\0';

    if( httpd_query_key_value(body, key, out, out_len) != ESP_OK )
    {
        out[0] = '\0';
        return;
    }

    url_decode(out);
}

static esp_err_t
send_json(httpd_req_t* req, const char* json)
{
    httpd_resp_set_type(req, "application/json");
    /*
     * No caching, on every API response.
     *
     * Safari caches aggressively and will re-serve a status document from
     * before a change was made, which reads as the device having ignored the
     * request. The page then shows stale state until it is force-reloaded,
     * which is not a gesture a phone user has.
     */
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, json);
}

/** Copy `in` into `out` with the five characters JSON forbids escaped. */
static void
json_escape(const char* in, char* out, size_t out_len)
{
    size_t o = 0;

    for( const char* p = in; *p && o + 7 < out_len; p++ )
    {
        unsigned char c = (unsigned char)*p;

        if( c == '"' || c == '\\' )
        {
            out[o++] = '\\';
            out[o++] = (char)c;
        }
        else if( c < 0x20 )
        {
            /* An SSID is arbitrary bytes and may legitimately carry a control
             * character; unescaped it truncates the document at the phone. */
            o += (size_t)snprintf(out + o, out_len - o, "\\u%04x", c);
        }
        else
        {
            out[o++] = (char)c;
        }
    }

    out[o] = '\0';
}

/* --- handlers ----------------------------------------------------------- */

/** Copy `in` into `out` with the characters that would break out of an HTML
 *  attribute escaped. An SSID is arbitrary text and goes into one. */
static void
html_escape(const char* in, char* out, size_t out_len)
{
    size_t o = 0;

    for( const char* p = in; *p && o + 8 < out_len; p++ )
    {
        switch( *p )
        {
        case '&': o += (size_t)snprintf(out + o, out_len - o, "&amp;"); break;
        case '<': o += (size_t)snprintf(out + o, out_len - o, "&lt;"); break;
        case '>': o += (size_t)snprintf(out + o, out_len - o, "&gt;"); break;
        case '"': o += (size_t)snprintf(out + o, out_len - o, "&quot;"); break;
        case '\'': o += (size_t)snprintf(out + o, out_len - o, "&#39;"); break;
        default: out[o++] = *p; break;
        }
    }

    out[o] = '\0';
}

/*
 * THE SETUP PAGE IS RENDERED HERE, NOT FETCHED IN PIECES BY THE PHONE.
 *
 * The page used to load and then fetch /api/scan and /api/status to fill
 * itself in. Two extra requests inside the iOS captive sheet, one of which
 * used to trigger a live WiFi scan that knocked the phone off the access
 * point -- so the sheet closed, reopened, and did it again.
 *
 * Now the network list and the remembered name are substituted into the HTML
 * as it is sent. The sheet makes ONE request and needs no JavaScript at all,
 * which is the right shape for a WebView that can be dismissed at any moment.
 */
static esp_err_t
root_get(httpd_req_t* req)
{
    static const char marker[] = "<!--NETWORKS-->";
    const char* start = s_provisioning ? portal_html_start : app_html_start;
    const char* end = s_provisioning ? portal_html_end : app_html_end;
    size_t len = (size_t)(end - start - 1);
    const char* at;

    ESP_LOGI(TAG, "GET %s", req->uri);

    httpd_resp_set_type(req, "text/html");

    at = s_provisioning ? strstr(start, marker) : NULL;
    if( !at )
        return httpd_resp_send(req, start, len);

    httpd_resp_send_chunk(req, start, (ssize_t)(at - start));

    {
        struct NetScanEntry nets[16];
        int n = net_scan_results(nets, (int)(sizeof(nets) / sizeof(nets[0])));
        const char* was = settings_get()->wifi_ssid;
        bool chose = false;
        char opt[33 * 8 + 96];

        for( int i = 0; i < n; i++ )
        {
            char ssid[33 * 8];
            /* The remembered network if it is in range, otherwise the
             * strongest -- which, for an ornament sitting in the house, is
             * almost always the house. */
            bool sel = (!chose && was[0] && strcmp(was, nets[i].ssid) == 0) ||
                       (!chose && !was[0] && i == 0);

            if( sel )
                chose = true;

            html_escape(nets[i].ssid, ssid, sizeof(ssid));
            snprintf(opt, sizeof(opt), "<option value=\"%s\"%s>%s</option>", ssid,
                     sel ? " selected" : "", ssid);
            httpd_resp_send_chunk(req, opt, HTTPD_RESP_USE_STRLEN);
        }

        if( n == 0 )
            httpd_resp_send_chunk(req, "<option value=\"\">No networks found</option>",
                                  HTTPD_RESP_USE_STRLEN);
    }

    httpd_resp_send_chunk(req, at + sizeof(marker) - 1,
                          (ssize_t)(len - (size_t)(at - start) - (sizeof(marker) - 1)));
    return httpd_resp_send_chunk(req, NULL, 0);
}

/*
 * ANY unrouted request becomes a redirect to the portal, which is what a
 * captive portal is.
 *
 * iOS asks for captive.apple.com/hotspot-detect.html, Android for
 * connectivitycheck.gstatic.com/generate_204, Windows for
 * www.msftconnecttest.com/connecttest.txt, and each expects a very specific
 * answer that means "you have internet". Anything else -- including this 302
 * -- means "you are behind a portal", and the phone opens it. Listing the
 * probe URLs individually would work until one of them changed; answering
 * everything we do not recognise does not have that failure mode.
 *
 * Registered only in provisioning mode. On the home network the device is an
 * ordinary HTTP server and a 404 should be a 404.
 */
static esp_err_t
captive_redirect(httpd_req_t* req, httpd_err_code_t err)
{
    (void)err;

    /* Every probe and stray request lands here. Logged because "the phone
     * joined and then nothing happened" is otherwise indistinguishable from
     * "the phone asked for something we did not answer". */
    ESP_LOGI(TAG, "redirecting %s", req->uri);

    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t
status_get(httpd_req_t* req)
{
    const struct Settings* cfg = settings_get();
    char ip[16];
    /*
     * Six bytes per source byte, because json_escape's worst case is \u00xx
     * for every character -- an SSID is arbitrary bytes and may be all
     * control characters. The document buffer has to hold both expansions plus
     * the fixed text, or -Wformat-truncation is right that this can silently
     * emit half a JSON document.
     */
    static char ssid[SETTINGS_SSID_MAX * 6];
    static char server[SETTINGS_SERVER_MAX * 6];
    static char json[sizeof(ssid) + sizeof(server) + XMB_NAME_MAX * 6 + 256];

    net_ip_str(ip, sizeof(ip));
    json_escape(cfg->wifi_ssid, ssid, sizeof(ssid));
    json_escape(cfg->server_url, server, sizeof(server));

    char model[XMB_NAME_MAX * 6];
    char host[NET_AP_SSID_MAX];
    struct DisplayPose pose;

    json_escape(model_store_active_name(), model, sizeof(model));
    net_mdns_host(host, sizeof(host));
    display_get_pose(&pose);

    snprintf(json, sizeof(json),
             "{\"mode\":\"%s\",\"ssid\":\"%s\",\"ip\":\"%s\",\"server\":\"%s\","
             "\"brightness\":%d,\"heap\":%u,\"model\":\"%s\",\"view_arena\":%u,"
             "\"host\":\"%s\",\"alias\":%s,"
             "\"yaw\":%d,\"pitch\":%d,\"zoom\":%d,\"offy\":%d,\"spin\":%s,"
             "\"zoom_default\":%d}",
             s_provisioning ? "provisioning" : "station", ssid, ip, server,
             cfg->brightness_pct, (unsigned)esp_get_free_heap_size(), model,
             (unsigned)model_store_view_arena(), host,
             net_holds_alias() ? "true" : "false",
             pose.yaw, pose.pitch, pose.zoom, pose.offset_y,
             pose.spin ? "true" : "false", display_default_zoom());

    return send_json(req, json);
}

/*
 * The networks seen BEFORE the access point came up.
 *
 * This used to run a live scan, and that was the bug behind "the captive page
 * opens and closes": a scan takes the radio off the access point's channel for
 * a couple of seconds, the attached phone loses its beacons, and iOS closes
 * the setup sheet. It then reopens, reloads the page, scans again -- which
 * looks exactly like the device's network repeatedly dying, because it is.
 *
 * net.c scans once at boot with nobody connected. This hands back that list.
 */
static esp_err_t
scan_get(httpd_req_t* req)
{
    static struct NetScanEntry nets[16];
    static char json[16 * (33 * 6 + 32) + 32];
    size_t o = 0;
    int n = net_scan_results(nets, (int)(sizeof(nets) / sizeof(nets[0])));

    o += (size_t)snprintf(json + o, sizeof(json) - o, "[");
    for( int i = 0; i < n; i++ )
    {
        char ssid[33 * 6];

        json_escape(nets[i].ssid, ssid, sizeof(ssid));
        o += (size_t)snprintf(json + o, sizeof(json) - o, "%s{\"ssid\":\"%s\",\"rssi\":%d}",
                              i ? "," : "", ssid, nets[i].rssi);
    }
    snprintf(json + o, sizeof(json) - o, "]");

    return send_json(req, json);
}

static void
restart_task(void* arg)
{
    (void)arg;

    /*
     * A second before restarting, so the response actually reaches the phone.
     *
     * esp_restart from inside the handler kills the socket with the reply
     * still in it, and the browser shows a connection error for a request that
     * in fact succeeded -- which reads as provisioning having failed.
     */
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

static esp_err_t
provision_post(httpd_req_t* req)
{
    static char body[BODY_MAX];
    struct Settings next = *settings_get();
    char ssid[SETTINGS_SSID_MAX];
    char pass[SETTINGS_PASS_MAX];
    char server[SETTINGS_SERVER_MAX];

    ESP_LOGI(TAG, "POST /api/provision, %d bytes", (int)req->content_len);

    if( read_body(req, body, sizeof(body)) != ESP_OK )
    {
        ESP_LOGE(TAG, "could not read the form body");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body too large");
        return ESP_FAIL;
    }

    form_field(body, "ssid", ssid, sizeof(ssid));
    form_field(body, "pass", pass, sizeof(pass));
    form_field(body, "server", server, sizeof(server));

    {
        /* The dropdown lists what was in range; this is the escape hatch for a
         * hidden network or one that was not. Typed wins, and it needs no
         * JavaScript to do so -- which the captive sheet may not run. */
        char other[SETTINGS_SSID_MAX];

        form_field(body, "ssid_other", other, sizeof(other));
        if( other[0] )
            strlcpy(ssid, other, sizeof(ssid));
    }

    if( !ssid[0] )
    {
        ESP_LOGE(TAG, "form had no network name; body was \"%s\"", body);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no network name");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "form: ssid \"%s\", %d-char password, server \"%s\"", ssid,
             (int)strlen(pass), server);

    strlcpy(next.wifi_ssid, ssid, sizeof(next.wifi_ssid));
    strlcpy(next.wifi_pass, pass, sizeof(next.wifi_pass));
    next.provisioned = true;
    if( server[0] )
        strlcpy(next.server_url, server, sizeof(next.server_url));

    {
        esp_err_t err = settings_set(&next);

        if( err != ESP_OK )
        {
            ESP_LOGE(TAG, "could not save the settings: %s", esp_err_to_name(err));
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "could not save");
            return ESP_FAIL;
        }
    }

    ESP_LOGI(TAG, "provisioned for \"%s\"; restarting", next.wifi_ssid);

    /*
     * THE HAND-OFF PAGE, and what it can and cannot do.
     *
     * It cannot open the control page by itself. In a moment this access point
     * disappears, the phone falls back to the network it came from, and iOS
     * closes the captive sheet along with the network that served it. Nothing
     * here survives to run a redirect, and nothing can push a URL to a phone.
     *
     * What it CAN do is hand over an address that will work. The mDNS name
     * does not depend on DHCP, so it is known before the device has joined
     * anything -- which means this page can carry a real link to a device that
     * is not on the network yet. Tapping it once the phone is back on the home
     * network opens the control page, with no second QR to scan.
     *
     * The script tries to save even that tap: while the sheet happens to still
     * be open, it polls the name, and follows it the moment it answers. On iOS
     * that usually loses the race with the sheet closing, which is why the
     * link is the real answer and the poll is a bonus.
     */
    {
        char host[NET_AP_SSID_MAX];
        static char page[1600];

        net_mdns_host(host, sizeof(host));

        /*
         * WATCH FOR THE DISPLAY, THEN LEAVE THE CAPTIVE SHEET FOR A REAL
         * BROWSER.
         *
         * The page polls the display's own name until it answers, which is the
         * moment the phone is back on the home network and the display has
         * finished joining it. Then it escapes.
         *
         * Escaping needs a per-platform scheme, because a captive sheet is not
         * a browser and an ordinary link just navigates inside it: iOS honours
         * `x-safari-http://` to hand a URL to Safari, and Android takes an
         * `intent://` URL to the default browser. Neither is a standard, and
         * a WebView may refuse to launch another app without a tap -- so the
         * button below runs exactly the same code, and stays as the path that
         * always works.
         */
        snprintf(page, sizeof(page),
                 "<!doctype html><meta charset=utf-8>"
                 "<meta name=viewport content='width=device-width,initial-scale=1'>"
                 "<body style=\"font:17px -apple-system,sans-serif;padding:2em;"
                 "text-align:center;color-scheme:light dark\">"
                 "<h2>Joining your network</h2>"
                 "<p id=msg>Waiting for the display to come back&hellip;</p>"
                 "<p><a id=go href=\"http://%s.local/\" "
                 "style=\"display:inline-block;padding:.9em 1.4em;background:#0a7;"
                 "color:#fff;border-radius:10px;text-decoration:none;font-weight:600\">"
                 "Open %s.local</a></p>"
                 "<p style='opacity:.6'>The display also shows this address on "
                 "screen as a QR code.</p>"
                 "<script>"
                 "var h='%s',u='http://'+h+'.local/';"
                 "var g=document.getElementById('go'),m=document.getElementById('msg');"
                 "function esc(){var a=navigator.userAgent;"
                 "if(/iPhone|iPad|iPod/.test(a)){location.href='x-safari-'+u;}"
                 "else if(/Android/.test(a)){"
                 "location.href='intent://'+h+'.local/#Intent;scheme=http;end';}"
                 "else{location.href=u;}}"
                 "g.addEventListener('click',function(e){e.preventDefault();esc();});"
                 "function t(){fetch(u+'api/status',{mode:'no-cors',cache:'no-store'})"
                 ".then(function(){m.textContent='The display is back. Opening it\\u2026';"
                 "esc();})"
                 ".catch(function(){setTimeout(t,2000);});}"
                 "setTimeout(t,3000);"
                 "</script>",
                 host, host, host);

        httpd_resp_set_type(req, "text/html");
        httpd_resp_sendstr(req, page);
    }

    xTaskCreate(restart_task, "restart", 2048, NULL, 5, NULL);
    return ESP_OK;
}

static esp_err_t
config_post(httpd_req_t* req)
{
    char body[BODY_MAX];
    struct Settings next = *settings_get();
    char server[SETTINGS_SERVER_MAX];
    char brightness[8];

    if( read_body(req, body, sizeof(body)) != ESP_OK )
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body too large");
        return ESP_FAIL;
    }

    form_field(body, "server", server, sizeof(server));
    form_field(body, "brightness", brightness, sizeof(brightness));

    if( server[0] )
        strlcpy(next.server_url, server, sizeof(next.server_url));
    if( brightness[0] )
        next.brightness_pct = atoi(brightness);

    if( settings_set(&next) != ESP_OK )
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "could not save");
        return ESP_FAIL;
    }

    /* From the stored value rather than the parsed one: settings_set clamps,
     * and the panel should show what was actually kept. */
    display_set_brightness(settings_get()->brightness_pct);

    return status_get(req);
}

/*
 * THE CATALOGUE IS PROXIED, NOT FETCHED BY THE PHONE.
 *
 * The obvious design has the page fetch the asset server directly. It does not
 * work: this page is served over plain HTTP from the device, so an HTTPS
 * catalogue is a cross-origin request the server would have to opt into with
 * CORS headers, and a plain-HTTP one from an HTTPS-hosted page would be
 * blocked outright. Proxying makes every request the page issues same-origin,
 * so the asset server needs no configuration at all -- it can be a directory
 * behind any static file server.
 *
 * It also puts the reachability test in the right place. The device is what
 * has to reach the asset server in order to download a model; a page that
 * could see the catalogue from the phone would prove nothing about that.
 */
static esp_err_t
catalog_get(httpd_req_t* req)
{
    const struct Settings* cfg = settings_get();
    char url[SETTINGS_SERVER_MAX + 32];
    esp_http_client_config_t hcfg = { 0 };
    esp_http_client_handle_t client;
    char* body = NULL;
    int content_length;
    int total = 0;
    esp_err_t err;

    if( !cfg->server_url[0] )
    {
        /* Not an error: a device with no server configured has no catalogue,
         * and the page shows that as a prompt rather than as a failure. */
        return send_json(req, "{\"models\":[],\"reason\":\"no server configured\"}");
    }

    snprintf(url, sizeof(url), "%s/catalog.json", cfg->server_url);

    hcfg.url = url;
    hcfg.timeout_ms = 8000;

    client = esp_http_client_init(&hcfg);
    if( !client )
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "bad server address");
        return ESP_FAIL;
    }

    err = esp_http_client_open(client, 0);
    if( err != ESP_OK )
    {
        esp_http_client_cleanup(client);
        return send_json(req, "{\"models\":[],\"reason\":\"cannot reach the server\"}");
    }

    content_length = (int)esp_http_client_fetch_headers(client);
    if( content_length <= 0 || content_length > CATALOG_MAX )
        content_length = CATALOG_MAX;

    body = malloc((size_t)content_length + 1);
    if( !body )
    {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "out of memory");
        return ESP_FAIL;
    }

    while( total < content_length )
    {
        int n = esp_http_client_read(client, body + total, content_length - total);

        if( n <= 0 )
            break;
        total += n;
    }
    body[total] = '\0';

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if( total == 0 )
    {
        free(body);
        return send_json(req, "{\"models\":[],\"reason\":\"the server sent nothing\"}");
    }

    /* Passed through verbatim: the device does not parse the catalogue, it
     * relays it. Only the bundle URL comes back, in the request below. */
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    err = httpd_resp_send(req, body, total);
    free(body);

    return err;
}

/** Ask for a model. The render loop does the work; this only queues it. */
static esp_err_t
model_post(httpd_req_t* req)
{
    const struct Settings* cfg = settings_get();
    char body[BODY_MAX];
    char file[192];
    char url[SETTINGS_SERVER_MAX + sizeof(file) + 16];

    if( read_body(req, body, sizeof(body)) != ESP_OK )
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body too large");
        return ESP_FAIL;
    }

    form_field(body, "file", file, sizeof(file));

    if( !file[0] )
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no model named");
        return ESP_FAIL;
    }

    /*
     * The catalogue names a FILE, and the device joins it to the server it was
     * configured with. Taking a whole URL from the request would let a page --
     * or anything else that can reach this device on the LAN -- point it at an
     * arbitrary host, which is a wider door than a model picker needs.
     */
    if( strstr(file, "..") || strchr(file, ':') )
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad model name");
        return ESP_FAIL;
    }

    if( !cfg->server_url[0] )
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no server configured");
        return ESP_FAIL;
    }

    snprintf(url, sizeof(url), "%s/%s", cfg->server_url, file);

    if( model_store_request(url) != ESP_OK )
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "could not queue it");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "queued %s", url);
    return send_json(req, "{\"ok\":true}");
}

static esp_err_t
progress_get(httpd_req_t* req)
{
    struct ModelProgress p;
    char escaped[sizeof(p.error) * 6];
    char json[sizeof(escaped) + 160];
    static const char* const names[] = { "idle", "downloading", "verifying", "failed" };

    model_store_progress(&p);
    json_escape(p.error, escaped, sizeof(escaped));

    snprintf(json, sizeof(json),
             "{\"state\":\"%s\",\"received\":%d,\"total\":%d,\"error\":\"%s\"}",
             names[p.state <= MODEL_STORE_FAILED ? p.state : 0], p.received, p.total, escaped);

    return send_json(req, json);
}

/*
 * The other displays on this network.
 *
 * A browser cannot receive an mDNS advertisement -- there is no API for it in
 * any of them -- so the discovery happens here, where there is a Bonjour
 * responder that can browse, and the page renders what this device found. That
 * is the whole trick: one display's page can list every display in the house
 * because the display, not the phone, did the looking.
 *
 * It reads a table a continuous mDNS browse keeps current, so it returns
 * immediately. It used to run a query here and stall this task for over a
 * second every time the page asked.
 */
static esp_err_t
peers_get(httpd_req_t* req)
{
    static struct NetPeer peers[8];
    static char json[8 * (sizeof(peers[0]) * 2 + 48) + 32];
    size_t o = 0;
    int n;

    n = net_find_peers(peers, (int)(sizeof(peers) / sizeof(peers[0])));

    o += (size_t)snprintf(json + o, sizeof(json) - o, "[");
    for( int i = 0; i < n; i++ )
    {
        char host[sizeof(peers[0].host) * 6];
        char model[sizeof(peers[0].model) * 6];

        json_escape(peers[i].host, host, sizeof(host));
        json_escape(peers[i].model, model, sizeof(model));

        o += (size_t)snprintf(json + o, sizeof(json) - o,
                              "%s{\"host\":\"%s\",\"ip\":\"%s\",\"model\":\"%s\"}",
                              i ? "," : "", host, peers[i].ip, model);
    }
    snprintf(json + o, sizeof(json) - o, "]");

    return send_json(req, json);
}

/*
 * The camera. Written straight into the live pose the render loop reads.
 *
 * Every field is optional, so the page can send just the one a slider moved
 * rather than the whole camera on every drag -- which matters when a finger
 * on a drag pad produces ten of these a second against a server with seven
 * sockets.
 */
static esp_err_t
pose_post(httpd_req_t* req)
{
    static char body[BODY_MAX];
    struct DisplayPose pose;
    char v[16];

    if( read_body(req, body, sizeof(body)) != ESP_OK )
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body too large");
        return ESP_FAIL;
    }

    display_get_pose(&pose);

    form_field(body, "yaw", v, sizeof(v));
    if( v[0] )
        pose.yaw = atoi(v);
    form_field(body, "pitch", v, sizeof(v));
    if( v[0] )
        pose.pitch = atoi(v);
    form_field(body, "zoom", v, sizeof(v));
    if( v[0] )
        pose.zoom = atoi(v);
    form_field(body, "offy", v, sizeof(v));
    if( v[0] )
        pose.offset_y = atoi(v);
    form_field(body, "spin", v, sizeof(v));
    if( v[0] )
        pose.spin = (v[0] == '1' || v[0] == 't');

    /* Reset comes back to the framing the boot pass chose for this model,
     * which is the one camera known to fit it on the panel. */
    form_field(body, "reset", v, sizeof(v));
    if( v[0] == '1' )
    {
        pose.zoom = display_default_zoom();
        pose.yaw = 0;
        pose.pitch = 280;
        pose.offset_y = 0;
        pose.spin = true;
    }

    display_set_pose(&pose);

    /* The clamped values, not the requested ones, so a slider that asked for
     * something out of range snaps to what actually happened. */
    display_get_pose(&pose);

    {
        char json[160];

        snprintf(json, sizeof(json),
                 "{\"yaw\":%d,\"pitch\":%d,\"zoom\":%d,\"offy\":%d,\"spin\":%s}",
                 pose.yaw, pose.pitch, pose.zoom, pose.offset_y,
                 pose.spin ? "true" : "false");
        return send_json(req, json);
    }
}

static esp_err_t
forget_post(httpd_req_t* req)
{
    settings_forget_wifi();
    send_json(req, "{\"ok\":true}");
    xTaskCreate(restart_task, "restart", 2048, NULL, 5, NULL);
    return ESP_OK;
}

/* --- lifecycle ---------------------------------------------------------- */

esp_err_t
http_api_start(bool provisioning)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    if( s_server )
        return ESP_OK;

    s_provisioning = provisioning;

    /*
     * SAFARI OPENS SEVERAL CONNECTIONS AND KEEPS THEM OPEN.
     *
     * It speculatively connects for resources it has not decided to fetch and
     * holds keep-alives afterwards, so a server sized for "one phone, one
     * socket" runs out and simply stops answering -- which appears as a page
     * that half loads and then hangs, with nothing in the log.
     *
     * lru_purge_enable is the other half: without it a full socket table is a
     * hard error, and with it the oldest idle connection is dropped to make
     * room, which is exactly the right thing to do to a speculative one.
     */
    config.max_open_sockets = 7;
    config.lru_purge_enable = true;

    /*
     * More than the eight handlers the default allows.
     *
     * Nine routes are registered below, and the ninth fails -- which, through
     * the ESP_ERROR_CHECK on the registration, aborts and reboots the device
     * in a loop. The check is right to be there: a route that quietly failed
     * to register is a button in the phone app that does nothing, with the
     * reason a warning nobody reads. So the table is sized for the routes that
     * exist, with room for a few more.
     */
    config.max_uri_handlers = 16;

    /* The captive-portal 404 handler needs the wildcard matcher, and so will
     * the static asset routes when the app grows past two pages. */
    config.uri_match_fn = httpd_uri_match_wildcard;

    /*
     * 8 KB, not the 4 KB default.
     *
     * These handlers build whole documents on the stack -- a 512-byte form
     * body plus a 232-byte settings copy plus a 1,200-byte response page in
     * the provisioning handler alone, and more in the status and peer
     * handlers. At 4 KB that overflowed: the device rebooted in the middle of
     * POST /api/provision, came back up unprovisioned, and from the phone it
     * looked exactly like the button doing nothing.
     *
     * The buffers below are also `static` now, which is safe because
     * esp_http_server runs its handlers on ONE task -- but the stack is what
     * actually failed, so it is raised rather than trusted to the diet.
     */
    config.stack_size = 8192;

    if( httpd_start(&s_server, &config) != ESP_OK )
    {
        ESP_LOGE(TAG, "could not start the HTTP server");
        return ESP_FAIL;
    }

    static const httpd_uri_t routes[] = {
        { .uri = "/", .method = HTTP_GET, .handler = root_get },
        { .uri = "/api/status", .method = HTTP_GET, .handler = status_get },
        { .uri = "/api/scan", .method = HTTP_GET, .handler = scan_get },
        { .uri = "/api/provision", .method = HTTP_POST, .handler = provision_post },
        { .uri = "/api/config", .method = HTTP_POST, .handler = config_post },
        { .uri = "/api/catalog", .method = HTTP_GET, .handler = catalog_get },
        { .uri = "/api/model", .method = HTTP_POST, .handler = model_post },
        { .uri = "/api/progress", .method = HTTP_GET, .handler = progress_get },
        { .uri = "/api/peers", .method = HTTP_GET, .handler = peers_get },
        { .uri = "/api/pose", .method = HTTP_POST, .handler = pose_post },
        { .uri = "/api/forget", .method = HTTP_POST, .handler = forget_post },
    };

    for( size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++ )
        ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &routes[i]));

    if( provisioning )
        ESP_ERROR_CHECK(httpd_register_err_handler(s_server, HTTPD_404_NOT_FOUND,
                                                   captive_redirect));

    ESP_LOGI(TAG, "serving the %s page", provisioning ? "provisioning" : "control");
    net_heap_report("with the HTTP server up");

    return ESP_OK;
}
