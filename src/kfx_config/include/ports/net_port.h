/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file net_port.h
 *     NetPort: the calls into kfx_net: the packet history (for the local view's lag compensation) and the matchmaking settings from keeperfx.cfg.
 * @par Purpose:
 *     Provided by kfx_net, which builds the table (net_port_impl);
 *     main.cpp installs it with set_net_port(). Callers use the netport_*()
 *     wrappers. The entries are listed once, in ports/net_port.def, which
 *     generates the struct, the wrappers, the unwired defaults (net_port.c)
 *     and their test. Refactor pass 2, S15
 *     (docs/refactor-pass2/stage-15-ports-and-events.md).
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_PORT_NET_PORT_H
#define DK_PORT_NET_PORT_H

#include "bflib_basics.h"
#include "globals.h"
#include "port_check.h"
#include "bflib_netsp.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
struct Packet;

struct NetPort {
#define KFX_PORT_VOID(name, params, args) void (*name) params;
#define KFX_PORT_RET(ret, name, params, args, dflt) ret (*name) params;
#define KFX_PORT_VOIDX(name, params, args, stmt) KFX_PORT_VOID(name, params, args)
#define KFX_PORT_RETX(ret, name, params, args, dflt, stmt) KFX_PORT_RET(ret, name, params, args, dflt)
#define KFX_PORT_RETS(ret, name, params, args, body) KFX_PORT_RET(ret, name, params, args, 0)
#include "ports/net_port.def"
#undef KFX_PORT_VOID
#undef KFX_PORT_RET
#undef KFX_PORT_VOIDX
#undef KFX_PORT_RETX
#undef KFX_PORT_RETS
};
KFX_ASSERT_PORT_TABLE(struct NetPort);

/** The installed table; never NULL (the unwired defaults until main.cpp sets it). */
extern const struct NetPort *net_port;
/** The unwired defaults, for tests. */
extern const struct NetPort net_port_defaults;
void set_net_port(const struct NetPort *port);

#define KFX_PORT_VOID(name, params, args) static inline void netport_##name params { net_port->name args; }
#define KFX_PORT_RET(ret, name, params, args, dflt) static inline ret netport_##name params { return net_port->name args; }
#define KFX_PORT_VOIDX(name, params, args, stmt) KFX_PORT_VOID(name, params, args)
#define KFX_PORT_RETX(ret, name, params, args, dflt, stmt) KFX_PORT_RET(ret, name, params, args, dflt)
#define KFX_PORT_RETS(ret, name, params, args, body) KFX_PORT_RET(ret, name, params, args, 0)
#include "ports/net_port.def"
#undef KFX_PORT_VOID
#undef KFX_PORT_RET
#undef KFX_PORT_VOIDX
#undef KFX_PORT_RETX
#undef KFX_PORT_RETS
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
