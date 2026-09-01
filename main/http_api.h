#ifndef HTTP_API_H
#define HTTP_API_H

/*
 * The device's own web server: the provisioning form on the access point, and
 * the control page on the home network.
 *
 * ONE ORIGIN, PLAIN HTTP, SERVED FROM THE DEVICE. Safari enforces mixed-content
 * blocking with no override, so a control page hosted anywhere over HTTPS
 * could never call this device's API -- the browser blocks the request before
 * it is sent. Serving the page from the device makes every request same-origin
 * and sidesteps both that and CORS, and it keeps the control page working when
 * the asset server is unreachable, which is the moment it is most needed.
 */

#include <stdbool.h>
#include "esp_err.h"

/**
 * Start the server. `provisioning` selects which page `/` serves and whether
 * unrouted requests are redirected as a captive portal.
 */
esp_err_t http_api_start(bool provisioning);

#endif /* HTTP_API_H */
