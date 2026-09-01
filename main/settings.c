#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "settings.h"

static const char *TAG = "settings";

/* One namespace, one blob. The alternative -- a key per field -- means a
 * partially written record after a power cut at the wrong moment, and there is
 * nothing here big enough to be worth that. */
#define NVS_NAMESPACE "xmas"
#define NVS_KEY       "settings"

#define DEFAULT_BRIGHTNESS_PCT 35

static struct Settings s_settings;

esp_err_t
settings_init(void)
{
    nvs_handle_t h;
    esp_err_t err;
    size_t len = sizeof(s_settings);

    memset(&s_settings, 0, sizeof(s_settings));
    s_settings.brightness_pct = DEFAULT_BRIGHTNESS_PCT;

    err = nvs_flash_init();
    if( err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND )
    {
        /* A partition from an older layout, or one that filled up. Erasing it
         * loses the settings, which is recoverable; refusing to boot is not. */
        ESP_LOGW(TAG, "nvs partition unusable (%s), erasing", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if( err != ESP_OK )
        return err;

    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if( err == ESP_ERR_NVS_NOT_FOUND )
    {
        ESP_LOGI(TAG, "no stored settings; starting from defaults");
        return ESP_OK;
    }
    if( err != ESP_OK )
        return err;

    err = nvs_get_blob(h, NVS_KEY, &s_settings, &len);
    nvs_close(h);

    if( err == ESP_ERR_NVS_NOT_FOUND )
    {
        /* The namespace exists -- something else put a key in it -- but this
         * device has never been configured. Not a warning, and reported apart
         * from the length check below: nvs_get_blob leaves `len` untouched when
         * the key is absent, so sharing that branch printed "stored settings
         * are 232 bytes, expected 232" for a record that was simply not there.
         */
        ESP_LOGI(TAG, "no stored settings; starting from defaults");
        return ESP_OK;
    }

    if( len != sizeof(s_settings) )
    {
        /*
         * A blob of the wrong length is a record written by a different build
         * of this struct. Reading it into the current one would put old bytes
         * in new fields, so it is discarded rather than migrated -- there is
         * no version of these settings worth a migration path.
         */
        ESP_LOGW(TAG, "stored settings are %u bytes, expected %u; using defaults",
                 (unsigned)len, (unsigned)sizeof(s_settings));
        memset(&s_settings, 0, sizeof(s_settings));
        s_settings.brightness_pct = DEFAULT_BRIGHTNESS_PCT;
        return ESP_OK;
    }
    if( err != ESP_OK )
        return err;

    ESP_LOGI(TAG, "loaded settings: ssid \"%s\", server \"%s\"",
             s_settings.wifi_ssid, s_settings.server_url);
    return ESP_OK;
}

const struct Settings*
settings_get(void)
{
    return &s_settings;
}

esp_err_t
settings_set(const struct Settings* s)
{
    nvs_handle_t h;
    esp_err_t err;

    if( !s )
        return ESP_ERR_INVALID_ARG;

    /* Copy first, so a caller passing settings_get() back in (the usual shape
     * of "change one field") is not reading a buffer we are writing. */
    struct Settings next = *s;

    /* The strings come from an HTTP request body; a missing terminator here is
     * an overrun in every ESP_LOGI that prints one. */
    next.wifi_ssid[sizeof(next.wifi_ssid) - 1] = '\0';
    next.wifi_pass[sizeof(next.wifi_pass) - 1] = '\0';
    next.server_url[sizeof(next.server_url) - 1] = '\0';

    if( next.brightness_pct < 0 )
        next.brightness_pct = 0;
    if( next.brightness_pct > 100 )
        next.brightness_pct = 100;

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if( err != ESP_OK )
        return err;

    err = nvs_set_blob(h, NVS_KEY, &next, sizeof(next));
    if( err == ESP_OK )
        err = nvs_commit(h);
    nvs_close(h);

    if( err == ESP_OK )
        s_settings = next;

    return err;
}

bool
settings_have_wifi(void)
{
    /* Not just "is there an SSID": forgetting a network keeps the name as a
     * hint for the setup form and clears this. See settings.h. */
    return s_settings.provisioned && s_settings.wifi_ssid[0] != '\0';
}

esp_err_t
settings_forget_wifi(void)
{
    struct Settings next = s_settings;

    /* The SSID stays. See the note on `provisioned` in settings.h: it becomes
     * a hint for the setup form rather than something to connect with. */
    memset(next.wifi_pass, 0, sizeof(next.wifi_pass));
    next.provisioned = false;

    return settings_set(&next);
}
