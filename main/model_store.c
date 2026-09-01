#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "mbedtls/sha256.h"
#include "nvs.h"

#include "model_store.h"

static const char *TAG = "models";

#define NVS_NAMESPACE "xmas"
#define NVS_KEY_SLOT  "model_slot"

/* 4 KB, which is the flash sector: the erase granularity, so writing in
 * anything smaller means read-modify-write for no benefit. It is also a size
 * the heap can spare while WiFi is running. */
#define CHUNK 4096

static const esp_partition_t* s_slot[2];
static int s_active = -1;
static size_t s_view_arena_bytes;

static esp_partition_mmap_handle_t s_map;
static bool s_mapped;
static struct ModelBundle s_bundle;
static bool s_have_bundle;

static SemaphoreHandle_t s_lock;
static char s_pending_url[256];
static bool s_pending;
static struct ModelProgress s_progress;

static void
progress_set(enum ModelStoreState state, int received, int total, const char* error)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_progress.state = state;
    s_progress.received = received;
    s_progress.total = total;
    if( error )
        strlcpy(s_progress.error, error, sizeof(s_progress.error));
    else if( state != MODEL_STORE_FAILED )
        s_progress.error[0] = '\0';
    xSemaphoreGive(s_lock);
}

void
model_store_progress(struct ModelProgress* out)
{
    if( !out )
        return;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_progress;
    xSemaphoreGive(s_lock);
}

static int
stored_slot(void)
{
    nvs_handle_t h;
    int32_t v = 0;

    if( nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK )
        return -1;

    if( nvs_get_i32(h, NVS_KEY_SLOT, &v) != ESP_OK )
        v = -1;
    nvs_close(h);

    return (v == 0 || v == 1) ? (int)v : -1;
}

static esp_err_t
store_slot(int slot)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);

    if( err != ESP_OK )
        return err;

    err = nvs_set_i32(h, NVS_KEY_SLOT, (int32_t)slot);
    if( err == ESP_OK )
        err = nvs_commit(h);
    nvs_close(h);

    return err;
}

/** Map a slot and open the bundle in it. */
static esp_err_t
activate(int slot)
{
    const void* mapped = NULL;
    esp_err_t err;

    if( slot < 0 || slot > 1 || !s_slot[slot] )
        return ESP_ERR_INVALID_ARG;

    err = esp_partition_mmap(s_slot[slot], 0, s_slot[slot]->size, ESP_PARTITION_MMAP_DATA,
                             &mapped, &s_map);
    if( err != ESP_OK )
    {
        ESP_LOGE(TAG, "cannot map slot %d: %s", slot, esp_err_to_name(err));
        return err;
    }
    s_mapped = true;

    err = model_bundle_open(mapped, s_slot[slot]->size, &s_bundle);
    if( err != ESP_OK )
    {
        esp_partition_munmap(s_map);
        s_mapped = false;
        return err;
    }

    /*
     * The arena check again, at load rather than only at download.
     *
     * A slot can hold a bundle this firmware cannot draw without a download
     * having just happened: the firmware may have been reflashed with a
     * smaller arena since. Refusing here falls back to the built-in model,
     * which is a device that works, rather than overrunning the arena.
     */
    if( model_bundle_view_bytes(s_bundle.header) > s_view_arena_bytes )
    {
        ESP_LOGE(TAG, "slot %d wants a %u byte view; this build has %u", slot,
                 (unsigned)model_bundle_view_bytes(s_bundle.header),
                 (unsigned)s_view_arena_bytes);
        model_bundle_close(&s_bundle);
        esp_partition_munmap(s_map);
        s_mapped = false;
        return ESP_ERR_INVALID_SIZE;
    }

    s_active = slot;
    s_have_bundle = true;
    return ESP_OK;
}

esp_err_t
model_store_init(size_t view_arena_bytes)
{
    int slot;

    s_view_arena_bytes = view_arena_bytes;
    s_lock = xSemaphoreCreateMutex();
    if( !s_lock )
        return ESP_ERR_NO_MEM;

    s_slot[0] = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "mdl_a");
    s_slot[1] = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "mdl_b");

    if( !s_slot[0] || !s_slot[1] )
    {
        ESP_LOGW(TAG, "no model slots in the partition table; the built-in model is all "
                      "this device can show");
        return ESP_ERR_NOT_FOUND;
    }

    slot = stored_slot();
    if( slot < 0 )
    {
        ESP_LOGI(TAG, "no downloaded model yet");
        return ESP_ERR_NOT_FOUND;
    }

    if( activate(slot) != ESP_OK )
    {
        ESP_LOGW(TAG, "slot %d did not load; falling back to the built-in model", slot);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "showing the model in slot %d", slot);
    return ESP_OK;
}

