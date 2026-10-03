/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file display_host_port.h
 *     DisplayHostPort: the calls kfx_platform's renderer and input make into the frontend: the ImGui context kfx_frontend owns, the UI scale values, and the slab-background fallback drawing.
 * @par Purpose:
 *     Provided by kfx_frontend, which builds the table (display_host_port_impl);
 *     main.cpp installs it with set_display_host_port(). Callers use the display_*()
 *     wrappers. The entries are listed once, in ports/display_host_port.def, which
 *     generates the struct, the wrappers, the unwired defaults (display_host_port.c)
 *     and their test. Refactor pass 2, S15
 *     (docs/refactor-pass2/stage-15-ports-and-events.md).
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_PORT_DISPLAY_HOST_PORT_H
#define DK_PORT_DISPLAY_HOST_PORT_H

#include "bflib_basics.h"
#include "globals.h"
#include "port_check.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
struct SDL_Window;
union SDL_Event;
struct SDL_Renderer;
struct VideoScaleValues;

struct DisplayHostPort {
#define KFX_PORT_VOID(name, params, args) void (*name) params;
#define KFX_PORT_RET(ret, name, params, args, dflt) ret (*name) params;
#define KFX_PORT_VOIDX(name, params, args, stmt) KFX_PORT_VOID(name, params, args)
#define KFX_PORT_RETX(ret, name, params, args, dflt, stmt) KFX_PORT_RET(ret, name, params, args, dflt)
#define KFX_PORT_RETS(ret, name, params, args, body) KFX_PORT_RET(ret, name, params, args, 0)
#include "ports/display_host_port.def"
#undef KFX_PORT_VOID
#undef KFX_PORT_RET
#undef KFX_PORT_VOIDX
#undef KFX_PORT_RETX
#undef KFX_PORT_RETS
};
KFX_ASSERT_PORT_TABLE(struct DisplayHostPort);

/** The installed table; never NULL (the unwired defaults until main.cpp sets it). */
extern const struct DisplayHostPort *display_host_port;
/** The unwired defaults, for tests. */
extern const struct DisplayHostPort display_host_port_defaults;
void set_display_host_port(const struct DisplayHostPort *port);

#define KFX_PORT_VOID(name, params, args) static inline void display_##name params { display_host_port->name args; }
#define KFX_PORT_RET(ret, name, params, args, dflt) static inline ret display_##name params { return display_host_port->name args; }
#define KFX_PORT_VOIDX(name, params, args, stmt) KFX_PORT_VOID(name, params, args)
#define KFX_PORT_RETX(ret, name, params, args, dflt, stmt) KFX_PORT_RET(ret, name, params, args, dflt)
#define KFX_PORT_RETS(ret, name, params, args, body) KFX_PORT_RET(ret, name, params, args, 0)
#include "ports/display_host_port.def"
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
