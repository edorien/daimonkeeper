/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_port.h
 *     EditorPort: the calls into kfx_editor: opening and querying the level-editor session, the content tools, and the undo journal the packet handlers record into.
 * @par Purpose:
 *     Provided by kfx_editor, which builds the table (editor_port_impl);
 *     main.cpp installs it with set_editor_port(). Callers use the editorport_*()
 *     wrappers. The entries are listed once, in ports/editor_port.def, which
 *     generates the struct, the wrappers, the unwired defaults (editor_port.c)
 *     and their test. Refactor pass 2, S15
 *     (docs/refactor-pass2/stage-15-ports-and-events.md).
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_PORT_EDITOR_PORT_H
#define DK_PORT_EDITOR_PORT_H

#include "bflib_basics.h"
#include "globals.h"
#include "port_check.h"
#include "editor_types.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

struct EditorPort {
#define KFX_PORT_VOID(name, params, args) void (*name) params;
#define KFX_PORT_RET(ret, name, params, args, dflt) ret (*name) params;
#define KFX_PORT_VOIDX(name, params, args, stmt) KFX_PORT_VOID(name, params, args)
#define KFX_PORT_RETX(ret, name, params, args, dflt, stmt) KFX_PORT_RET(ret, name, params, args, dflt)
#define KFX_PORT_RETS(ret, name, params, args, body) KFX_PORT_RET(ret, name, params, args, 0)
#include "ports/editor_port.def"
#undef KFX_PORT_VOID
#undef KFX_PORT_RET
#undef KFX_PORT_VOIDX
#undef KFX_PORT_RETX
#undef KFX_PORT_RETS
};
KFX_ASSERT_PORT_TABLE(struct EditorPort);

/** The installed table; never NULL (the unwired defaults until main.cpp sets it). */
extern const struct EditorPort *editor_port;
/** The unwired defaults, for tests. */
extern const struct EditorPort editor_port_defaults;
void set_editor_port(const struct EditorPort *port);

#define KFX_PORT_VOID(name, params, args) static inline void editorport_##name params { editor_port->name args; }
#define KFX_PORT_RET(ret, name, params, args, dflt) static inline ret editorport_##name params { return editor_port->name args; }
#define KFX_PORT_VOIDX(name, params, args, stmt) KFX_PORT_VOID(name, params, args)
#define KFX_PORT_RETX(ret, name, params, args, dflt, stmt) KFX_PORT_RET(ret, name, params, args, dflt)
#define KFX_PORT_RETS(ret, name, params, args, body) KFX_PORT_RET(ret, name, params, args, 0)
#include "ports/editor_port.def"
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
