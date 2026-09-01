#ifndef SETTINGS_H
#define SETTINGS_H

/*
 * Everything the device remembers across a power cycle, in NVS.
 *
 * WiFi credentials are kept HERE rather than left to esp_wifi's own NVS
 * storage. esp_wifi will happily persist them for us, but then "is this device
 * provisioned?" is a question only esp_wifi can answer, and it answers it by
 * trying to connect -- which is a several-second boot delay before we can
 * decide whether to show a provisioning QR. Owning the record makes that
 * decision free and makes "forget the network" one erase we control.
 */

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#define SETTINGS_SSID_MAX     33  /* 32 + NUL */
#define SETTINGS_PASS_MAX     65  /* 64 + NUL */
#define SETTINGS_SERVER_MAX  129

struct Settings
{
    /*
     * Whether the credentials below are meant to be used.
     *
     * Separate from the SSID being non-empty, because forgetting a network
     * KEEPS THE NAME and drops only the password. Someone who mistyped a
     * password should not have to pick their network out of a scan list again,
     * and someone re-provisioning in the same house is almost always naming
     * the same network -- so the name survives as a hint for the setup form
     * while this flag is what decides whether to try connecting.
     */
    bool provisioned;

    char wifi_ssid[SETTINGS_SSID_MAX];
    char wifi_pass[SETTINGS_PASS_MAX];

    /*
     * Base URL of the host that serves the model catalogue and bundles, with
     * no trailing slash -- "http://192.168.1.10:8080" or
     * "https://models.example.com". Empty until configured, which is not an
     * error: the device renders its built-in model perfectly well and only
     * needs this to be told about others.
     */
    char server_url[SETTINGS_SERVER_MAX];

    /** Backlight duty, 0-100. */
    int brightness_pct;
};

/** Open NVS and read the stored settings, or defaults if there are none. */
esp_err_t settings_init(void);

/** The live settings. Never NULL after settings_init. */
const struct Settings* settings_get(void);

/** Replace the settings and write them through to NVS. */
esp_err_t settings_set(const struct Settings* s);

/** Whether a network has been configured AND not forgotten; false means
 *  provisioning. Not the same as `wifi_ssid` being set -- see `provisioned`. */
bool settings_have_wifi(void);

/** Forget the network: drops the password and clears `provisioned`, but keeps
 *  the SSID so the setup form can offer it back. */
esp_err_t settings_forget_wifi(void);

#endif /* SETTINGS_H */
