/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file render_port.h
 *     RenderPort: the calls into kfx_render: palettes and the eye lens, the engine view and window, the cursor-tag boxes, texture maps, animation and custom sprites.
 * @par Purpose:
 *     Provided by kfx_render, which builds the table (render_port_impl);
 *     main.cpp installs it with set_render_port(). Callers use the render_*()
 *     wrappers. The entries are listed once, in ports/render_port.def, which
 *     generates the struct, the wrappers, the unwired defaults (render_port.c)
 *     and their test. Refactor pass 2, S15
 *     (docs/refactor-pass2/stage-15-ports-and-events.md).
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_PORT_RENDER_PORT_H
#define DK_PORT_RENDER_PORT_H

#include "bflib_basics.h"
#include "globals.h"
#include "port_check.h"
#include "bflib_video.h"
#include "bflib_netsp.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
struct PlayerInfo;
struct Packet;
struct Camera;
struct ObjectConfigStats;
struct TbSprite;
struct RoomSpace;
struct Thing;

struct RenderPort {
#define KFX_PORT_VOID(name, params, args) void (*name) params;
#define KFX_PORT_RET(ret, name, params, args, dflt) ret (*name) params;
#define KFX_PORT_VOIDX(name, params, args, stmt) KFX_PORT_VOID(name, params, args)
#define KFX_PORT_RETX(ret, name, params, args, dflt, stmt) KFX_PORT_RET(ret, name, params, args, dflt)
#define KFX_PORT_RETS(ret, name, params, args, body) KFX_PORT_RET(ret, name, params, args, 0)
#include "ports/render_port.def"
#undef KFX_PORT_VOID
#undef KFX_PORT_RET
#undef KFX_PORT_VOIDX
#undef KFX_PORT_RETX
#undef KFX_PORT_RETS
};
KFX_ASSERT_PORT_TABLE(struct RenderPort);

/** The installed table; never NULL (the unwired defaults until main.cpp sets it). */
extern const struct RenderPort *render_port;
/** The unwired defaults, for tests. */
extern const struct RenderPort render_port_defaults;
void set_render_port(const struct RenderPort *port);

#define KFX_PORT_VOID(name, params, args) static inline void render_##name params { render_port->name args; }
#define KFX_PORT_RET(ret, name, params, args, dflt) static inline ret render_##name params { return render_port->name args; }
#define KFX_PORT_VOIDX(name, params, args, stmt) KFX_PORT_VOID(name, params, args)
#define KFX_PORT_RETX(ret, name, params, args, dflt, stmt) KFX_PORT_RET(ret, name, params, args, dflt)
#define KFX_PORT_RETS(ret, name, params, args, body) KFX_PORT_RET(ret, name, params, args, 0)
#include "ports/render_port.def"
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
