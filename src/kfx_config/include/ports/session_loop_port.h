/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file session_loop_port.h
 *     SessionLoopPort: the calls into kfx_apploop's session loop: keeping the game responsive while the network waits, the host-packet timestamp, and the frame interpolation fraction.
 * @par Purpose:
 *     Provided by kfx_apploop, which builds the table (session_loop_port_impl);
 *     main.cpp installs it with set_session_loop_port(). Callers use the loop_*()
 *     wrappers. The entries are listed once, in ports/session_loop_port.def, which
 *     generates the struct, the wrappers, the unwired defaults (session_loop_port.c)
 *     and their test. Refactor pass 2, S15
 *     (docs/refactor-pass2/stage-15-ports-and-events.md).
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_PORT_SESSION_LOOP_PORT_H
#define DK_PORT_SESSION_LOOP_PORT_H

#include "bflib_basics.h"
#include "globals.h"
#include "port_check.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

struct SessionLoopPort {
#define KFX_PORT_VOID(name, params, args) void (*name) params;
#define KFX_PORT_RET(ret, name, params, args, dflt) ret (*name) params;
#define KFX_PORT_VOIDX(name, params, args, stmt) KFX_PORT_VOID(name, params, args)
#define KFX_PORT_RETX(ret, name, params, args, dflt, stmt) KFX_PORT_RET(ret, name, params, args, dflt)
#define KFX_PORT_RETS(ret, name, params, args, body) KFX_PORT_RET(ret, name, params, args, 0)
#include "ports/session_loop_port.def"
#undef KFX_PORT_VOID
#undef KFX_PORT_RET
#undef KFX_PORT_VOIDX
#undef KFX_PORT_RETX
#undef KFX_PORT_RETS
};
KFX_ASSERT_PORT_TABLE(struct SessionLoopPort);

/** The installed table; never NULL (the unwired defaults until main.cpp sets it). */
extern const struct SessionLoopPort *session_loop_port;
/** The unwired defaults, for tests. */
extern const struct SessionLoopPort session_loop_port_defaults;
void set_session_loop_port(const struct SessionLoopPort *port);

#define KFX_PORT_VOID(name, params, args) static inline void loop_##name params { session_loop_port->name args; }
#define KFX_PORT_RET(ret, name, params, args, dflt) static inline ret loop_##name params { return session_loop_port->name args; }
#define KFX_PORT_VOIDX(name, params, args, stmt) KFX_PORT_VOID(name, params, args)
#define KFX_PORT_RETX(ret, name, params, args, dflt, stmt) KFX_PORT_RET(ret, name, params, args, dflt)
#define KFX_PORT_RETS(ret, name, params, args, body) KFX_PORT_RET(ret, name, params, args, 0)
#include "ports/session_loop_port.def"
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
