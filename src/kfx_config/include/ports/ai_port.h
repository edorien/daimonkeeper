/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file ai_port.h
 *     AiPort: the calls into kfx_ai: setting up and toggling computer players, and the build and held-thing hooks the simulation triggers.
 * @par Purpose:
 *     Provided by kfx_ai, which builds the table (ai_port_impl);
 *     main.cpp installs it with set_ai_port(). Callers use the ai_*()
 *     wrappers. The entries are listed once, in ports/ai_port.def, which
 *     generates the struct, the wrappers, the unwired defaults (ai_port.c)
 *     and their test. Refactor pass 2, S15
 *     (docs/refactor-pass2/stage-15-ports-and-events.md).
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_PORT_AI_PORT_H
#define DK_PORT_AI_PORT_H

#include "bflib_basics.h"
#include "globals.h"
#include "port_check.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
struct Computer2;
struct Thing;
struct Coord3d;

struct AiPort {
#define KFX_PORT_VOID(name, params, args) void (*name) params;
#define KFX_PORT_RET(ret, name, params, args, dflt) ret (*name) params;
#define KFX_PORT_VOIDX(name, params, args, stmt) KFX_PORT_VOID(name, params, args)
#define KFX_PORT_RETX(ret, name, params, args, dflt, stmt) KFX_PORT_RET(ret, name, params, args, dflt)
#define KFX_PORT_RETS(ret, name, params, args, body) KFX_PORT_RET(ret, name, params, args, 0)
#include "ports/ai_port.def"
#undef KFX_PORT_VOID
#undef KFX_PORT_RET
#undef KFX_PORT_VOIDX
#undef KFX_PORT_RETX
#undef KFX_PORT_RETS
};
KFX_ASSERT_PORT_TABLE(struct AiPort);

/** The installed table; never NULL (the unwired defaults until main.cpp sets it). */
extern const struct AiPort *ai_port;
/** The unwired defaults, for tests. */
extern const struct AiPort ai_port_defaults;
void set_ai_port(const struct AiPort *port);

#define KFX_PORT_VOID(name, params, args) static inline void ai_##name params { ai_port->name args; }
#define KFX_PORT_RET(ret, name, params, args, dflt) static inline ret ai_##name params { return ai_port->name args; }
#define KFX_PORT_VOIDX(name, params, args, stmt) KFX_PORT_VOID(name, params, args)
#define KFX_PORT_RETX(ret, name, params, args, dflt, stmt) KFX_PORT_RET(ret, name, params, args, dflt)
#define KFX_PORT_RETS(ret, name, params, args, body) KFX_PORT_RET(ret, name, params, args, 0)
#include "ports/ai_port.def"
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