size_t
model_store_view_arena(void)
{
    return s_view_arena_bytes;
}

const char*
model_store_active_name(void)
{
    return s_have_bundle ? s_bundle.header->name : "";
}

const struct ModelBundle*
model_store_active(void)
{
    return s_have_bundle ? &s_bundle : NULL;
}

esp_err_t
model_store_request(const char* url)
{
    if( !url || !url[0] )
        return ESP_ERR_INVALID_ARG;

    if( strlen(url) >= sizeof(s_pending_url) )
        return ESP_ERR_INVALID_SIZE;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    strlcpy(s_pending_url, url, sizeof(s_pending_url));
    s_pending = true;
    s_progress.state = MODEL_STORE_DOWNLOADING;
    s_progress.received = 0;
    s_progress.total = 0;
    s_progress.error[0] = '\0';
    xSemaphoreGive(s_lock);

    return ESP_OK;
}

bool
model_store_take_request(char* url, size_t url_len)
{
    bool had;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    had = s_pending;
    if( had )
        strlcpy(url, s_pending_url, url_len);
    s_pending = false;
    xSemaphoreGive(s_lock);

    return had;
}

/** Hash what actually landed in the slot, which is the only copy that matters. */
static esp_err_t
verify_slot(const esp_partition_t* part, const struct XmbHeader* h, uint8_t* scratch)
{
    mbedtls_sha256_context ctx;
    uint8_t digest[32];
    size_t remaining;
    size_t offset = sizeof(*h);

    if( h->total_size <= sizeof(*h) )
        return ESP_ERR_INVALID_SIZE;

    remaining = h->total_size - sizeof(*h);

    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);

    while( remaining > 0 )
    {
        size_t take = remaining > CHUNK ? CHUNK : remaining;
        esp_err_t err = esp_partition_read(part, offset, scratch, take);

        if( err != ESP_OK )
        {
            mbedtls_sha256_free(&ctx);
            return err;
        }

        mbedtls_sha256_update(&ctx, scratch, take);
        offset += take;
        remaining -= take;
    }

    mbedtls_sha256_finish(&ctx, digest);
    mbedtls_sha256_free(&ctx);

    if( memcmp(digest, h->sha256, sizeof(digest)) != 0 )
    {
        ESP_LOGE(TAG, "the bundle in flash does not match its own hash");
        return ESP_ERR_INVALID_CRC;
    }

    return ESP_OK;
}

