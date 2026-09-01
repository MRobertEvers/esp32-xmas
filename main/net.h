#ifndef NET_H
#define NET_H

/*
 * WiFi, in the two modes this device has.
 *
 * UNPROVISIONED it is an access point with a name and password derived from
 * its own MAC, which the panel shows as a QR code the phone's camera can join
 * from directly. PROVISIONED it is a station on the user's network, and the
 * panel shows a QR of its own address so the phone can reach the control page
 * without knowing anything about mDNS.
 *
 * The two are never both wanted: APSTA costs a second netif and the AP keeps
 * broadcasting an open door long after it is needed, so provisioning switches
 * the interface over rather than adding to it.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/** SSID of the provisioning AP, "XMAS-A1B2". Stable for a given board. */
#define NET_AP_SSID_MAX  16
/** Password of the provisioning AP. WPA2 needs at least 8 characters. */
#define NET_AP_PASS_MAX  16

/** NVS, the event loop, the network interfaces and esp_wifi, but no radio. */
esp_err_t net_init(void);

/** One network seen in the scan taken before the access point came up. */
struct NetScanEntry
{
    char ssid[33];
    int rssi;
};

/**
 * The cached scan, strongest first, one entry per name.
 *
 * CACHED, AND SCANNED BEFORE THE AP STARTS, because scanning afterwards
 * breaks the thing it is for. A scan makes the radio leave the access point's
 * channel for a couple of seconds; a phone associated to that access point
 * loses its beacons, and iOS -- which is only showing the setup page on
 * sufferance -- closes the captive sheet. The list is a property of the room,
 * not of the moment, so it is taken once while nobody is connected.
 */
int net_scan_results(struct NetScanEntry* out, int max);

/** Start the provisioning access point. Idempotent. */
esp_err_t net_start_ap(void);

/**
 * Join `ssid`, and block until it works or `timeout_ms` runs out.
 *
 * Blocking is deliberate: everything after this -- the address on the panel,
 * the QR that encodes it, the catalogue fetch -- needs an IP, and there is
 * nothing useful to do concurrently while waiting for one.
 */
esp_err_t net_start_sta(const char* ssid, const char* pass, uint32_t timeout_ms);

/** Whether the station currently holds an IP address. */
bool net_have_ip(void);

/** The station's address as "192.168.1.57", or "0.0.0.0" if there is none. */
void net_ip_str(char* out, size_t out_len);

/**
 * The `.local` name this board will answer to, WITHOUT starting mDNS.
 *
 * Derived from the MAC, so it is known before the device has an address --
 * which is what lets the provisioning page hand the phone a working link
 * before the network it names has been joined.
 */
void net_mdns_host(char* out, size_t out_len);

/** This board's provisioning AP credentials, derived from its MAC. */
void net_ap_credentials(char* ssid, size_t ssid_len, char* pass, size_t pass_len);

/**
 * Advertise this device on the local network as `<name>.local`, and as an
 * `_http._tcp` service.
 *
 * `out_host` receives the hostname, without the `.local`.
 *
 * THE PHONE CANNOT GO LOOKING. A browser has no mDNS, no raw sockets and no
 * ARP, so a web page cannot enumerate what is on the network -- discovery has
 * to run the other way: the device announces, and the operating system
 * resolves. iOS and macOS do that natively through Bonjour, which is what
 * makes `xmas-a1b2.local` dependable on the phone this is built for.
 *
 * It does not replace the QR. A name still has to be typed, and the QR is the
 * path that needs nothing typed at all; what the name buys is an address that
 * survives the router handing out a different one.
 */
esp_err_t net_start_mdns(char* out_host, size_t out_len);

/**
 * Another display on this network.
 *
 * `model` is what it is currently showing, taken from the TXT record it
 * advertises, so a list of peers can be useful without asking each one.
 */
struct NetPeer
{
    char host[24];
    char ip[16];
    char model[40];
};

/**
 * The other displays currently on the network.
 *
 * THIS IS THE DISCOVERY A BROWSER CANNOT DO. No browser exposes mDNS -- there
 * is no API to receive an advertisement, only the operating system's ability
 * to resolve a `.local` name someone already typed. So the device does the
 * browsing and the control page asks it; one display's page can then list
 * every display in the house.
 *
 * Reads a table kept up to date by a continuous mDNS browse, so it returns
 * immediately -- it does not run a query. Peers appear when they announce
 * themselves and disappear when they send a goodbye, rather than on a poll
 * interval. Never counts this device itself.
 */
int net_find_peers(struct NetPeer* out, int max);

/**
 * Find a model server advertising itself on this network, as a base URL.
 *
 * The setup form asks for this address, and it is the one field a user should
 * never have to type: a Wi-Fi password cannot be handed over by any phone
 * without Matter or DPP, but a server's address can announce itself. Writes
 * something like "http://192.168.1.146:8080" and returns true if one answered.
 */
bool net_find_model_server(char* out, size_t out_len, uint32_t timeout_ms);

/**
 * The shared name every display answers to -- but only one at a time.
 *
 * `xmas.local` should work if ANY display is on the network, and several
 * devices claiming one hostname is a fight: mDNS conflict detection makes the
 * losers rename themselves, so the name lands on whoever booted last, or
 * flaps. So they elect instead. Each browses for the others and the lowest
 * hostname wins -- a total order over names that are derived from MAC
 * addresses, so it is decided without any negotiation and every device reaches
 * the same answer on its own.
 *
 * The winner adds `xmas` as a DELEGATED hostname alongside its own, so it
 * answers to both. The losers answer only to their own names, which still
 * work. If the winner leaves, the next election promotes the new lowest.
 */
esp_err_t net_start_alias_election(void);

/** Whether this device is currently the one answering to `xmas.local`. */
bool net_holds_alias(void);

/** Update what this device advertises it is showing. */
void net_mdns_set_model(const char* model);

/** Free internal DRAM, logged with `when` as the label. The number this
 *  firmware is closest to running out of, so it is worth a line at each step
 *  that spends a lot of it. */
void net_heap_report(const char* when);

#endif /* NET_H */
