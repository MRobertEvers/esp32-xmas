#ifndef CAPTIVE_DNS_H
#define CAPTIVE_DNS_H

/*
 * The wildcard DNS responder that makes a phone open the provisioning page on
 * its own. See captive_dns.c for why a captive portal needs one.
 *
 * Started only while the provisioning access point is up.
 */

#include <stdint.h>
#include "esp_err.h"

/** Answer every A query with `answer_ip`, in network byte order. Idempotent. */
esp_err_t captive_dns_start(uint32_t answer_ip);

#endif /* CAPTIVE_DNS_H */
