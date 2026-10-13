/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/**
 * @file net_portforward.cpp
 *     Port forwarding support using NAT-PMP and UPnP.
 * @par Purpose:
 *     Tries NAT-PMP first (simpler/faster), falls back to UPnP.
 * @author   KeeperFX Team
 * @date     01 Jan 2026
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include <inttypes.h>
#include "pre_inc.h"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#endif
#include "net_portforward.h"
#include "version.h" // PRODUCT_NAME, the UPnP mapping label
#include "bflib_basics.h"
#include "bflib_datetm.h"

#ifndef MINIUPNP_STATICLIB
#define MINIUPNP_STATICLIB
#endif
#include <miniupnpc/miniupnpc.h>
#include <miniupnpc/upnpcommands.h>
#include <miniupnpc/upnperrors.h>

#ifndef NATPMP_STATICLIB
#define NATPMP_STATICLIB
#endif
#ifdef __WIN32__
#include <natpmp/natpmp.h>
#else
#include <natpmp.h>
#endif

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <future>
#include <mutex>
#ifdef __WIN32__
#include <winsock2.h>
#include <iphlpapi.h>
#endif

#define NATPMP_TIMEOUT_SECONDS 1.0
#define UPNP_TIMEOUT_MS 3000
// Every mapping is a lease the router drops by itself, renewed while the game is hosting. A permanent mapping would
// stay in the router, open to the internet, after a crash or a power cut. (UPnP routers that accept nothing but
// permanent leases still get one; the log says so.)
#define MAPPING_LEASE_SECONDS 3600
#define MAPPING_LEASE_TEXT "3600"
#define MAPPING_RENEW_SECONDS (MAPPING_LEASE_SECONDS / 2)

#include "post_inc.h"

enum PortForwardMethod {
    PORT_FORWARD_NONE = 0,
    PORT_FORWARD_NATPMP,
    PORT_FORWARD_UPNP
};

static enum PortForwardMethod active_method = PORT_FORWARD_NONE;
static uint16_t mapped_port = 0;
static std::future<void> mapping_task;
static std::atomic<uint32_t> mapping_generation = 0;
static std::mutex mapping_mutex;                     // pairs with mapping_changed: wakes the renewal wait early
static std::condition_variable mapping_changed;
static bool lease_is_permanent = false;              // a UPnP router that takes nothing else: nothing to renew

static struct UPNPUrls upnp_urls;
static struct IGDdatas upnp_data;
static char upnp_lanaddr[64];

static natpmp_t natpmp;

#ifdef __WIN32__
static int64_t is_cgnat_detected() {
    ULONG buffer_size = 0;
    if (GetAdaptersInfo(NULL, &buffer_size) != ERROR_BUFFER_OVERFLOW) {
        return 0;
    }
    IP_ADAPTER_INFO *adapter_info = (IP_ADAPTER_INFO *)malloc(buffer_size);
    if (!adapter_info) {
        return 0;
    }
    if (GetAdaptersInfo(adapter_info, &buffer_size) != NO_ERROR) {
        free(adapter_info);
        return 0;
    }
    for (IP_ADAPTER_INFO *adapter = adapter_info; adapter; adapter = adapter->Next) {
        uint64_t local_ip = inet_addr(adapter->IpAddressList.IpAddress.String);
        if (local_ip != INADDR_NONE && local_ip != 0) {
            unsigned char first_octet = local_ip & 0xFF;
            unsigned char second_octet = (local_ip >> 8) & 0xFF;
            if (first_octet == 100 && second_octet >= 64 && second_octet <= 127) {
                free(adapter_info);
                return 1;
            }
        }
        uint64_t gateway_ip = inet_addr(adapter->GatewayList.IpAddress.String);
        if (gateway_ip != INADDR_NONE && gateway_ip != 0) {
            unsigned char first_octet = gateway_ip & 0xFF;
            unsigned char second_octet = (gateway_ip >> 8) & 0xFF;
            if (first_octet == 100 && second_octet >= 64 && second_octet <= 127) {
                free(adapter_info);
                return 1;
            }
        }
    }
    free(adapter_info);
    return 0;
}
#else
static int64_t is_cgnat_detected() {
    return 0;
}
#endif

static int64_t natpmp_add_port_mapping(int64_t port) {
    clock_t start_time = LbTimerClock();
    if (initnatpmp(&natpmp, 0, 0) < 0) {
        LbNetLog("NAT-PMP: Failed to initialize\n");
        return 0;
    }
    if (sendpublicaddressrequest(&natpmp) < 0) {
        LbNetLog("NAT-PMP: Failed to send public address request\n");
        closenatpmp(&natpmp);
        return 0;
    }
    natpmpresp_t response;
    fd_set file_descriptors;
    struct timeval timeout;
    int64_t result;
    do {
        double elapsed = (double)(LbTimerClock() - start_time) / 1000.0;
        if (elapsed > NATPMP_TIMEOUT_SECONDS) {
            LbNetLog("NAT-PMP: Timeout getting public address\n");
            closenatpmp(&natpmp);
            return 0;
        }
        FD_ZERO(&file_descriptors);
        FD_SET(natpmp.s, &file_descriptors);
        getnatpmprequesttimeout(&natpmp, &timeout);
        select(FD_SETSIZE, &file_descriptors, NULL, NULL, &timeout);
        result = readnatpmpresponseorretry(&natpmp, &response);
    } while (result == NATPMP_TRYAGAIN);
    if (result < 0) {
        LbNetLog("NAT-PMP: Failed to get public address\n");
        closenatpmp(&natpmp);
        return 0;
    }
    if (sendnewportmappingrequest(&natpmp, NATPMP_PROTOCOL_UDP, port, port, MAPPING_LEASE_SECONDS) < 0) {
        LbNetLog("NAT-PMP: Failed to send port mapping request\n");
        closenatpmp(&natpmp);
        return 0;
    }
    do {
        double elapsed = (double)(LbTimerClock() - start_time) / 1000.0;
        if (elapsed > NATPMP_TIMEOUT_SECONDS) {
            LbNetLog("NAT-PMP: Timeout on port mapping\n");
            closenatpmp(&natpmp);
            return 0;
        }
        FD_ZERO(&file_descriptors);
        FD_SET(natpmp.s, &file_descriptors);
        getnatpmprequesttimeout(&natpmp, &timeout);
        select(FD_SETSIZE, &file_descriptors, NULL, NULL, &timeout);
        result = readnatpmpresponseorretry(&natpmp, &response);
    } while (result == NATPMP_TRYAGAIN);
    if (result < 0) {
        LbNetLog("NAT-PMP: Failed to add port mapping\n");
        closenatpmp(&natpmp);
        return 0;
    }
    LbNetLog("NAT-PMP: port forwarding active on port %" PRIu64 "\n", (uint64_t)(port));
    mapped_port = port;
    active_method = PORT_FORWARD_NATPMP;
    closenatpmp(&natpmp);
    return 1;
}

static void port_forward_add_mapping_internal(int64_t port)
{
    if (is_cgnat_detected()) {
        LbNetLog("CGNAT detected, automatic port forwarding unavailable\n");
        return;
    }
    if (natpmp_add_port_mapping(port)) {
        return;
    }
    int error = 0;
    struct UPNPDev *device_list = upnpDiscover(UPNP_TIMEOUT_MS, NULL, NULL, 0, 0, 2, &error);
    if (!device_list) {
        LbNetLog("UPnP: no devices found, port forwarding unavailable, UDP hole punching will be used\n");
        return;
    }
#if (MINIUPNPC_API_VERSION >= 18)
    int64_t internet_gateway_device_result = UPNP_GetValidIGD(device_list, &upnp_urls, &upnp_data, upnp_lanaddr, sizeof(upnp_lanaddr), NULL, 0);
#else
    int64_t internet_gateway_device_result = UPNP_GetValidIGD(device_list, &upnp_urls, &upnp_data, upnp_lanaddr, sizeof(upnp_lanaddr));
#endif
    freeUPNPDevlist(device_list);
    if (internet_gateway_device_result == 0) {
        LbNetLog("UPnP: router found but no valid IGD, port forwarding unavailable, UDP hole punching will be used\n");
        FreeUPNPUrls(&upnp_urls);
        return;
    }
    char port_string[16];
    snprintf(port_string, sizeof(port_string), "%" PRIu64, (uint64_t)(port));
    UPNP_DeletePortMapping(upnp_urls.controlURL, upnp_data.first.servicetype, port_string, "UDP", "");
    LbNetLog("UPnP: lanaddr=%s\n", upnp_lanaddr);
    lease_is_permanent = false;
    int64_t result = UPNP_AddPortMapping(upnp_urls.controlURL, upnp_data.first.servicetype, port_string, port_string, upnp_lanaddr, PRODUCT_NAME, "UDP", "", MAPPING_LEASE_TEXT);
    if (result == 725) { // OnlyPermanentLeasesSupported
        LbNetLog("UPnP: the router only takes permanent leases; the mapping is removed when the game stops hosting, but a crash would leave it\n");
        result = UPNP_AddPortMapping(upnp_urls.controlURL, upnp_data.first.servicetype, port_string, port_string, upnp_lanaddr, PRODUCT_NAME, "UDP", "", "0");
        lease_is_permanent = true;
    }
    if (result != UPNPCOMMAND_SUCCESS) {
        LbNetLog("UPnP: failed to add port mapping (error %" PRId64 "), UDP hole punching will be used\n", (int64_t)(result));
        FreeUPNPUrls(&upnp_urls);
        return;
    }
    LbNetLog("UPnP: port forwarding active on port %" PRIu64 "\n", (uint64_t)(port));
    mapped_port = port;
    active_method = PORT_FORWARD_UPNP;
}

static void port_forward_remove_mapping(void)
{
    if (active_method == PORT_FORWARD_NONE || mapped_port == 0) {
        return;
    }
    if (active_method == PORT_FORWARD_NATPMP) {
        if (initnatpmp(&natpmp, 0, 0) < 0) {
            LbNetLog("NAT-PMP: Failed to initialize for removal\n");
            mapped_port = 0;
            active_method = PORT_FORWARD_NONE;
            return;
        }
        sendnewportmappingrequest(&natpmp, NATPMP_PROTOCOL_UDP, mapped_port, 0, 0);
        natpmpresp_t response;
        fd_set file_descriptors;
        struct timeval timeout;
        int64_t result;
        clock_t start_time = LbTimerClock();
        do {
            double elapsed = (double)(LbTimerClock() - start_time) / 1000.0;
            if (elapsed > NATPMP_TIMEOUT_SECONDS) {
                break;
            }
            FD_ZERO(&file_descriptors);
            FD_SET(natpmp.s, &file_descriptors);
            getnatpmprequesttimeout(&natpmp, &timeout);
            select(FD_SETSIZE, &file_descriptors, NULL, NULL, &timeout);
            result = readnatpmpresponseorretry(&natpmp, &response);
        } while (result == NATPMP_TRYAGAIN);
        closenatpmp(&natpmp);
    } else if (active_method == PORT_FORWARD_UPNP) {
        char port_string[16];
        snprintf(port_string, sizeof(port_string), "%" PRIu64, (uint64_t)(mapped_port));
        UPNP_DeletePortMapping(upnp_urls.controlURL, upnp_data.first.servicetype, port_string, "UDP", NULL);
        FreeUPNPUrls(&upnp_urls);
    }
    mapped_port = 0;
    active_method = PORT_FORWARD_NONE;
}

// Asks the router to extend the lease; false if it no longer answers (the lease then simply runs out).
static bool port_forward_renew_mapping(void)
{
    if (active_method == PORT_FORWARD_NATPMP) {
        return natpmp_add_port_mapping(mapped_port) != 0;
    }
    if (active_method == PORT_FORWARD_UPNP) {
        char port_string[16];
        snprintf(port_string, sizeof(port_string), "%" PRIu64, (uint64_t)(mapped_port));
        return UPNP_AddPortMapping(upnp_urls.controlURL, upnp_data.first.servicetype, port_string, port_string, upnp_lanaddr, PRODUCT_NAME, "UDP", "", MAPPING_LEASE_TEXT) == UPNPCOMMAND_SUCCESS;
    }
    return false;
}

// Stops the renewal task and takes the mapping out of the router on any orderly exit, not only through host_destroy().
static void port_forward_at_exit(void)
{
    port_forward_set_mapping(0);
    if (mapping_task.valid()) {
        mapping_task.wait_for(std::chrono::seconds(8));
    }
}

void port_forward_set_mapping(uint16_t port)
{
    static std::once_flag exit_hook;
    std::call_once(exit_hook, [] { std::atexit(port_forward_at_exit); });
    uint32_t generation;
    {
        std::lock_guard<std::mutex> lock(mapping_mutex);
        generation = ++mapping_generation;
    }
    mapping_changed.notify_all();
    mapping_task = std::async(std::launch::async, [previous = std::move(mapping_task), port, generation]() mutable {
        if (previous.valid()) {
            previous.wait();
            previous = {};
        }
        if (generation != mapping_generation) {
            return;
        }
#ifdef _WIN32
        WSADATA data;
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            return;
        }
#endif
        port_forward_remove_mapping();
        if (port != 0 && generation == mapping_generation) {
            port_forward_add_mapping_internal(port);
            // Keep the lease alive for as long as this is the current mapping. A new request (or the exit hook)
            // bumps the generation, which ends the wait; the next task then removes the mapping.
            while (active_method != PORT_FORWARD_NONE && !lease_is_permanent) {
                std::unique_lock<std::mutex> lock(mapping_mutex);
                if (mapping_changed.wait_for(lock, std::chrono::seconds(MAPPING_RENEW_SECONDS),
                        [generation] { return generation != mapping_generation; })) {
                    break;
                }
                lock.unlock();
                if (!port_forward_renew_mapping()) {
                    LbNetLog("Port forwarding: the router did not renew the lease; it will run out on its own\n");
                    break;
                }
            }
        }
#ifdef _WIN32
        WSACleanup();
#endif
    });
}
