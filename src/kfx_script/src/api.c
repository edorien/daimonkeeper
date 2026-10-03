#include "pre_inc.h"

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN 1
#  include <winsock2.h>
#  include <ws2tcpip.h>
   typedef SOCKET kfx_socket_t;
#  define KFX_INVALID_SOCKET INVALID_SOCKET
#  define kfx_closesocket(s) closesocket(s)
#  define kfx_socket_error() WSAGetLastError()
#else
#  include <sys/types.h>
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <unistd.h>
#  include <fcntl.h>
#  include <errno.h>
#  include <sys/select.h>
   typedef int64_t kfx_socket_t;
#  define KFX_INVALID_SOCKET (-1)
#  define kfx_closesocket(s) close(s)
#  define kfx_socket_error() errno
#  ifndef SOCKET_ERROR
#    define SOCKET_ERROR (-1)
#  endif
#endif

#include "api.h"
#include "api_seat_view.h"
#include "api_seat_diff.h"
#include "api_seat_decision.h"
#include "api_log_tail.h"
#include "bflib_basics.h"
#include "external_seat.h"
#include "net_game.h"
#include "front_landview.h"
#include <json.h>
#include <json-dom.h>
#include "config_keeperfx.h"
#include "config_campaigns.h"
#include "front_landview.h"
#include "lvl_script.h"
#include "lvl_script_commands.h"
#include "lvl_script_lib.h"
#include "lvl_script_value.h"
#include "dungeon_data.h"
#include "player_data.h"
#include "player_instances.h"
#include "game_legacy.h"
#include "console_cmd.h"
#include "post_inc.h"
#include "value_util.h"

#define API_SERVER_BUFFER 4096

/** Largest data response (get_player_view and friends), in bytes. */
#define API_DATA_BUFFER (1024 * 1024)

#define API_SUBSCRIBE_LIST_SIZE 256

#define API_SUBSCRIBE_INACTIVE 0
#define API_SUBSCRIBE_EVENT 1
#define API_SUBSCRIBE_VAR 2

/**
 * Structure to hold API global variables.
 *
 * This structure defines global variables related to the API, including the server socket,
 * active client socket (only one client at a time), and a socket set for managing sockets.
 */
struct ApiGlobals
{
    kfx_socket_t serverSocket;  // Server socket for API communication
    kfx_socket_t activeSocket;  // Active client socket (only one client at a time)
} api = { KFX_INVALID_SOCKET, KFX_INVALID_SOCKET }; // sockets start invalid, not 0 (0 is a valid fd)

/**
 * Structure representing a subscribed variable.
 *
 * This structure holds information about a variable subscribed by a client, including
 * the player ID, type, and ID of the variable.
 */
struct SubscribedVariable
{
    PlayerNumber player_id;
    char name[COMMAND_WORD_LEN];
    unsigned char type;
    unsigned char id;
    int64_t val;
};

/**
 * Structure representing a subscription slot.
 *
 * This 'slot' can contain either a SubscribedVariable or an event.
 * The type is used to determine which type of subscription is held.
 * It's also possible to have an inactive subscription.
 */
struct Subscription
{
    struct SubscribedVariable var;
    char event[COMMAND_WORD_LEN];
    int64_t type;
} api_subscriptions[API_SUBSCRIBE_LIST_SIZE];

/**
 * Counter for the amount of active subscriptions.
 *
 * We use an int for this so we can stop checking the list of
 * subscription slots when we are sure there's no more subscriptions left.
 * This is done for performance reasons.
 */
int64_t api_sub_count = 0;

/**
 * Structure to hold the state of a dump buffer.
 *
 * This structure holds the state of a dump buffer, which is used by functions
 * for writing JSON data. It includes a pointer to the output buffer and the
 * remaining space available in the buffer.
 */
struct dump_buf_state
{
    char *out;     /**< Pointer to the output buffer. */
    int64_t out_space; /**< Remaining space available in the output buffer. */
};

/**
 * Callback function for writing JSON value dump.
 *
 * This function is a callback used by the JSON library for writing JSON value dump.
 * It copies the JSON data into a buffer, tracking the buffer space available.
 *
 * @param str Pointer to the buffer containing the JSON data.
 * @param size Size of the JSON data in bytes.
 * @param dump_buffer_state Pointer to the dump buffer state structure.
 *            It holds information about the output buffer and available space.
 *
 * @return 0 on success, JSON_ERR_OUTOFMEMORY (-2) if the buffer is too small.
 */
static int json_value_dump_writer(const char *str, size_t size, void *dump_buffer_state)
{
    // @author: https://github.com/wolfSSL/wolfsentry/blob/857c85d1b3a6c7b297efa2bbb6ea89817aea7b4b/src/kv.c#L395

    // Check if buffer is too small
    if (size > (size_t)((struct dump_buf_state *)dump_buffer_state)->out_space)
    {
        JUSTLOG("buffer too small");
        return JSON_ERR_OUTOFMEMORY;
    }

    // Copy data into current part of buffer
    memcpy(((struct dump_buf_state *)dump_buffer_state)->out, str, size);
    ((struct dump_buf_state *)dump_buffer_state)->out += size;
    ((struct dump_buf_state *)dump_buffer_state)->out_space -= (int64_t)size;

    return 0;
}

/**
 * Function to get the number of max available KeeperFX flags with a name
 *
 * @return size_t Amount of flags
 */
size_t get_max_flags()
{
    size_t num = 0;
    while (flag_desc[num].name != NULL)
    {
        num++;
    }
    return num;
}

/**
 * Send raw bytes over the active client socket (blocking until all sent or error).
 * Replaces SDLNet_TCP_Send().
 */
static void api_send(const char *data, int64_t len)
{
    if (api.activeSocket == KFX_INVALID_SOCKET || len <= 0)
        return;
    int64_t sent = 0;
    while (sent < len)
    {
        int64_t r = (int64_t)send(api.activeSocket, data + sent, len - sent, 0);
        if (r > 0)
        {
            sent += r;
            continue;
        }
        if (r < 0)
        {
#ifdef _WIN32
            if (WSAGetLastError() == WSAEWOULDBLOCK)
#else
            if (errno == EAGAIN || errno == EWOULDBLOCK)
#endif
            {
                fd_set wfds;
                FD_ZERO(&wfds);
                FD_SET(api.activeSocket, &wfds);
                if (select((int64_t)(api.activeSocket + 1), NULL, &wfds, NULL, NULL) > 0)
                    continue;
            }
        }
        break;
    }
}

/**
 * Initialize the TCP API server.
 *
 * This function initializes the TCP API server by opening a socket on the specified port.
 * It also initializes SDLNet library and sets up necessary data structures.
 * If the server is already active or the API is not enabled, it does nothing.
 *
 * @return 0 on success, 1 on failure.
 */
