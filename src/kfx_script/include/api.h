#ifndef API_H
#define API_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// enum ApiEventDataType/struct ApiEventData live in kfx_config's
// ports/script_port.h -- kfx_sim's actionpt.c is the lowest-ranked real
// producer of event data payloads and can't reach up to this (kfx_script)
// header, so the type is defined there and just used here.
#include "ports/script_port.h"

#ifdef __cplusplus
extern "C"
{
#endif

    int64_t api_init_server();
    void api_update_server();
    void api_close_server();

    void api_event(const char *event_name);
    void api_event_with_data(const char *event_name, const struct ApiEventData *data, size_t data_count);

    size_t get_max_flags();

    void api_clear_all_subscriptions();
    int64_t api_is_subscribed_to_event(const char *event_name);
    int64_t api_subscribe_event(const char *event_name);
    int64_t api_unsubscribe_event(const char *event_name);
    int64_t api_is_subscribed_to_var(PlayerNumber plyr_idx, unsigned char valtype, int64_t validx);
    int64_t api_subscribe_var(PlayerNumber plyr_idx, const char *var_name, unsigned char valtype, int64_t validx);
    int64_t api_unsubscribe_var(PlayerNumber plyr_idx, unsigned char valtype, int64_t validx);

#ifdef __cplusplus
}
#endif

#endif