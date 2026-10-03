/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file audio_port.h
 *     AudioFeedbackPort: the game's sound feedback: spoken and queued sound messages (kfx_frontend's gui_soundmsgs), thing samples and ambient sounds (kfx_game's sounds.c).
 * @par Purpose:
 *     Provided by kfx_frontend, which builds the table (audio_port_impl);
 *     main.cpp installs it with set_audio_port(). Callers use the audio_*()
 *     wrappers. The entries are listed once, in ports/audio_port.def, which
 *     generates the struct, the wrappers, the unwired defaults (audio_port.c)
 *     and their test. Refactor pass 2, S15
 *     (docs/refactor-pass2/stage-15-ports-and-events.md).
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_PORT_AUDIO_PORT_H
#define DK_PORT_AUDIO_PORT_H

#include "bflib_basics.h"
#include "globals.h"
#include "port_check.h"
#include "bflib_sound.h"
#include "game_time.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
struct Thing;
struct SpeechRef;
struct Coord3d;

struct AudioFeedbackPort {
#define KFX_PORT_VOID(name, params, args) void (*name) params;
#define KFX_PORT_RET(ret, name, params, args, dflt) ret (*name) params;
#define KFX_PORT_VOIDX(name, params, args, stmt) KFX_PORT_VOID(name, params, args)
#define KFX_PORT_RETX(ret, name, params, args, dflt, stmt) KFX_PORT_RET(ret, name, params, args, dflt)
#define KFX_PORT_RETS(ret, name, params, args, body) KFX_PORT_RET(ret, name, params, args, 0)
#include "ports/audio_port.def"
#undef KFX_PORT_VOID
#undef KFX_PORT_RET
#undef KFX_PORT_VOIDX
#undef KFX_PORT_RETX
#undef KFX_PORT_RETS
};
KFX_ASSERT_PORT_TABLE(struct AudioFeedbackPort);

/** The installed table; never NULL (the unwired defaults until main.cpp sets it). */
extern const struct AudioFeedbackPort *audio_port;
/** The unwired defaults, for tests. */
extern const struct AudioFeedbackPort audio_port_defaults;
void set_audio_port(const struct AudioFeedbackPort *port);

#define KFX_PORT_VOID(name, params, args) static inline void audio_##name params { audio_port->name args; }
#define KFX_PORT_RET(ret, name, params, args, dflt) static inline ret audio_##name params { return audio_port->name args; }
#define KFX_PORT_VOIDX(name, params, args, stmt) KFX_PORT_VOID(name, params, args)
#define KFX_PORT_RETX(ret, name, params, args, dflt, stmt) KFX_PORT_RET(ret, name, params, args, dflt)
#define KFX_PORT_RETS(ret, name, params, args, body) KFX_PORT_RET(ret, name, params, args, 0)
#include "ports/audio_port.def"
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