int64_t api_init_server()
{
    // Ignore if server is already active
    if (api.serverSocket != KFX_INVALID_SOCKET)
    {
        return 0;
    }

    // Check if API is enabled
    if (api_enabled != true)
    {
        return 0;
    }
    else
    {
        JUSTLOG("API server starting on port: %" PRIu64, (uint64_t)(api_port));
    }

#ifdef _WIN32
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
    {
        JUSTLOG("WSAStartup failed: %" PRId64, (int64_t)(kfx_socket_error()));
        return 1;
    }
#endif

    api.activeSocket = KFX_INVALID_SOCKET;

    kfx_socket_t srv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (srv == KFX_INVALID_SOCKET)
    {
        JUSTLOG("socket() failed: %" PRId64, (int64_t)(kfx_socket_error()));
        api_close_server();
        return 1;
    }

    // Allow quick restart after close
    int64_t reuse = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

    // Non-blocking server socket so accept() doesn't stall the game loop
#ifdef _WIN32
    u_long nb = 1;
    ioctlsocket(srv, FIONBIO, &nb);
#else
    {
        int64_t flags = fcntl(srv, F_GETFL, 0);
        fcntl(srv, F_SETFL, flags | O_NONBLOCK);
    }
#endif

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // localhost only, as SDL_net bound NULL host
    addr.sin_port = htons((int64_t)api_port);

    if (bind(srv, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
    {
        JUSTLOG("bind() failed: %" PRId64, (int64_t)(kfx_socket_error()));
        kfx_closesocket(srv);
        api_close_server();
        return 1;
    }

    if (listen(srv, 1) == SOCKET_ERROR)
    {
        JUSTLOG("listen() failed: %" PRId64, (int64_t)(kfx_socket_error()));
        kfx_closesocket(srv);
        api_close_server();
        return 1;
    }

    api.serverSocket = srv;

    JUSTLOG("API server active");

    // Initialize all subscription slots
    for (int64_t i = 0; i < API_SUBSCRIBE_LIST_SIZE; ++i)
    {
        api_subscriptions[i].type = API_SUBSCRIBE_INACTIVE;
    }

    JUSTLOG("Allocated %" PRId64 " API subscription slots", (int64_t)(API_SUBSCRIBE_LIST_SIZE));

    return 0;
}

/**
 * Send an API error message.
 *
 * This function sends an error message to the API client over the active socket.
 * If the API server is not active, this function does nothing.
 *
 * @param err A null-terminated string representing the error message to be sent.
 */
static void api_err(const char *err, VALUE *ack_id)
{
    // Do nothing if API server is not active
    if (!api.activeSocket)
    {
        return;
    }

    // Create JSON response object
    VALUE json_root_real;
    VALUE *json_root = &json_root_real;
    value_init_dict(json_root);

    // Add ack ID
    if (ack_id != NULL)
    {
        VALUE *val_ack = value_dict_add(json_root, "ack");
        *val_ack = *ack_id;
    }

    // Create success key
    VALUE *val_success = value_dict_add(json_root, "success");
    value_init_bool(val_success, false);

    // Create error key
    VALUE *val_err = value_dict_add(json_root, "error");
    value_init_string(val_err, (char *)err);

    // Create JSON response
    char json_string[1024];
    struct dump_buf_state dump_state = {json_string, sizeof(json_string) - 1};
    int64_t json_dump_return_value = json_dom_dump(json_root, json_value_dump_writer, &dump_state, 0, JSON_DOM_DUMP_MINIMIZE);

    *dump_state.out = 0;
    if (json_dump_return_value != 0)
    {
        value_fini(json_root);
        return;
    }

    // Add newline to end of data
    dump_state.out[0] = '\n';
    dump_state.out++;

    // Send data to client
    api_send(json_string, dump_state.out - json_string);
    value_fini(json_root);
}

/**
 * Send an API success message.
 *
 * This function sends a success message to the API client over the active socket.
 * If the API server is not active, this function does nothing.
 */
static void api_ok(VALUE *ack_id)
{
    // Do nothing if API server is not active
    if (!api.activeSocket)
    {
        return;
    }

    // Check if we can send a success message without an ack
    if (ack_id == NULL)
    {
        // We can send it directly without using JSON functions here
        const char msg[] = "{\"success\":true}\n";
        api_send(msg, strlen(msg));
        return;
    }

    // Create JSON response object
    VALUE json_root_real;
    VALUE *json_root = &json_root_real;
    value_init_dict(json_root);

    // Add ack
    VALUE *val_ack = value_dict_add(json_root, "ack");
    *val_ack = *ack_id;

    // Create success key
    VALUE *val_success = value_dict_add(json_root, "success");
    value_init_bool(val_success, true);

    // Create JSON response
    char json_string[1024];
    struct dump_buf_state dump_state = {json_string, sizeof(json_string) - 1};
    int64_t json_dump_return_value = json_dom_dump(json_root, json_value_dump_writer, &dump_state, 0, JSON_DOM_DUMP_MINIMIZE);

    *dump_state.out = 0;
    if (json_dump_return_value != 0)
    {
        value_fini(json_root);
        return;
    }

    // Add newline to end of data
    dump_state.out[0] = '\n';
    dump_state.out++;

    // Send data to client
    api_send(json_string, dump_state.out - json_string);
    value_fini(json_root);
}

/**
 * Return data to the API client.
 *
 * This function takes ownership of the provided value and constructs a JSON response
 * indicating the success status along with the provided value.
 *
 * @param success The success status of the operation, true for success, false for failure.
 * @param value The value to be returned to the API client.
 *              Ownership is transferred to this function.
 */
static void api_return_data(TbBool success, VALUE value, VALUE *ack_id)
{
    // Do nothing if API server is not active
    if (!api.activeSocket)
    {
        return;
    }

    // Create JSON response object
    VALUE json_root_real;
    VALUE *json_root = &json_root_real;
    value_init_dict(json_root);

    // Add ack ID
    if (ack_id != NULL)
    {
        VALUE *val_ack = value_dict_add(json_root, "ack");
        *val_ack = *ack_id;
    }

    // Create success key
    VALUE *val_success = value_dict_add(json_root, "success");
    value_init_bool(val_success, success);

    // Create data key
    VALUE *val_data = value_dict_add(json_root, "data");
    *val_data = value;

    // Create JSON response. Data responses (a seat's view is several KB) do not fit the small stack
    // buffers the ack/error replies use, so this one is heap-allocated.
    char *json_string = (char *)malloc(API_DATA_BUFFER);
    if (json_string == NULL)
    {
        api_err("OUT_OF_MEMORY", ack_id);
        value_fini(json_root);
        return;
    }
    struct dump_buf_state dump_state = {json_string, API_DATA_BUFFER - 2};
    int64_t json_dump_return_value = json_dom_dump(json_root, json_value_dump_writer, &dump_state, 0, JSON_DOM_DUMP_MINIMIZE);

    *dump_state.out = 0;
    if (json_dump_return_value != 0)
    {
        free(json_string);
        api_err("RESPONSE_TOO_LARGE", ack_id);
        value_fini(json_root);
        return;
    }

    // Add newline to end of data
    dump_state.out[0] = '\n';
    dump_state.out++;

    // Send data to client
    api_send(json_string, dump_state.out - json_string);
    free(json_string);
    value_fini(json_root);
}

/**
 * Send a string data response to the API client.
 *
 * This function sends a string data response to the API client over the active socket.
 * If the API server is not active, this function does nothing.
 *
 * @param data The string data to be sent to the API client.
 */
// static void api_return_data_string(const char *data)
// {
//     // Do nothing if the API server is not active
//     if (!api.activeSocket)
//     {
//         return;
//     }

//     // Create value to send back
//     VALUE dataValue, *value = &json_dataValue;
//     value_init_string(value, data);

//     // Send the data
//     api_return_data(true, dataValue);
// }

void api_return_var_update(PlayerNumber plyr_idx, const char *var_name, int64_t value)
{
    // Do nothing if API server is not active
    if (!api.activeSocket)
    {
        return;
    }

    // Create JSON response object
    VALUE json_root_real;
    VALUE *json_root = &json_root_real;
    value_init_dict(json_root);

    // Create event key
    VALUE *val_event = value_dict_add(json_root, "event");
    value_init_string(val_event, "VAR_UPDATE");

    // Create var change object
    VALUE *val_var = value_dict_add(json_root, "var");
    value_init_dict(val_var);

    // Add player string
    VALUE *val_var_player = value_dict_add(val_var, "player");
    value_init_string(val_var_player, player_code_name(plyr_idx));

    // Add variable name
    VALUE *val_var_name = value_dict_add(val_var, "name");
    value_init_string(val_var_name, var_name);

    // Add the new value
    VALUE *val_var_new_val = value_dict_add(val_var, "value");
    value_init_int32(val_var_new_val, value);

    // Create JSON response
    char json_string[1024];
    struct dump_buf_state dump_state = {json_string, sizeof(json_string) - 1};
    int64_t json_dump_return_value = json_dom_dump(json_root, json_value_dump_writer, &dump_state, 0, JSON_DOM_DUMP_MINIMIZE);

    *dump_state.out = 0;
    if (json_dump_return_value != 0)
    {
        value_fini(json_root);
        return;
    }

    // Add newline to end of data
    dump_state.out[0] = '\n';
    dump_state.out++;

    // Send data to client
    api_send(json_string, dump_state.out - json_string);
    value_fini(json_root);
}

/**
 * Send a long integer data response to the API client.
 *
 * This is useful for sending numeric values.
 *
 * This function sends a long integer data response to the API client over the active socket.
 * If the API server is not active, this function does nothing.
 *
 * @param data The long integer data to be sent to the API client.
 */
static void api_return_data_number(int64_t data, VALUE *ack_id)
{
    // Do nothing if API server is not active
    if (!api.activeSocket)
    {
        return;
    }

    // Check if we can send a success message without an ack
    if (ack_id == NULL)
    {
        // Send back the JSON as a string. A number should never be able to break the syntax.
        char buf[256];
        int64_t len = snprintf(buf, sizeof(buf) - 1, "{\"success\":true,\"data\":%" PRId64 "}\n", (int64_t)(data));
        api_send(buf, len);
        return;
    }

    // Create JSON response object
    VALUE json_root_real;
    VALUE *json_root = &json_root_real;
    value_init_dict(json_root);

    // Add ack
    VALUE *val_ack = value_dict_add(json_root, "ack");
    *val_ack = *ack_id;

    // Create success key
    VALUE *val_success = value_dict_add(json_root, "success");
    value_init_bool(val_success, true);

    // Create success key
    VALUE *val_data = value_dict_add(json_root, "data");
    value_init_int32(val_data, data);

    // Create JSON response
    char json_string[1024];
    struct dump_buf_state dump_state = {json_string, sizeof(json_string) - 1};
    int64_t json_dump_return_value = json_dom_dump(json_root, json_value_dump_writer, &dump_state, 0, JSON_DOM_DUMP_MINIMIZE);

    *dump_state.out = 0;
    if (json_dump_return_value != 0)
    {
        value_fini(json_root);
        return;
    }

    // Add newline to end of data
    dump_state.out[0] = '\n';
    dump_state.out++;

    // Send data to client
    api_send(json_string, dump_state.out - json_string);
    value_fini(json_root);
}

void api_clear_all_subscriptions()
{
    if (api_sub_count == 0)
    {
        return;
    }

    // Loop trough all subscriptions
    // We don't exit the loop earlier just incase
    // This way this function also works as a full subscription list refresh
    for (int64_t i = 0; i < API_SUBSCRIBE_LIST_SIZE; i++)
    {
        // If this subscription slot is inactive we can skip it
        if (api_subscriptions[i].type == API_SUBSCRIBE_INACTIVE)
        {
            continue;
        }

        // Set type as inactive and clear all data
        api_subscriptions[i].type = API_SUBSCRIBE_INACTIVE;
        memset(api_subscriptions[i].event, 0, sizeof(api_subscriptions[i].event));
        memset(&api_subscriptions[i].var, 0, sizeof(struct SubscribedVariable));
    }

    api_sub_count = 0;
}

int64_t api_is_subscribed_to_event(const char *event_name)
{
    // Look up if we are subscribed to this event
    int64_t api_sub_found_count = 0;
    for (int64_t i = 0; i < API_SUBSCRIBE_LIST_SIZE; i++)
    {
        // Cancel this subscription search if we have
        // seen the same amount of subscriptions as we are subscribed to
        if (api_sub_count == api_sub_found_count)
        {
            return false;
        }

        // If this subscription slot is inactive we can skip it
        if (api_subscriptions[i].type == API_SUBSCRIBE_INACTIVE)
        {
            continue;
        }

        api_sub_found_count++;

        // Ignore if this subscription slot is not an event
        if (api_subscriptions[i].type != API_SUBSCRIBE_EVENT)
        {
            continue;
        }

        // Break out of the for loop if this subscription event matches the triggered one
        if (strcmp(api_subscriptions[i].event, event_name) == 0)
        {
            return true;
        }
    }

    return false;
}

int64_t api_subscribe_event(const char *event_name)
{
    // Return if we are already subscribed to this event
    if (api_is_subscribed_to_event(event_name) == true)
    {
        return true;
    }

    // Make sure we have an open subscription slot
    if (api_sub_count >= API_SUBSCRIBE_LIST_SIZE)
    {
        WARNLOG(
            "Tried to register API event '%s' but we are already at the limit of %" PRId64 " subscription slots",
            event_name,
            (int64_t)(API_SUBSCRIBE_LIST_SIZE));

        return false;
    }

    // Loop trough the list of subscription slots to find an empty slot and create a subscription
    for (int64_t i = 0; i < API_SUBSCRIBE_LIST_SIZE; i++)
    {

        // If this subscription slot is inactive we'll use it
        if (api_subscriptions[i].type == API_SUBSCRIBE_INACTIVE)
        {
            api_subscriptions[i].type = API_SUBSCRIBE_EVENT;
            strncpy(api_subscriptions[i].event, event_name, sizeof(api_subscriptions[i].event) - 1);
            api_sub_count++;
            return true;
        }
    }

    return false;
}

int64_t api_unsubscribe_event(const char *event_name)
{
    // First make sure we are actually subscribed to this event
    if (api_is_subscribed_to_event(event_name) == false)
    {
        return true;
    }

    // Loop trough the list of subscription slots to find an empty slot and create a subscription
    for (int64_t i = 0; i < API_SUBSCRIBE_LIST_SIZE; i++)
    {

        // If this subscription slot is not an event we'll skip it
        if (api_subscriptions[i].type != API_SUBSCRIBE_EVENT)
        {
            continue;
        }

        // Check if this subscription slot is the event we're unsubscribing from
        if (strcmp(api_subscriptions[i].event, event_name) == 0)
        {
            api_subscriptions[i].type = API_SUBSCRIBE_INACTIVE;
            memset(api_subscriptions[i].event, 0, sizeof(api_subscriptions[i].event));
            api_sub_count--;
            return true;
        }
    }

    return false;
}

int64_t api_is_subscribed_to_var(PlayerNumber plyr_idx, unsigned char valtype, int64_t validx)
{
    // Look up if we are subscribed to updates of this variable
    int64_t api_sub_found_count = 0;
    for (int64_t i = 0; i < API_SUBSCRIBE_LIST_SIZE; i++)
    {
        // Cancel this subscription search if we have
        // seen the same amount of subscriptions as we are subscribed to
        if (api_sub_count == api_sub_found_count)
        {
            return false;
        }

        // If this subscription slot is inactive we can skip it
        if (api_subscriptions[i].type == API_SUBSCRIBE_INACTIVE)
        {
            continue;
        }

        api_sub_found_count++;

        // Ignore if this subscription slot is not for a var
        if (api_subscriptions[i].type != API_SUBSCRIBE_VAR)
        {
            continue;
        }

        // Break out of the for loop if this subscribed var matches the triggered one
        if (api_subscriptions[i].var.player_id == plyr_idx &&
            api_subscriptions[i].var.type == valtype &&
            api_subscriptions[i].var.id == validx)
        {
            return true;
        }
    }

    return false;
}

int64_t api_subscribe_var(PlayerNumber plyr_idx, const char *var_name, unsigned char valtype, int64_t validx)
{
    JUSTLOG("Sub: %" PRId64 ", %" PRId64 ", %" PRId64, (int64_t)(plyr_idx), (int64_t)(valtype), (int64_t)(validx));

    // Return if we are already subscribed to this var
    if (api_is_subscribed_to_var(plyr_idx, valtype, validx) == true)
    {
        return true;
    }

    // Make sure we have an open subscription slot
    if (api_sub_count >= API_SUBSCRIBE_LIST_SIZE)
    {
        WARNLOG("Tried to register to update of var but we are already at the limit of %" PRId64 " subscription slots", (int64_t)(API_SUBSCRIBE_LIST_SIZE));
        return false;
    }

    // Loop trough the list of subscription slots to find an empty slot and create a subscription
    for (int64_t i = 0; i < API_SUBSCRIBE_LIST_SIZE; i++)
    {

        // If this subscription slot is inactive we'll use it
        if (api_subscriptions[i].type == API_SUBSCRIBE_INACTIVE)
        {

            struct SubscribedVariable sub_var;
            sub_var.player_id = plyr_idx;
            sub_var.type = valtype;
            sub_var.id = validx;
            sub_var.val = get_condition_value(plyr_idx, valtype, validx);
            strncpy(sub_var.name, var_name, sizeof(sub_var.name) - 1);

            api_subscriptions[i].type = API_SUBSCRIBE_VAR;
            api_subscriptions[i].var = sub_var;

            api_sub_count++;
            return true;
        }
    }

    return false;
}

int64_t api_unsubscribe_var(PlayerNumber plyr_idx, unsigned char valtype, int64_t validx)
{
    // First make sure we are actually subscribed to this var
    if (api_is_subscribed_to_var(plyr_idx, valtype, validx) == false)
    {
        return true;
    }

    // Loop trough the list of subscription slots to find an empty slot and create a subscription
    for (int64_t i = 0; i < API_SUBSCRIBE_LIST_SIZE; i++)
    {

        // If this subscription slot is not an event we'll skip it
        if (api_subscriptions[i].type != API_SUBSCRIBE_VAR)
        {
            continue;
        }

        // Check if this subscription slot is the event we're unsubscribing from
        if (api_subscriptions[i].var.player_id == plyr_idx &&
            api_subscriptions[i].var.type == valtype &&
            api_subscriptions[i].var.id == validx)
        {
            api_subscriptions[i].type = API_SUBSCRIBE_INACTIVE;
            memset(&api_subscriptions[i].var, 0, sizeof(struct SubscribedVariable));
            api_sub_count--;
            return true;
        }
    }

    return false;
}

void api_check_var_update()
{
    // Do nothing if API server is not active
    if (!api.activeSocket)
    {
        return;
    }

    // Loop trough all our subscriptions
    int64_t api_sub_found_count = 0;
    for (int64_t i = 0; i < API_SUBSCRIBE_LIST_SIZE; i++)
    {
        // Cancel this subscription search if we have
        // seen the same amount of subscriptions as we are subscribed to
        if (api_sub_count == api_sub_found_count)
        {
            return;
        }

        // If this subscription slot is inactive we can skip it
        if (api_subscriptions[i].type == API_SUBSCRIBE_INACTIVE)
        {
            continue;
        }

        api_sub_found_count++;

        // Ignore if this subscription slot is not for a var
        if (api_subscriptions[i].type != API_SUBSCRIBE_VAR)
        {
            continue;
        }

        // Get the variable value
        int64_t variable_value = get_condition_value(
            api_subscriptions[i].var.player_id,
            api_subscriptions[i].var.type,
            api_subscriptions[i].var.id);

        // Check if variable has changed
        if (api_subscriptions[i].var.val != variable_value)
        {

            // Update the remembered value
            api_subscriptions[i].var.val = variable_value;

            // Send notification to client
            api_return_var_update(
                api_subscriptions[i].var.player_id,
                api_subscriptions[i].var.name,
                api_subscriptions[i].var.val);
        }
    }
}

/**
 * Send an API event message with optional structured data.
 *
 * JSON construction is kept inside the API implementation so callers only
 * provide typed C values. The event is only serialized and sent when a client
 * is actively subscribed to it.
 *
 * @param event_name A null-terminated string representing the event name.
 * @param data Optional array of named event data values.
 * @param data_count Number of entries in data.
 */
void api_event_with_data(const char *event_name, const struct ApiEventData *data, size_t data_count)
{
    // Do nothing if API server is not active
    if (!api.activeSocket)
    {
        return;
    }

    // Do nothing if we are not subscribed to this event
    if (api_is_subscribed_to_event(event_name) == false)
    {
        return;
    }

    VALUE json_root_real;
    VALUE *json_root = &json_root_real;
    value_init_dict(json_root);

    VALUE *val_event = value_dict_add(json_root, "event");
    value_init_string(val_event, event_name);

    if (data != NULL && data_count > 0)
    {
        VALUE *val_data = value_dict_add(json_root, "data");
        value_init_dict(val_data);

        for (size_t i = 0; i < data_count; i++)
        {
            const struct ApiEventData *item = &data[i];
            VALUE *value = value_dict_add(val_data, item->name);

            switch (item->type)
            {
            case API_EVENT_DATA_INT32:
                value_init_int32(value, item->value.int32_value);
                break;
            case API_EVENT_DATA_UINT32:
                value_init_uint32(value, item->value.uint32_value);
                break;
            case API_EVENT_DATA_INT64:
                value_init_int64(value, item->value.int64_value);
                break;
            case API_EVENT_DATA_UINT64:
                value_init_uint64(value, item->value.uint64_value);
                break;
            case API_EVENT_DATA_FLOAT:
                value_init_float(value, item->value.float_value);
                break;
            case API_EVENT_DATA_DOUBLE:
                value_init_double(value, item->value.double_value);
                break;
            case API_EVENT_DATA_BOOL:
                value_init_bool(value, item->value.bool_value);
                break;
            case API_EVENT_DATA_STRING:
                value_init_string(value, item->value.string_value != NULL ? item->value.string_value : "");
                break;
            default:
                value_init_null(value);
                break;
            }
        }

    }

    char json_string[API_SERVER_BUFFER];
    struct dump_buf_state dump_state = {json_string, sizeof(json_string) - 1};
    int64_t json_dump_return_value = json_dom_dump(json_root, json_value_dump_writer, &dump_state, 0, JSON_DOM_DUMP_MINIMIZE);

    *dump_state.out = 0;
    if (json_dump_return_value != 0)
    {
        value_fini(json_root);
        return;
    }

    *dump_state.out++ = '\n';
    api_send(json_string, dump_state.out - json_string);
    value_fini(json_root);
}

/**
 * Send an API event message without additional data.
 *
 * This remains the compatibility wrapper used by existing event callers.
 */
void api_event(const char *event_name)
{
    api_event_with_data(event_name, NULL, 0);
}

/**
 * Process the incoming buffer from the API client.
 *
 * This function processes the incoming buffer from the API client, parsing the JSON object
 * and executing the corresponding action. It handles various actions such as map commands,
 * console commands, getting player flags, reading and setting variables, and retrieving
 * level information.
 *
 * @param buffer The buffer containing the JSON data sent by the client.
 * @param buf_size The size of the buffer.
 */
static void api_process_buffer(const char *buffer, size_t buf_size)
{
    // Acknowledgement ID
    // This is used to link a response to a request.
    // The Ack ID should be sent back exactly like the client sent it.
    // It is useful because some clients handle our packets out of order or in very different scopes.
    VALUE *ack_id;

    // Values for the data of the buffer
    VALUE json_data, *value = &json_data;

    // Handle closing null byte
    if (buffer[buf_size - 1] == 0)
    {
        buf_size -= 1;
    }

    // Check if something is actually sent
    if (strlen(buffer) < 1)
    {
        api_err("NO_JSON", NULL);
        return;
    }

    // Decode the json object
    int64_t ret = json_dom_parse(buffer, buf_size, NULL, 0, value, NULL);
    if (ret != 0)
    {
        api_err("INVALID_JSON", NULL);
        return;
    }

    // Make sure we have a json object
    if (value_type(value) != VALUE_DICT)
    {
        api_err("INVALID_JSON_OBJECT", NULL);
        value_fini(&json_data);
        return;
    }

    // Get ack ID of the packet
    ack_id = value_dict_get(value, "ack");

    // Get the action the user wants to do
    const char *action = value_string(value_dict_get(value, "action"));
    if (action == NULL)
    {
        api_err("MISSING_ACTION", ack_id);
        value_fini(&json_data);
        return;
    }

    // Get the player id for the action.
    // Falls back to player 0 (default player)
    PlayerNumber player_id = my_player_number;
    VALUE *player = value_dict_get(value, "player");
    if (value_type(player) == VALUE_INT32)
    {
        player_id = (PlayerNumber)value_int32(player);
    }
    else if (value_type(player) == VALUE_STRING)
    {
        player_id = get_id(player_desc, (char *)value_string(player));
    }

    // ==================================================================================================================================
    // Commands that always work
    // ==================================================================================================================================

    // Handle get KeeperFX info command
    if (strcasecmp("get_kfx_info", action) == 0)
    {
        // Create level data to return to client
        VALUE data_kfx_info_real;
        VALUE *data_kfx_info = &data_kfx_info_real;
        value_init_dict(data_kfx_info);

        // Add stuff to level data
        value_init_string(value_dict_add(data_kfx_info, "kfx_version"), VER_STRING);

        // Return data to client
        api_return_data(true, data_kfx_info_real, ack_id);

        // End
        value_fini(&json_data);
        return;
    }

    // Handle subscribe var command
    if (strcasecmp("subscribe_var", action) == 0)
    {
        // Get variable name
        const char *variable_name = (char *)value_string(value_dict_get(value, "var"));
        if (variable_name == NULL || strlen(variable_name) < 1)
        {
            api_err("MISSING_VAR", ack_id);
            value_fini(&json_data);
            return;
        }

        // Recognize variable
        int64_t variable_id, variable_type;
        if (parse_get_varib(variable_name, &variable_id, &variable_type,1) == false)
        {
            api_err("UNKNOWN_VAR", ack_id);
            value_fini(&json_data);
            return;
        }

        // Try to subscribe to the variable
        if (api_subscribe_var(player_id, variable_name, variable_type, variable_id))
        {
            api_ok(ack_id);
        }
        else
        {
            api_err("SUB_FAILED", ack_id);
        }

        // End
        value_fini(&json_data);
        return;
    }

    // Handle subscribe var command
    if (strcasecmp("unsubscribe_var", action) == 0)
    {
        // Get variable name
        char *variable_name = (char *)value_string(value_dict_get(value, "var"));
        if (variable_name == NULL || strlen(variable_name) < 1)
        {
            api_err("MISSING_VAR", ack_id);
            value_fini(&json_data);
            return;
        }

        // Recognize variable
        int64_t variable_id, variable_type;
        if (parse_get_varib(variable_name, &variable_id, &variable_type,1) == false)
        {
            api_err("UNKNOWN_VAR", ack_id);
            value_fini(&json_data);
            return;
        }

        // Try to subscribe to the variable
        if (api_unsubscribe_var(player_id, variable_type, variable_id))
        {
            api_ok(ack_id);
        }
        else
        {
            api_err("SUB_FAILED", ack_id);
        }

        // End
        value_fini(&json_data);
        return;
    }

    // Handle subscribe var command
    if (strcasecmp("subscribe_event", action) == 0)
    {
        // Get event name
        char *event_name = (char *)value_string(value_dict_get(value, "event"));
        if (event_name == NULL || strlen(event_name) < 1)
        {
            api_err("MISSING_EVENT", ack_id);
            value_fini(&json_data);
            return;
        }

        // Make sure event name is not too long
        if (strlen(event_name) > COMMAND_WORD_LEN)
        {
            api_err("STRING_TOO_LONG", ack_id);
            value_fini(&json_data);
            return;
        }

        // Try to subscribe to the variable
        if (api_subscribe_event(event_name))
        {
            api_ok(ack_id);
        }
        else
        {
            api_err("SUB_FAILED", ack_id);
        }

        // End
        value_fini(&json_data);
        return;
    }

    // Handle subscribe var command
    if (strcasecmp("unsubscribe_event", action) == 0)
    {
        // Get event name
        char *event_name = (char *)value_string(value_dict_get(value, "event"));
        if (event_name == NULL || strlen(event_name) < 1)
        {
            api_err("MISSING_EVENT", ack_id);
            value_fini(&json_data);
            return;
        }

        // Try to subscribe to the variable
        if (api_unsubscribe_event(event_name))
        {
            api_ok(ack_id);
        }
        else
        {
            api_err("SUB_FAILED", ack_id);
        }

        // End
        value_fini(&json_data);
        return;
    }

    // Handle unsubscribe all
    if (strcasecmp("unsubscribe_all", action) == 0)
    {
        // Unsubscribe from every subscriptions
        api_clear_all_subscriptions();
        api_ok(ack_id);

        // End
        value_fini(&json_data);
        return;
    }

    // ==================================================================================================================================
    // Commands that only work when a map is loaded
    // ==================================================================================================================================

    // At this point our game needs to be a LOCAL game before we do anything
    if (kfx_sim_state.game_kind != GKind_LocalGame)
    {
        api_err("NOT_IN_LOCAL_GAME", ack_id);
        value_fini(&json_data);
        return;
    }

    // Handle map command
    if (strcasecmp("map_command", action) == 0)
    {
        // Do not allow this command when the game is paused
        if ((kfx_sim_state.operation_flags & GOF_Paused) != 0)
        {
            api_err("GAME_IS_PAUSED", ack_id);
            value_fini(&json_data);
            return;
        }

        // Get map command
        char *map_command = (char *)value_string(value_dict_get(value, "command"));
        if (map_command == NULL)
        {
            api_err("MISSING_COMMAND", ack_id);
            value_fini(&json_data);
            return;
        }

        // Execute map command
        if (script_scan_line(map_command, false, 99)) // Maximum level of a command support
        {
            api_ok(ack_id);
        }
        else
        {
            api_err("FAILED_TO_EXECUTE_MAP_COMMAND", ack_id);
        }

        // End
        value_fini(&json_data);
        return;
    }

    // Handle console command
    if (strcasecmp("console_command", action) == 0)
    {
        // Do not allow this command when the game is paused
        if ((kfx_sim_state.operation_flags & GOF_Paused) != 0)
        {
            api_err("GAME_IS_PAUSED", ack_id);
            value_fini(&json_data);
            return;
        }

        // Get console command
        char *console_command = (char *)value_string(value_dict_get(value, "command"));
        if (console_command == NULL || strlen(console_command) < 1)
        {
            api_err("MISSING_COMMAND", ack_id);
            value_fini(&json_data);
            return;
        }

        // If the console-prefix-character is at the start of the string we'll ignore that char
        if (console_command[0] == cmd_char)
        {
            console_command += 1;
        }

        // Execute console command
        if (cmd_exec(player_id, console_command))
        {
            api_ok(ack_id);
        }
        else
        {
            api_err("FAILED_TO_EXECUTE_CONSOLE_COMMAND", ack_id);
        }

        // End
        value_fini(&json_data);
        return;
    }

    // Handle get all player flags command
    if (strcasecmp("get_all_player_flags", action) == 0)
    {

        // Create flag data to return to client
        VALUE flag_data_real;
        VALUE *flag_data = &flag_data_real;
        value_init_dict(flag_data);

        for (int64_t player_index = 0; player_index < ALL_PLAYERS; player_index++)
        {
            // Create object for this player
            VALUE *player_info = value_dict_add(flag_data, player_code_name(player_index));
            value_init_dict(player_info);

            for (size_t flag_index = 0; flag_index < get_max_flags(); flag_index++)
            {
                // Get flag value
                int64_t flag_value = get_condition_value(player_id, SVar_FLAG, flag_index);

                // Add flag to player flag
                const char *flag_string = get_conf_parameter_text(flag_desc, flag_index);
                value_init_int32(value_dict_add(player_info, flag_string), flag_value);
            }
        }

        // Return data to client
        api_return_data(true, flag_data_real, ack_id);

        // End
        value_fini(&json_data);
        return;
    }

    // ==================================================================================================================================
    // External seat commands (docs/refactor/AI/LLM/02-transport-and-protocol.md). Unlike the commands above they work
    // while the game is paused, and they require an explicit "player": defaulting to the local human would be wrong here.
    // ==================================================================================================================================

    if (strcasecmp("claim_seat", action) == 0)
    {
        // Interim, until the Skirmish Slots & AI page can choose an External controller (milestone M5): make
        // an existing computer-controlled or unclaimed slot, or the local human's own (M10), an External seat.
        VALUE *pv = value_dict_get(value, "player");
        PlayerNumber claim_id = -1;
        if (value_type(pv) == VALUE_INT32) claim_id = (PlayerNumber)value_int32(pv);
        else if (value_type(pv) == VALUE_STRING) claim_id = get_id(player_desc, (char *)value_string(pv));
        else { api_err("MISSING_PLAYER", ack_id); value_fini(&json_data); return; }
        // Idempotent: a slot the Skirmish page already made External is answered with its existing user.
        NetUserId claimed = -1;
        if ((claim_id >= 0) && (claim_id < PLAYERS_COUNT) && player_exists(get_player(claim_id))
         && flag_is_set(get_player(claim_id)->allocflags, PlaF_ExternalSeat)
         && (get_net_user_player_number(get_player(claim_id)->user_id) == claim_id)) {
            claimed = get_player(claim_id)->user_id;
        } else {
            claimed = net_add_external_seat(claim_id);
        }
        if (claimed < 0) { api_err("CANNOT_CLAIM_SEAT", ack_id); value_fini(&json_data); return; }
        extseat_note_activity();
        VALUE data_real; VALUE *data = &data_real;
        value_init_dict(data);
        value_init_int32(value_dict_add(data, "player"), (int32_t)claim_id);
        value_init_int32(value_dict_add(data, "user"), (int32_t)claimed);
        api_return_data(true, data_real, ack_id);
        value_fini(&json_data);
        return;
    }

    if (strcasecmp("release_seat", action) == 0)
    {
        // The agent hands its seat back to the built-in AI (06 section 2.2, Option B, explicit form).
        VALUE *pv = value_dict_get(value, "player");
        PlayerNumber rel_id = -1;
        if (value_type(pv) == VALUE_INT32) rel_id = (PlayerNumber)value_int32(pv);
        else if (value_type(pv) == VALUE_STRING) rel_id = get_id(player_desc, (char *)value_string(pv));
        else { api_err("MISSING_PLAYER", ack_id); value_fini(&json_data); return; }
        extseat_note_activity();
        if (!net_release_external_seat(rel_id)) { api_err("NOT_A_VALID_SEAT", ack_id); value_fini(&json_data); return; }
        api_ok(ack_id);
        value_fini(&json_data);
        return;
    }

    if (strcasecmp("set_takeover", action) == 0)
    {
        // Opt-in: if this connection is lost, or the pause watchdog fires, the built-in AI takes every External seat.
        VALUE *ev = value_dict_get(value, "enabled");
        if (value_type(ev) != VALUE_BOOL && value_type(ev) != VALUE_INT32) { api_err("MISSING_ENABLED", ack_id); value_fini(&json_data); return; }
        extseat_set_takeover((value_type(ev) == VALUE_BOOL) ? value_bool(ev) : (value_int32(ev) != 0));
        extseat_note_activity();
        api_ok(ack_id);
        value_fini(&json_data);
        return;
    }

    if (strcasecmp("set_decision_policy", action) == 0)
    {
        // Minimum game turns between two DECISION_DUE events (default 100); reasons inside the window are held and delivered together.
        VALUE *mi = value_dict_get(value, "min_interval_turns");
        if (value_type(mi) != VALUE_INT32) { api_err("MISSING_MIN_INTERVAL", ack_id); value_fini(&json_data); return; }
        api_seat_decision_set_min_interval(value_int32(mi));
        extseat_note_activity();
        api_ok(ack_id);
        value_fini(&json_data);
        return;
    }

    if (strcasecmp("set_game_speed", action) == 0)
    {
        // The console's own "FPS"/turn-rate cheat (console_cmd.c), exposed here: how many simulated turns run per real
        // second. Global to the game (not per seat) -- an agent buying itself more real-time to think this way slows
        // the game for everyone watching, the same trade-off a human slowing it down for themselves would make.
        // turns_per_second=0 resets to the level's/command line's own configured rate (start_params.num_fps).
        VALUE *tv = value_dict_get(value, "turns_per_second");
        if (value_type(tv) != VALUE_INT32) { api_err("MISSING_TURNS_PER_SECOND", ack_id); value_fini(&json_data); return; }
        const int32_t requested = value_int32(tv);
        if (requested == 0) {
            kfx_sim_state.turns_per_second = start_params.num_fps;
        } else if ((requested < 1) || (requested > 100)) {
            api_err("BAD_TURNS_PER_SECOND", ack_id); value_fini(&json_data); return;
        } else {
            kfx_sim_state.turns_per_second = requested;
        }
        extseat_note_activity();
        VALUE data_real; VALUE *data = &data_real;
        value_init_dict(data);
        value_init_int64(value_dict_add(data, "turns_per_second"), (int64_t)kfx_sim_state.turns_per_second);
        api_return_data(true, data_real, ack_id);
        value_fini(&json_data);
        return;
    }

    if (strcasecmp("get_log_tail", action) == 0)
    {
        // The running game's own log (log_file_name, normally keeperfx.log in its data directory), for an agent
        // debugging a confusing session without a human tailing the file by hand. Reads only a bounded tail window
        // of the file (never the whole thing, however large the log has grown) and returns at most `lines` of that
        // window's complete lines (api_log_tail_lines does the actual splitting, and is what is unit-tested).
        VALUE *lv = value_dict_get(value, "lines");
        int64_t want = (value_type(lv) == VALUE_INT32) ? value_int32(lv) : 100;
        if (want < 1) want = 1;
        if (want > 500) want = 500;
        // The Debug log levels buffer their writes (docs/refactor-pass2/stage-02-logging-option.md).
        LbLogFlush();
        // Reported with every reply, so an agent can tell why a tail is short or empty.
        const char *log_level_name = get_conf_parameter_text(log_level_type, get_log_level() + 1);
        FILE *f = fopen(log_file_name, "rb");
        if ((f == NULL) && (get_log_level() == LogLvl_Off))
        {
            // Logging is off, so there is no file: an empty tail, not an error.
            VALUE data_real; VALUE *data = &data_real;
            value_init_dict(data);
            value_init_array(value_dict_add(data, "lines"));
            value_init_string(value_dict_add(data, "log_level"), (char *)log_level_name);
            api_return_data(true, data_real, ack_id);
            value_fini(&json_data);
            return;
        }
        if (f == NULL) { api_err("LOG_UNAVAILABLE", ack_id); value_fini(&json_data); return; }
        fseek(f, 0, SEEK_END);
        const int64_t size = (int64_t)ftell(f);
        const int64_t window = 262144; // comfortably more bytes than 500 lines will ever need
        const int64_t start = (size > window) ? (size - window) : 0;
        const size_t to_read = (size_t)(size - start);
        char *buf = (char *)malloc(to_read);
        if (buf == NULL) { fclose(f); api_err("LOG_UNAVAILABLE", ack_id); value_fini(&json_data); return; }
        fseek(f, start, SEEK_SET);
        const size_t got = fread(buf, 1, to_read, f);
        fclose(f);
        VALUE data_real; VALUE *data = &data_real;
        value_init_dict(data);
        VALUE *arr = value_dict_add(data, "lines");
        value_init_array(arr);
        api_log_tail_lines(buf, got, /*is_whole_buffer=*/(start == 0), want, arr);
        free(buf);
        value_init_string(value_dict_add(data, "log_level"), (char *)log_level_name);
        api_return_data(true, data_real, ack_id);
        value_fini(&json_data);
        return;
    }

    if (strcasecmp("get_seats", action) == 0)
    {
        // Every External seat in this game: the ones the Skirmish page created at start, plus any claimed since.
        VALUE data_real; VALUE *data = &data_real;
        value_init_dict(data);
        VALUE *arr = value_dict_add(data, "seats");
        value_init_array(arr);
        for (PlayerNumber p = 0; p < PLAYERS_COUNT; p++)
        {
            const struct PlayerInfo *pl = get_player(p);
            if (!player_exists(pl) || !flag_is_set(pl->allocflags, PlaF_ExternalSeat) || (get_net_user_player_number(pl->user_id) != p))
                continue;
            VALUE *e = value_array_append(arr);
            value_init_dict(e);
            value_init_int32(value_dict_add(e, "player"), (int32_t)p);
            value_init_int32(value_dict_add(e, "user"), (int32_t)pl->user_id);
        }
        api_return_data(true, data_real, ack_id);
        value_fini(&json_data);
        return;
    }

    if (strcasecmp("get_player_view", action) == 0 || strcasecmp("submit_action", action) == 0
     || strcasecmp("set_pause", action) == 0 || strcasecmp("advance_turns", action) == 0)
    {
        VALUE *pv = value_dict_get(value, "player");
        PlayerNumber seat_id = -1;
        if (value_type(pv) == VALUE_INT32) seat_id = (PlayerNumber)value_int32(pv);
        else if (value_type(pv) == VALUE_STRING) seat_id = get_id(player_desc, (char *)value_string(pv));
        else { api_err("MISSING_PLAYER", ack_id); value_fini(&json_data); return; }
        if ((seat_id < 0) || (seat_id >= PLAYERS_COUNT) || !player_exists(get_player(seat_id))) {
            api_err("INVALID_PLAYER", ack_id); value_fini(&json_data); return;
        }
        const struct PlayerInfo *seat_player = get_player(seat_id);
        const NetUserId seat_user = seat_player->user_id;
        if (!flag_is_set(seat_player->allocflags, PlaF_ExternalSeat) || (get_net_user_player_number(seat_user) != seat_id)) {
            api_err("NOT_A_VALID_SEAT", ack_id); value_fini(&json_data); return;
        }
        extseat_note_activity();

        VALUE *wd = value_dict_get(value, "watchdog_ms");
        if (value_type(wd) == VALUE_INT32) extseat_set_watchdog_ms(value_int32(wd));

        if (strcasecmp("get_player_view", action) == 0)
        {
            VALUE view_real; VALUE *view = &view_real;
            api_seat_build_view(view, seat_id);
            // since=<view_id of the last view received>: only what changed (03 M6); anything else gets the full view.
            VALUE *sv = value_dict_get(value, "since");
            const TbBool want_diff = (value_type(sv) == VALUE_INT32) || (value_type(sv) == VALUE_INT64);
            api_seat_finish_view(view, seat_id, want_diff, want_diff ? value_int64(sv) : 0);
            api_return_data(true, view_real, ack_id);
        }
        else if (strcasecmp("set_pause", action) == 0)
        {
            VALUE *pa = value_dict_get(value, "paused");
            if (value_type(pa) != VALUE_BOOL && value_type(pa) != VALUE_INT32) { api_err("MISSING_PAUSED", ack_id); value_fini(&json_data); return; }
            const TbBool want = (value_type(pa) == VALUE_BOOL) ? value_bool(pa) : (value_int32(pa) != 0);
            if (want) extseat_pause(); else extseat_resume();
            api_ok(ack_id);
        }
        else if (strcasecmp("advance_turns", action) == 0)
        {
            VALUE *tv = value_dict_get(value, "turns");
            if (value_type(tv) != VALUE_INT32) { api_err("MISSING_TURNS", ack_id); value_fini(&json_data); return; }
            const int32_t turns = value_int32(tv);
            if ((turns < 1) || (turns > 100000)) { api_err("BAD_TURNS", ack_id); value_fini(&json_data); return; }
            extseat_advance(turns);
            api_ok(ack_id);
        }
        else
        {
            struct ExtSeatVerb verb;
            memset(&verb, 0, sizeof(verb));
            const char *vname = value_string(value_dict_get(value, "verb"));
            if (vname == NULL) { api_err("MISSING_VERB", ack_id); value_fini(&json_data); return; }
            if (strcasecmp(vname, "place_trap") == 0) verb.kind = ESV_PlaceTrap;
            else if (strcasecmp(vname, "place_door") == 0) verb.kind = ESV_PlaceDoor;
            else if (strcasecmp(vname, "slap") == 0) verb.kind = ESV_Slap;
            else if (strcasecmp(vname, "pick_up") == 0) verb.kind = ESV_PickUp;
            else if (strcasecmp(vname, "drop") == 0) verb.kind = ESV_Drop;
            else if (strcasecmp(vname, "cast_power") == 0) verb.kind = ESV_CastPower;
            else if (strcasecmp(vname, "build_room") == 0) verb.kind = ESV_BuildRoom;
            else if (strcasecmp(vname, "mark_dig") == 0) verb.kind = ESV_MarkDig;
            else if (strcasecmp(vname, "sell") == 0) verb.kind = ESV_Sell;
            else if (strcasecmp(vname, "cancel") == 0) verb.kind = ESV_Cancel;
            else if (strcasecmp(vname, "set_tendency") == 0) verb.kind = ESV_SetTendency;
            else if (strcasecmp(vname, "move_creature") == 0) verb.kind = ESV_MoveCreature;
            else if (strcasecmp(vname, "release_creature") == 0) verb.kind = ESV_ReleaseCreature;
            else if (strcasecmp(vname, "set_alliance") == 0) verb.kind = ESV_SetAlliance;
            else { api_err("UNKNOWN_VERB", ack_id); value_fini(&json_data); return; }

            const char *kind = value_string(value_dict_get(value, (verb.kind == ESV_CastPower) ? "power" : "kind"));
            if (kind != NULL) snprintf(verb.name, sizeof(verb.name), "%s", kind);
            VALUE *pos = value_dict_get(value, "pos");
            if (value_type(pos) == VALUE_ARRAY && value_array_size(pos) == 2)
            {
                verb.has_pos = true;
                verb.stl_x = value_int32(value_array_get(pos, 0));
                verb.stl_y = value_int32(value_array_get(pos, 1));
            }
            VALUE *rect = value_dict_get(value, "slab_rect");
            if (value_type(rect) == VALUE_ARRAY && value_array_size(rect) == 4)
            {
                verb.has_rect = true;
                verb.slab_x0 = value_int32(value_array_get(rect, 0));
                verb.slab_y0 = value_int32(value_array_get(rect, 1));
                verb.slab_x1 = value_int32(value_array_get(rect, 2));
                verb.slab_y1 = value_int32(value_array_get(rect, 3));
            }
            VALUE *en = value_dict_get(value, "enabled");
            if ((value_type(en) == VALUE_BOOL) || (value_type(en) == VALUE_INT32)) {
                verb.has_enabled = true;
                verb.enabled = (value_type(en) == VALUE_BOOL) ? value_bool(en) : (value_int32(en) != 0);
            }
            VALUE *ht = value_dict_get(value, "hold_turns");
            if (value_type(ht) == VALUE_INT32) verb.hold_turns = value_int32(ht);
            VALUE *ov = value_dict_get(value, "overcharge_turns");
            if (value_type(ov) == VALUE_INT32) verb.overcharge_turns = value_int32(ov);
            VALUE *tid = value_dict_get(value, "thing_id");
            if (value_type(tid) == VALUE_INT32)
            {
                verb.has_thing = true;
                verb.thing_id = value_int32(tid);
            }
            VALUE *ap = value_dict_get(value, "ally_player");
            if (value_type(ap) == VALUE_INT32) { verb.has_target_player = true; verb.target_player = value_int32(ap); }
            else if (value_type(ap) == VALUE_STRING) { verb.has_target_player = true; verb.target_player = get_id(player_desc, (char *)value_string(ap)); }
            // Real-time play: queue=true lets a verb wait behind the running gesture. view_turn (the `turn` of the
            // view the decision was based on) with max_age_turns bounds how stale the order may be when it starts.
            VALUE *qv = value_dict_get(value, "queue");
            const TbBool queue = ((value_type(qv) == VALUE_BOOL) && value_bool(qv)) || ((value_type(qv) == VALUE_INT32) && (value_int32(qv) != 0));
            VALUE *vt = value_dict_get(value, "view_turn");
            VALUE *ma = value_dict_get(value, "max_age_turns");
            int64_t age = -1;
            if (value_type(vt) == VALUE_INT32) {
                age = (int64_t)get_gameturn() - (int64_t)value_int32(vt);
                if ((value_type(ma) == VALUE_INT32) && (value_int32(ma) >= 0)) {
                    verb.expires_turn = (int64_t)value_int32(vt) + (int64_t)value_int32(ma);
                }
            }
            // dry_run: validated exactly as a real submit would be (same error codes), but never queued, tracked, or
            // subject to the seat's queue-busy state -- so an agent can check "would this be accepted, and how many
            // steps" before spending a real decision on an order it is not sure it can afford, any number of times.
            VALUE *dr = value_dict_get(value, "dry_run");
            const TbBool dry_run = ((value_type(dr) == VALUE_BOOL) && value_bool(dr)) || ((value_type(dr) == VALUE_INT32) && (value_int32(dr) != 0));
            if (dry_run)
            {
                int64_t steps = 0;
                const char *err = extseat_check_verb(seat_user, seat_id, &verb, &steps);
                if (err != NULL) { api_err(err, ack_id); value_fini(&json_data); return; }
                VALUE data_real; VALUE *data = &data_real;
                value_init_dict(data);
                value_init_bool(value_dict_add(data, "would_succeed"), true);
                value_init_int32(value_dict_add(data, "steps"), (int32_t)steps);
                api_return_data(true, data_real, ack_id);
                value_fini(&json_data);
                return;
            }

            struct ExtSeatSubmitInfo info;
            memset(&info, 0, sizeof(info));
            const char *err = extseat_submit_verb_ex(seat_user, seat_id, &verb, queue, &info);
            if (err != NULL) { api_err(err, ack_id); value_fini(&json_data); return; }
            VALUE data_real; VALUE *data = &data_real;
            value_init_dict(data);
            value_init_int32(value_dict_add(data, "steps"), (int32_t)info.steps);
            value_init_int32(value_dict_add(data, "id"), (int32_t)info.id);
            value_init_int32(value_dict_add(data, "queued_behind"), (int32_t)info.queued_behind);
            if (age >= 0) value_init_int32(value_dict_add(data, "age_turns"), (int32_t)age);
            api_return_data(true, data_real, ack_id);
        }
        value_fini(&json_data);
        return;
    }

    // Handle read var command
    if (strcasecmp("read_var", action) == 0)
    {
        // Get variable name
        char *variable_name = (char *)value_string(value_dict_get(value, "var"));
        if (variable_name == NULL || strlen(variable_name) < 1)
        {
            api_err("MISSING_VAR", ack_id);
            value_fini(&json_data);
            return;
        }

        // Recognize variable
        int64_t variable_id, variable_type;
        if (parse_get_varib(variable_name, &variable_id, &variable_type,1) == false)
        {
            api_err("UNKNOWN_VAR", ack_id);
            value_fini(&json_data);
            return;
        }

        // Get the variable
        int64_t variable_value = get_condition_value(player_id, variable_type, variable_id);

        // Return the variable to the user
        api_return_data_number(variable_value, ack_id);

        // End
        value_fini(&json_data);
        return;
    }

    // Handle set var command
    if (strcasecmp("set_var", action) == 0)
    {
        // Get variable name
        char *variable_name = (char *)value_string(value_dict_get(value, "var"));
        if (variable_name == NULL || strlen(variable_name) < 1)
        {
            api_err("MISSING_VAR", ack_id);
            value_fini(&json_data);
            return;
        }

        // Recognize variable
        int64_t variable_id, variable_type;
        if (parse_get_varib(variable_name, &variable_id, &variable_type,1) == false)
        {
            api_err("UNKNOWN_VAR", ack_id);
            value_fini(&json_data);
            return;
        }

        // Check if this type of variable can be set dynamically
        if (
            variable_type != SVar_FLAG &&
            variable_type != SVar_CAMPAIGN_FLAG &&
            variable_type != SVar_BOX_ACTIVATED &&
            variable_type != SVar_TRAP_ACTIVATED &&
            variable_type != SVar_SACRIFICED &&
            variable_type != SVar_REWARDED)
        {
            api_err("UNABLE_TO_SET_VAR", ack_id);
            value_fini(&json_data);
            return;
        }

        // Get the new value
        VALUE *new_value = value_dict_get(value, "value");
        if (new_value == NULL || value_type(new_value) != VALUE_INT32)
        {
            api_err("VALUE_MUST_BE_INT", ack_id);
            value_fini(&json_data);
            return;
        }

        // Set the variable
        set_variable(player_id, variable_type, variable_id, value_int32(new_value));

        // Return success
        api_ok(ack_id);

        // End
        value_fini(&json_data);
        return;
    }

    // Handle get level info command
    if (strcasecmp("get_level_info", action) == 0 || strcasecmp("get_map_info", action) == 0)
    {
        // Get level info and name
        const char *lv_name = NULL;
        LevelNumber lv_number = get_loaded_level_number();
        struct LevelInformation *lv_info = get_level_info(lv_number);
        if (lv_info != NULL)
        {
            if (lv_info->name_stridx > 0)
            {
                lv_name = get_string(lv_info->name_stridx);
            }
            else
            {
                lv_name = lv_info->name;
            }
        }
        else if (is_multiplayer_level(lv_number))
        {
            lv_name = (const char *)level_name;
        }

        // Create level data to return to client
        VALUE data_level_info_real;
        VALUE *data_level_info = &data_level_info_real;
        value_init_dict(data_level_info);

        // Add stuff to level data
        value_init_string(value_dict_add(data_level_info, "level_name"), lv_name);
        value_init_int32(value_dict_add(data_level_info, "level_number"), lv_number);
        value_init_int32(value_dict_add(data_level_info, "players"), lv_info->players);
        value_init_int32(value_dict_add(data_level_info, "mapsize_x"), lv_info->mapsize_x);
        value_init_int32(value_dict_add(data_level_info, "mapsize_y"), lv_info->mapsize_y);
        value_init_bool(value_dict_add(data_level_info, "is_multiplayer"), is_multiplayer_level(lv_number));

        // Create campaign data and add to level data
        VALUE *data_campaign_info = value_dict_add(data_level_info, "campaign");
        value_init_dict(data_campaign_info);

        // Add stuff to campaign data
        value_init_string(value_dict_add(data_campaign_info, "campaign_name"), campaign.name);
        value_init_string(value_dict_add(data_campaign_info, "campaign_display_name"), campaign.display_name);
        value_init_string(value_dict_add(data_campaign_info, "campaign_fname"), campaign.fname);
        value_init_bool(value_dict_add(data_campaign_info, "is_map_pack"), is_map_pack());

        // Return data to client
        api_return_data(true, data_level_info_real, ack_id);

        // End
        value_fini(&json_data);
        return;
    }

    // Handle get level info command
    if (strcasecmp("get_current_game_info", action) == 0)
    {
        // Create level data to return to client
        VALUE data_current_game_info_real;
        VALUE *data_current_game_info = &data_current_game_info_real;
        value_init_dict(data_current_game_info);

        // Add stuff to level data
        value_init_int32(value_dict_add(data_current_game_info, "game_turn"), get_gameturn());

        // Return data to client
        api_return_data(true, data_current_game_info_real, ack_id);

        // End
        value_fini(&json_data);
        return;
    }

    // Return unknown action
    // TODO: we should do this check before...
    api_err("UNKNOWN_ACTION", ack_id);
    value_fini(&json_data);
}

/**
 * Processes a buffer containing concatenated JSON objects, extracting and
 * processing each valid JSON object individually.
 *
 * @param buffer Pointer to the buffer containing concatenated JSON data.
 * @param buf_size Size of the buffer in bytes.
 */
void api_process_multipart_json(const char *buffer, int64_t buf_size)
{
    int64_t start = -1;
    int64_t depth = 0;

    for (int64_t i = 0; i < buf_size; ++i)
    {
        if (buffer[i] == '{')
        {
            if (depth == 0)
            {
                start = i; // Start of a new JSON object
            }
            depth++;
        }
        else if (buffer[i] == '}')
        {
            depth--;
            if (depth == 0 && start != -1)
            {
                // Extract the JSON object from buffer[start] to buffer[i+1]
                int64_t json_length = i - start + 1;
                //char json_string[json_length + 1]; // +1 for null terminator
                char* json_string = (char*)malloc((json_length + 1) * sizeof(char));
                if (!json_string) return;
                strncpy(json_string, buffer + start, json_length);
                json_string[json_length] = '\0';

                // Process the extracted JSON object
                JUSTLOG("Received message from client: %s", json_string);
                api_process_buffer(json_string, json_length);
                free(json_string);
                // Reset start to look for the next JSON object
                start = -1;
            }
        }
    }

    if (depth > 0)
    {
        api_err("INVALID_JSON_IN_PACKET", NULL);
    }
}

/**
 * Update the API server and handle all pending packets.
 *
 * This function updates the API server by checking for incoming connections and messages.
 * It accepts new client connections, processes incoming messages, and handles disconnections.
 */
void api_update_server()
{
    // Ends an agent's turn-advance and runs the stuck-pause watchdog; cheap, and needed even with no client.
    extseat_poll();
    api_seat_decision_tick();

    // Return if the TCP server is not listening
    if (api.serverSocket == KFX_INVALID_SOCKET)
    {
        return;
    }

    // Accept a pending connection (non-blocking; no select()/socket-set needed).
    {
        struct sockaddr_in client_addr;
#ifdef _WIN32
        int addr_len = sizeof(client_addr);
#else
        socklen_t addr_len = sizeof(client_addr);
#endif
        kfx_socket_t client = accept(api.serverSocket, (struct sockaddr*)&client_addr, &addr_len);
        if (client != KFX_INVALID_SOCKET)
        {
            if (api.activeSocket != KFX_INVALID_SOCKET)
            {
                // Already have a client — reject the second one
                kfx_closesocket(client);
                WARNLOG("Got another connection while API connection is still active");
            }
            else
            {
                // Make the new client socket non-blocking too
#ifdef _WIN32
                u_long nb = 1;
                ioctlsocket(client, FIONBIO, &nb);
#else
                int64_t flags = fcntl(client, F_GETFL, 0);
                fcntl(client, F_SETFL, flags | O_NONBLOCK);
#endif
                api.activeSocket = client;
                JUSTLOG("Client connected");
            }
        }
    }

    // Read from the active client, if any (non-blocking).
    if (api.activeSocket != KFX_INVALID_SOCKET)
    {
        char buffer[API_SERVER_BUFFER];
        memset(buffer, 0, API_SERVER_BUFFER);

        int64_t received = (int64_t)recv(api.activeSocket, buffer, API_SERVER_BUFFER - 1, 0);
        if (received > 0)
        {
            // TODO: non nullbyte terminated buffers can crash
            // For example: when pressing Ctrl C when conneted over telnet

            // Remove any possible trailing newline from the data
            // This makes it work with a Telnet connection as well
            if (strlen(buffer) > 0 && buffer[strlen(buffer) - 1] == '\n')
            {
                buffer[strlen(buffer) - 1] = '\0';
            }

            // Process all JSON objects in the buffer
            api_process_multipart_json(buffer, strlen(buffer));
        }
        else if (received == 0)
        {
            // Graceful disconnect
            api_clear_all_subscriptions();
            extseat_on_client_lost();
            kfx_closesocket(api.activeSocket);
            api.activeSocket = KFX_INVALID_SOCKET;
            JUSTLOG("API connection closed");
        }
        else
        {
            // received < 0: EWOULDBLOCK/EAGAIN just means "no data yet"; any
            // other error means the connection is gone.
#ifdef _WIN32
            if (WSAGetLastError() != WSAEWOULDBLOCK)
#else
            if (errno != EAGAIN && errno != EWOULDBLOCK)
#endif
            {
                api_clear_all_subscriptions();
                extseat_on_client_lost();
                kfx_closesocket(api.activeSocket);
                api.activeSocket = KFX_INVALID_SOCKET;
                JUSTLOG("API connection closed");
            }
        }
    }

    // Handle variable subscriptions
    api_check_var_update();
}

/**
 * Close the API server.
 *
 * This function stops the API server by closing the server socket and active client socket,
 * and frees the socket set. It also shuts down the SDLNet library.
 */
void api_close_server()
{
    api_clear_all_subscriptions();

    JUSTLOG("API server closing");

    if (api.activeSocket != KFX_INVALID_SOCKET)
    {
        kfx_closesocket(api.activeSocket);
        api.activeSocket = KFX_INVALID_SOCKET;
    }

    if (api.serverSocket != KFX_INVALID_SOCKET)
    {
        kfx_closesocket(api.serverSocket);
        api.serverSocket = KFX_INVALID_SOCKET;
    }

#ifdef _WIN32
    WSACleanup();
#endif
}
