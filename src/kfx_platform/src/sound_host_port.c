/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file sound_host_port.c
 *     The unwired defaults of SoundHostPort, generated from ports/sound_host_port.def.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "ports/sound_host_port.h"
#include "port_check.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
#define KFX_PORT_VOID(name, params, args) static void noop_##name params { KFX_UNWIRED("SoundHostPort"); }
#define KFX_PORT_VOIDX(name, params, args, stmt) static void noop_##name params { KFX_UNWIRED("SoundHostPort"); stmt }
#define KFX_PORT_RET(ret, name, params, args, dflt) static ret noop_##name params { KFX_UNWIRED("SoundHostPort"); return dflt; }
#define KFX_PORT_RETX(ret, name, params, args, dflt, stmt) static ret noop_##name params { KFX_UNWIRED("SoundHostPort"); stmt return dflt; }
#define KFX_PORT_RETS(ret, name, params, args, body) static ret noop_##name params { KFX_UNWIRED("SoundHostPort"); body }
#include "ports/sound_host_port.def"
#undef KFX_PORT_VOID
#undef KFX_PORT_RET
#undef KFX_PORT_VOIDX
#undef KFX_PORT_RETX
#undef KFX_PORT_RETS

const struct SoundHostPort sound_host_port_defaults = {
#define KFX_PORT_VOID(name, params, args) .name = &noop_##name,
#define KFX_PORT_RET(ret, name, params, args, dflt) .name = &noop_##name,
#define KFX_PORT_VOIDX(name, params, args, stmt) .name = &noop_##name,
#define KFX_PORT_RETX(ret, name, params, args, dflt, stmt) .name = &noop_##name,
#define KFX_PORT_RETS(ret, name, params, args, body) .name = &noop_##name,
#include "ports/sound_host_port.def"
#undef KFX_PORT_VOID
#undef KFX_PORT_RET
#undef KFX_PORT_VOIDX
#undef KFX_PORT_RETX
#undef KFX_PORT_RETS
};
const struct SoundHostPort *sound_host_port = &sound_host_port_defaults;

void set_sound_host_port(const struct SoundHostPort *port)
{
    sound_host_port = port ? port : &sound_host_port_defaults;
}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
