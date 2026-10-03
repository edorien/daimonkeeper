/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file input_focus_port.h
 *     InputFocusPort: the settings and live game state kfx_platform's input code checks on focus loss and for cursor locking (pause, possession, replay).
 * @par Purpose:
 *     Provided by kfx_sim, which builds the table (input_focus_port_impl);
 *     main.cpp installs it with set_input_focus_port(). Callers use the focus_*()
 *     wrappers. The entries are listed once, in ports/input_focus_port.def, which
 *     generates the struct, the wrappers, the unwired defaults (input_focus_port.c)
 *     and their test. Refactor pass 2, S15
 *     (docs/refactor-pass2/stage-15-ports-and-events.md).
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_PORT_INPUT_FOCUS_PORT_H
#define DK_PORT_INPUT_FOCUS_PORT_H

#include "bflib_basics.h"
#include "globals.h"
#include "port_check.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

struct InputFocusPort {
#define KFX_PORT_VOID(name, params, args) void (*name) params;
#define KFX_PORT_RET(ret, name, params, args, dflt) ret (*name) params;
#define KFX_PORT_VOIDX(name, params, args, stmt) KFX_PORT_VOID(name, params, args)
#define KFX_PORT_RETX(ret, name, params, args, dflt, stmt) KFX_PORT_RET(ret, name, params, args, dflt)
#define KFX_PORT_RETS(ret, name, params, args, body) KFX_PORT_RET(ret, name, params, args, 0)
#include "ports/input_focus_port.def"
#undef KFX_PORT_VOID
#undef KFX_PORT_RET
#undef KFX_PORT_VOIDX
#undef KFX_PORT_RETX
#undef KFX_PORT_RETS
};
KFX_ASSERT_PORT_TABLE(struct InputFocusPort);

/** The installed table; never NULL (the unwired defaults until main.cpp sets it). */
extern const struct InputFocusPort *input_focus_port;
/** The unwired defaults, for tests. */
extern const struct InputFocusPort input_focus_port_defaults;
void set_input_focus_port(const struct InputFocusPort *port);

#define KFX_PORT_VOID(name, params, args) static inline void focus_##name params { input_focus_port->name args; }
#define KFX_PORT_RET(ret, name, params, args, dflt) static inline ret focus_##name params { return input_focus_port->name args; }
#define KFX_PORT_VOIDX(name, params, args, stmt) KFX_PORT_VOID(name, params, args)
#define KFX_PORT_RETX(ret, name, params, args, dflt, stmt) KFX_PORT_RET(ret, name, params, args, dflt)
#define KFX_PORT_RETS(ret, name, params, args, body) KFX_PORT_RET(ret, name, params, args, 0)
#include "ports/input_focus_port.def"
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
