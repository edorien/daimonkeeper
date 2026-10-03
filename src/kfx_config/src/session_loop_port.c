/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file session_loop_port.c
 *     The unwired defaults of SessionLoopPort, generated from ports/session_loop_port.def.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "ports/session_loop_port.h"
#include "port_check.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
#define KFX_PORT_VOID(name, params, args) static void noop_##name params { KFX_UNWIRED("SessionLoopPort"); }
#define KFX_PORT_VOIDX(name, params, args, stmt) static void noop_##name params { KFX_UNWIRED("SessionLoopPort"); stmt }
#define KFX_PORT_RET(ret, name, params, args, dflt) static ret noop_##name params { KFX_UNWIRED("SessionLoopPort"); return dflt; }
#define KFX_PORT_RETX(ret, name, params, args, dflt, stmt) static ret noop_##name params { KFX_UNWIRED("SessionLoopPort"); stmt return dflt; }
#define KFX_PORT_RETS(ret, name, params, args, body) static ret noop_##name params { KFX_UNWIRED("SessionLoopPort"); body }
#include "ports/session_loop_port.def"
#undef KFX_PORT_VOID
#undef KFX_PORT_RET
#undef KFX_PORT_VOIDX
#undef KFX_PORT_RETX
#undef KFX_PORT_RETS

const struct SessionLoopPort session_loop_port_defaults = {
#define KFX_PORT_VOID(name, params, args) .name = &noop_##name,
#define KFX_PORT_RET(ret, name, params, args, dflt) .name = &noop_##name,
#define KFX_PORT_VOIDX(name, params, args, stmt) .name = &noop_##name,
#define KFX_PORT_RETX(ret, name, params, args, dflt, stmt) .name = &noop_##name,
#define KFX_PORT_RETS(ret, name, params, args, body) .name = &noop_##name,
#include "ports/session_loop_port.def"
#undef KFX_PORT_VOID
#undef KFX_PORT_RET
#undef KFX_PORT_VOIDX
#undef KFX_PORT_RETX
#undef KFX_PORT_RETS
};
const struct SessionLoopPort *session_loop_port = &session_loop_port_defaults;

void set_session_loop_port(const struct SessionLoopPort *port)
{
    session_loop_port = port ? port : &session_loop_port_defaults;
}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
