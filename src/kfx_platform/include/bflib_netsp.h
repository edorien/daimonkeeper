/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file bflib_netsp.h
 *     The network-transport service-provider contract (`struct NetSP`) and
 *     its supporting types.
 * @par Purpose:
 *     `struct NetSP` is a platform/HAL-style abstraction -- an
 *     implementation swapped in by the platform layer (bflib_enet.cpp's
 *     InitEnetSP()), consumed by the layer above (kfx_net's net_main.c,
 *     via `netstate.sp`) -- the same shape as bflib_video.h/bflib_sound.h's
 *     own abstractions. It used to be defined in kfx_net/include/net_main.h,
 *     one layer above the code that actually has to implement it
 *     (bflib_enet.cpp), which was a real check_layering.py accepted
 *     residual (kfx_platform -> kfx_net) -- see
 *     docs/refactor/todo/remove-remaining-layering-violations.md. Moved
 *     here so the platform layer implementing it doesn't have to reach
 *     upward for its own contract's definition; net_main.h now just
 *     #includes this instead of defining these symbols itself, so every
 *     existing kfx_net consumer keeps compiling unchanged.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef GIT_BFLIB_NETSP_H
#define GIT_BFLIB_NETSP_H

#include "bflib_basics.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TIMEOUT_CONNECT_HOLEPUNCH 5000
#define TIMEOUT_CONNECT_DIRECT_IPV6 5000
#define TIMEOUT_CONNECT_DIRECT_IPV4 5000
#define PEER_TIMEOUT_LIMIT 0
#define PEER_TIMEOUT_MIN_MS 5000
#define PEER_TIMEOUT_MAX_MS 30000

#define MAX_NET_USERS 4
#define MAX_NET_PEERS (MAX_NET_USERS - 1)
#define SERVER_ID 0
#define SOLO_HUMAN_ID 0 /* human player's user id in non-multiplayer, when relevant */

typedef int64_t NetUserId;

enum NetDropReason {
    NETDROP_MANUAL,
    NETDROP_ERROR,
};

typedef TbBool (*NetNewUserCallback)(NetUserId *assigned_id);
typedef void (*NetDropCallback)(NetUserId id, enum NetDropReason reason);

struct NetSP
{
    TbError (*init)(NetDropCallback drop_callback, NetNewUserCallback new_user_callback);
    void (*exit)();
    TbError (*host)(const char *session, void *options);
    TbError (*join)(const char *session, void *options);
    void (*update)(NetNewUserCallback new_user);
    void (*sendmsg_single)(NetUserId destination, const char *buffer, size_t size);
    void (*sendmsg_single_unsequenced)(NetUserId destination, const char *buffer, size_t size);
    void (*sendmsg_all)(const char *buffer, size_t size);
    size_t (*msgready)(NetUserId source, uint64_t timeout);
    size_t (*readmsg)(NetUserId source, char *buffer, size_t max_size);
    void (*drop_user)(NetUserId id);
};

#ifdef __cplusplus
}
#endif

#endif