esp_err_t
model_store_apply(const char* url)
{
    int target_slot = (s_active == 0) ? 1 : 0;
    const esp_partition_t* part = s_slot[target_slot];
    esp_http_client_config_t cfg = { 0 };
    esp_http_client_handle_t client = NULL;
    uint8_t* chunk = NULL;
    struct XmbHeader header;
    size_t written = 0;
    size_t erased = 0;
    int content_length;
    int status;
    esp_err_t err = ESP_FAIL;
    const char* why = "download failed";

    if( !part )
    {
        progress_set(MODEL_STORE_FAILED, 0, 0, "no model slots on this device");
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "downloading %s into slot %d", url, target_slot);

    chunk = malloc(CHUNK);
    if( !chunk )
    {
        progress_set(MODEL_STORE_FAILED, 0, 0, "out of memory");
        return ESP_ERR_NO_MEM;
    }

    cfg.url = url;
    cfg.timeout_ms = 15000;
    /* Follow redirects: an asset server behind a tidy URL is the normal case,
     * and a 302 that is not followed looks to the user like a missing model. */
    cfg.disable_auto_redirect = false;

    client = esp_http_client_init(&cfg);
    if( !client )
    {
        why = "bad server address";
        goto done;
    }

    err = esp_http_client_open(client, 0);
    if( err != ESP_OK )
    {
        why = "cannot reach the server";
        goto done;
    }

    content_length = (int)esp_http_client_fetch_headers(client);
    status = esp_http_client_get_status_code(client);

    if( status != 200 )
    {
        ESP_LOGE(TAG, "server answered %d", status);
        snprintf(s_progress.error, sizeof(s_progress.error), "server said %d", status);
        why = NULL;
        err = ESP_FAIL;
        goto done;
    }

    if( content_length > 0 && (size_t)content_length > part->size )
    {
        why = "model is too big for this device";
        err = ESP_ERR_INVALID_SIZE;
        goto done;
    }

    progress_set(MODEL_STORE_DOWNLOADING, 0, content_length > 0 ? content_length : 0, NULL);

    /*
     * ERASE AS WE GO, a sector ahead of the write.
     *
     * Erasing the whole 1 MB slot up front is about seven seconds of the panel
     * frozen before a single byte arrives, and it throws away the current
     * contents even when the download turns out to fail in its first packet.
     * Erasing only what is about to be written costs the same total time,
     * spread out, and leaves a failed download having damaged only as much as
     * it actually reached.
     */
    while( true )
    {
        int n = esp_http_client_read(client, (char*)chunk, CHUNK);

        if( n < 0 )
        {
            why = "the connection dropped";
            err = ESP_FAIL;
            goto done;
        }
        if( n == 0 )
            break;

        if( written + (size_t)n > part->size )
        {
            why = "model is too big for this device";
            err = ESP_ERR_INVALID_SIZE;
            goto done;
        }

        while( erased < written + (size_t)n )
        {
            err = esp_partition_erase_range(part, erased, CHUNK);
            if( err != ESP_OK )
            {
                why = "flash erase failed";
                goto done;
            }
            erased += CHUNK;
        }

        err = esp_partition_write(part, written, chunk, (size_t)n);
        if( err != ESP_OK )
        {
            why = "flash write failed";
            goto done;
        }

        if( written == 0 && (size_t)n >= sizeof(header) )
        {
            /*
             * Check the magic on the FIRST chunk, before spending a minute
             * writing something that was never a bundle. An asset server that
             * answers 200 with an HTML error page is the common case here.
             */
            memcpy(&header, chunk, sizeof(header));
            if( header.magic != XMB_MAGIC || header.version != XMB_VERSION )
            {
                why = "that file is not a model bundle";
                err = ESP_ERR_INVALID_ARG;
                goto done;
            }
            /*
             * Computed here, not read from the header.
             *
             * The header's number was worked out by the baker on another
             * machine with a different kernel lane, so it can be tens of
             * kilobytes under what this build will consume. Trusting it is how
             * a model gets accepted, written, and then overruns the arena on
             * the next boot.
             */
            if( model_bundle_view_bytes(&header) > s_view_arena_bytes )
            {
                ESP_LOGE(TAG, "bundle wants a %u byte view; this device has %u",
                         (unsigned)model_bundle_view_bytes(&header),
                         (unsigned)s_view_arena_bytes);
                why = "model is too complex for this device";
                err = ESP_ERR_INVALID_SIZE;
                goto done;
            }
        }

        written += (size_t)n;
        progress_set(MODEL_STORE_DOWNLOADING, (int)written,
                     content_length > 0 ? content_length : 0, NULL);
    }

    if( written < sizeof(header) )
    {
        why = "the server sent nothing";
        err = ESP_ERR_INVALID_SIZE;
        goto done;
    }

    if( header.total_size != written )
    {
        ESP_LOGE(TAG, "bundle declares %u bytes, %u arrived", (unsigned)header.total_size,
                 (unsigned)written);
        why = "the download was cut short";
        err = ESP_ERR_INVALID_SIZE;
        goto done;
    }

    progress_set(MODEL_STORE_VERIFYING, (int)written, (int)written, NULL);

    err = verify_slot(part, &header, chunk);
    if( err != ESP_OK )
    {
        why = "the download was corrupted";
        goto done;
    }

    err = store_slot(target_slot);
    if( err != ESP_OK )
    {
        why = "could not record the new model";
        goto done;
    }

    ESP_LOGI(TAG, "slot %d verified and made active; restarting", target_slot);

    free(chunk);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    /* The swap is a restart: see model_store.h for why nothing here tries to
     * exchange one live bundle for another. */
    vTaskDelay(pdMS_TO_TICKS(250));
    esp_restart();
    return ESP_OK;

done:
    if( why )
    {
        ESP_LOGE(TAG, "%s", why);
        progress_set(MODEL_STORE_FAILED, (int)written, 0, why);
    }
    else
    {
        progress_set(MODEL_STORE_FAILED, (int)written, 0, s_progress.error);
    }

    free(chunk);
    if( client )
    {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
    }

    return err == ESP_OK ? ESP_FAIL : err;
}
