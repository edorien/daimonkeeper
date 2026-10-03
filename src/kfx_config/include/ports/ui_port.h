/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file ui_port.h
 *     UiPort: the calls into kfx_frontend: menus and panels, messages and message boxes, tooltips, the event buttons, objectives, cheat menus, high score, the frontend state, and the network session screens.
 * @par Purpose:
 *     Provided by kfx_frontend, which builds the table (ui_port_impl);
 *     main.cpp installs it with set_ui_port(). Callers use the ui_*()
 *     wrappers. The entries are listed once, in ports/ui_port.def, which
 *     generates the struct, the wrappers, the unwired defaults (ui_port.c)
 *     and their test. Refactor pass 2, S15
 *     (docs/refactor-pass2/stage-15-ports-and-events.md).
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_PORT_UI_PORT_H
#define DK_PORT_UI_PORT_H

#include "bflib_basics.h"
#include "globals.h"
#include "port_check.h"
#include "compiler_compat.h"
#include "game_time.h"
#include "bflib_sound.h"
#include "bflib_keybrd.h"
#include "bflib_netsp.h"
#include "bflib_video.h"
#include <stddef.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
struct Thing;
struct SpeechRef;
struct PlayerInfo;
struct Packet;
struct RoomSpace;
struct Camera;
struct EventTypeInfo;
struct GuiBox;
struct GuiBoxOption;

/** UiPort's local_view_transition: the local player's view changes that
 *  kfx_sim's player instances report, in the order they happen. */
enum LocalViewTransition {
    LVTr_LevelStart = 0,          /**< init_player(): minimap, roomspace, engine window */
    LVTr_PassengerBegin,          /**< starting to ride a creature as a passenger (palette fade in) */
    LVTr_PossessionLeave,         /**< leaving a creature under direct control */
    LVTr_PassengerLeave,          /**< leaving a creature ridden as a passenger */
    LVTr_ControlledCreatureDied,  /**< the possessed creature died */
    LVTr_MapFadeInBegin,          /**< fading to the parchment map */
    LVTr_MapShown,                /**< the map is up */
    LVTr_MapFadeOutBegin,         /**< fading back from the map */
    LVTr_MapHidden,               /**< back from the map */
};

struct UiPort {
#define KFX_PORT_VOID(name, params, args) void (*name) params;
#define KFX_PORT_RET(ret, name, params, args, dflt) ret (*name) params;
#define KFX_PORT_VOIDX(name, params, args, stmt) KFX_PORT_VOID(name, params, args)
#define KFX_PORT_RETX(ret, name, params, args, dflt, stmt) KFX_PORT_RET(ret, name, params, args, dflt)
#define KFX_PORT_RETS(ret, name, params, args, body) KFX_PORT_RET(ret, name, params, args, 0)
#include "ports/ui_port.def"
#undef KFX_PORT_VOID
#undef KFX_PORT_RET
#undef KFX_PORT_VOIDX
#undef KFX_PORT_RETX
#undef KFX_PORT_RETS
};
KFX_ASSERT_PORT_TABLE(struct UiPort);

/** The installed table; never NULL (the unwired defaults until main.cpp sets it). */
extern const struct UiPort *ui_port;
/** The unwired defaults, for tests. */
extern const struct UiPort ui_port_defaults;
void set_ui_port(const struct UiPort *port);

#define KFX_PORT_VOID(name, params, args) static inline void ui_##name params { ui_port->name args; }
#define KFX_PORT_RET(ret, name, params, args, dflt) static inline ret ui_##name params { return ui_port->name args; }
#define KFX_PORT_VOIDX(name, params, args, stmt) KFX_PORT_VOID(name, params, args)
#define KFX_PORT_RETX(ret, name, params, args, dflt, stmt) KFX_PORT_RET(ret, name, params, args, dflt)
#define KFX_PORT_RETS(ret, name, params, args, body) KFX_PORT_RET(ret, name, params, args, 0)
#include "ports/ui_port.def"
#undef KFX_PORT_VOID
#undef KFX_PORT_RET
#undef KFX_PORT_VOIDX
#undef KFX_PORT_RETX
#undef KFX_PORT_RETS

/** The printf-style form of ui_message_add_vfmt(). */
static inline void ui_message_add_fmt(char msg_type, int64_t idx, const char *fmt_str, ...) KFX_PRINTF_FORMAT(3, 4);
static inline void ui_message_add_fmt(char msg_type, int64_t idx, const char *fmt_str, ...)
{
    va_list ap;
    va_start(ap, fmt_str);
    ui_port->message_add_vfmt(msg_type, idx, fmt_str, ap);
    va_end(ap);
}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
