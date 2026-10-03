/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file game_port.h
 *     GamePort: the calls into kfx_game: level flow (dungeon destroyed, bonus levels, transferred creatures, the intralevel data), the resync of kfx_game's state, the console command, the script string helpers, and the heap manager.
 * @par Purpose:
 *     Provided by kfx_game, which builds the table (game_port_impl);
 *     main.cpp installs it with set_game_port(). Callers use the game_*()
 *     wrappers. The entries are listed once, in ports/game_port.def, which
 *     generates the struct, the wrappers, the unwired defaults (game_port.c)
 *     and their test. Refactor pass 2, S15
 *     (docs/refactor-pass2/stage-15-ports-and-events.md).
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_PORT_GAME_PORT_H
#define DK_PORT_GAME_PORT_H

#include "bflib_basics.h"
#include "globals.h"
#include "port_check.h"
#include "bflib_fileio.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
struct Thing;
struct PlayerInfo;
struct Dungeon;
struct CreatureStorage;

struct GamePort {
#define KFX_PORT_VOID(name, params, args) void (*name) params;
#define KFX_PORT_RET(ret, name, params, args, dflt) ret (*name) params;
#define KFX_PORT_VOIDX(name, params, args, stmt) KFX_PORT_VOID(name, params, args)
#define KFX_PORT_RETX(ret, name, params, args, dflt, stmt) KFX_PORT_RET(ret, name, params, args, dflt)
#define KFX_PORT_RETS(ret, name, params, args, body) KFX_PORT_RET(ret, name, params, args, 0)
#include "ports/game_port.def"
#undef KFX_PORT_VOID
#undef KFX_PORT_RET
#undef KFX_PORT_VOIDX
#undef KFX_PORT_RETX
#undef KFX_PORT_RETS
};
KFX_ASSERT_PORT_TABLE(struct GamePort);

/** The installed table; never NULL (the unwired defaults until main.cpp sets it). */
extern const struct GamePort *game_port;
/** The unwired defaults, for tests. */
extern const struct GamePort game_port_defaults;
void set_game_port(const struct GamePort *port);

#define KFX_PORT_VOID(name, params, args) static inline void game_##name params { game_port->name args; }
#define KFX_PORT_RET(ret, name, params, args, dflt) static inline ret game_##name params { return game_port->name args; }
#define KFX_PORT_VOIDX(name, params, args, stmt) KFX_PORT_VOID(name, params, args)
#define KFX_PORT_RETX(ret, name, params, args, dflt, stmt) KFX_PORT_RET(ret, name, params, args, dflt)
#define KFX_PORT_RETS(ret, name, params, args, body) KFX_PORT_RET(ret, name, params, args, 0)
#include "ports/game_port.def"
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
