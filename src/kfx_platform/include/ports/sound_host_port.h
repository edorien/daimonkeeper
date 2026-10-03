/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file sound_host_port.h
 *     SoundHostPort: the game state kfx_platform's sound and music code reads (music track, creature sounds, mod lists, random seeds) and the audio set-up it triggers, provided by kfx_game.
 * @par Purpose:
 *     Provided by kfx_game, which builds the table (sound_host_port_impl);
 *     main.cpp installs it with set_sound_host_port(). Callers use the soundhost_*()
 *     wrappers. The entries are listed once, in ports/sound_host_port.def, which
 *     generates the struct, the wrappers, the unwired defaults (sound_host_port.c)
 *     and their test. Refactor pass 2, S15
 *     (docs/refactor-pass2/stage-15-ports-and-events.md).
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_PORT_SOUND_HOST_PORT_H
#define DK_PORT_SOUND_HOST_PORT_H

#include "bflib_basics.h"
#include "globals.h"
#include "port_check.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
struct CreatureSounds;
struct ModConfigItem;
struct NamedCommand;

struct SoundHostPort {
#define KFX_PORT_VOID(name, params, args) void (*name) params;
#define KFX_PORT_RET(ret, name, params, args, dflt) ret (*name) params;
#define KFX_PORT_VOIDX(name, params, args, stmt) KFX_PORT_VOID(name, params, args)
#define KFX_PORT_RETX(ret, name, params, args, dflt, stmt) KFX_PORT_RET(ret, name, params, args, dflt)
#define KFX_PORT_RETS(ret, name, params, args, body) KFX_PORT_RET(ret, name, params, args, 0)
#include "ports/sound_host_port.def"
#undef KFX_PORT_VOID
#undef KFX_PORT_RET
#undef KFX_PORT_VOIDX
#undef KFX_PORT_RETX
#undef KFX_PORT_RETS
};
KFX_ASSERT_PORT_TABLE(struct SoundHostPort);

/** The installed table; never NULL (the unwired defaults until main.cpp sets it). */
extern const struct SoundHostPort *sound_host_port;
/** The unwired defaults, for tests. */
extern const struct SoundHostPort sound_host_port_defaults;
void set_sound_host_port(const struct SoundHostPort *port);

#define KFX_PORT_VOID(name, params, args) static inline void soundhost_##name params { sound_host_port->name args; }
#define KFX_PORT_RET(ret, name, params, args, dflt) static inline ret soundhost_##name params { return sound_host_port->name args; }
#define KFX_PORT_VOIDX(name, params, args, stmt) KFX_PORT_VOID(name, params, args)
#define KFX_PORT_RETX(ret, name, params, args, dflt, stmt) KFX_PORT_RET(ret, name, params, args, dflt)
#define KFX_PORT_RETS(ret, name, params, args, body) KFX_PORT_RET(ret, name, params, args, 0)
#include "ports/sound_host_port.def"
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
