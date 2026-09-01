/*
 * A DNS server that answers every question with our own address.
 *
 * THIS IS WHAT MAKES THE PORTAL APPEAR BY ITSELF. A phone that joins a network
 * immediately fetches a known URL to find out whether it has working internet
 * -- iOS asks for captive.apple.com/hotspot-detect.html and expects a page
 * whose body is exactly "Success". If that lookup fails, the phone decides the
 * network is broken and offers to leave it. If it resolves and returns
 * something else, the phone decides it is behind a captive portal and opens
 * that page in a browser sheet, unprompted, which is the entire user-facing
 * flow this device depends on.
 *
 * So the name has to resolve, to us, whatever it was. Hence a wildcard
 * responder rather than a real resolver: there is nothing to look anything up
 * in, and every question has the same answer.
 *
 * Runs only while the provisioning AP is up. On a home network this would be
 * a hostile thing to run, and it is never started there.
 */

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lwip/sockets.h"

#include "captive_dns.h"

static const char *TAG = "captive_dns";

#define DNS_PORT 53
/* A query for a name longer than this is not one a phone's captivity probe
 * asks, and answering it is not worth the buffer. */
#define DNS_MAX_PACKET 320

struct dns_header
{
    uint16_t id;
    uint16_t flags;
    uint16_t questions;
    uint16_t answers;
    uint16_t authorities;
    uint16_t additional;
};

static TaskHandle_t s_task;
static uint32_t s_answer_ip;

/**
 * Build the reply in place: the query is echoed back with the response bit
 * set, followed by one answer record pointing at us.
 *
 * The answer's NAME is the 0xC00C compression pointer -- "the name at offset
 * 12", which is where the question's name starts in every well-formed query.
 * That is two bytes instead of a copy of the name and is what every real
 * server emits.
 */
static int
build_reply(uint8_t* buf, int len, int cap)
{
    struct dns_header* h = (struct dns_header*)buf;
    int qname_end;
    uint8_t* a;

    if( len < (int)sizeof(*h) )
        return 0;

    /* A response, authoritative, no error. Recursion-available is set because
     * the phone asked for recursion and a resolver that declines it invites a
     * retry against a server that is not here. */
    h->flags = htons(0x8580);
    h->answers = htons(1);
    h->authorities = 0;
    h->additional = 0;

    /* Walk the question's labels to find where the type and class sit. */
    qname_end = sizeof(*h);
    while( qname_end < len && buf[qname_end] != 0 )
    {
        int label = buf[qname_end];

        /* A compression pointer in a QUESTION is malformed, and following one
         * here would be a loop with attacker-controlled offsets. */
        if( label & 0xC0 )
            return 0;

        qname_end += label + 1;
    }
    qname_end += 1; /* the root label */

    /* The question's 16-bit type and class. */
    if( qname_end + 4 > len )
        return 0;

    /*
     * Only A records get an answer. A phone also asks for AAAA, and answering
     * one with an A record is a malformed reply -- the right response is the
     * same header with no answers, which tells it not to wait for one.
     */
    if( !(buf[qname_end] == 0 && buf[qname_end + 1] == 1) )
    {
        h->answers = 0;
        return qname_end + 4;
    }

    len = qname_end + 4;
    if( len + 16 > cap )
        return 0;

    a = buf + len;
    a[0] = 0xC0;                 /* name: pointer to offset 12 */
    a[1] = 0x0C;
    a[2] = 0x00; a[3] = 0x01;    /* type A */
    a[4] = 0x00; a[5] = 0x01;    /* class IN */
    /* TTL of zero. The phone must not cache this: the moment the device is
     * provisioned the AP disappears, and a cached wildcard would send every
     * lookup on that phone to an address that is gone. */
    a[6] = 0; a[7] = 0; a[8] = 0; a[9] = 0;
    a[10] = 0x00; a[11] = 0x04;  /* four bytes of address */
    memcpy(a + 12, &s_answer_ip, 4);

    return len + 16;
}

static void
dns_task(void* arg)
{
    uint8_t buf[DNS_MAX_PACKET];
    struct sockaddr_in bind_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(DNS_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    int sock;

    (void)arg;

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if( sock < 0 )
    {
        ESP_LOGE(TAG, "no socket (errno %d)", errno);
        s_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    if( bind(sock, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) < 0 )
    {
        ESP_LOGE(TAG, "cannot bind port %d (errno %d)", DNS_PORT, errno);
        close(sock);
        s_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "answering every name with the portal address");

    while( true )
    {
        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);
        int len = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr*)&from, &from_len);
        int reply;

        if( len <= 0 )
            continue;

        reply = build_reply(buf, len, sizeof(buf));
        if( reply > 0 )
            sendto(sock, buf, reply, 0, (struct sockaddr*)&from, from_len);
    }
}

esp_err_t
captive_dns_start(uint32_t answer_ip)
{
    if( s_task )
        return ESP_OK;

    s_answer_ip = answer_ip;

    /* 3 KB: this task owns one small buffer and calls nothing deep. The
     * default 4 KB would be 1 KB of a heap that has 86 to cover WiFi, the HTTP
     * server and everything the model download needs. */
    if( xTaskCreate(dns_task, "captive_dns", 3072, NULL, 4, &s_task) != pdPASS )
    {
        s_task = NULL;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}
