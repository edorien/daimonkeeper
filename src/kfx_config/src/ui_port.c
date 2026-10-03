/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file ui_port.c
 *     The unwired defaults of UiPort, generated from ports/ui_port.def.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "ports/ui_port.h"
#include "port_check.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
#define KFX_PORT_VOID(name, params, args) static void noop_##name params { KFX_UNWIRED("UiPort"); }
#define KFX_PORT_VOIDX(name, params, args, stmt) static void noop_##name params { KFX_UNWIRED("UiPort"); stmt }
#define KFX_PORT_RET(ret, name, params, args, dflt) static ret noop_##name params { KFX_UNWIRED("UiPort"); return dflt; }
#define KFX_PORT_RETX(ret, name, params, args, dflt, stmt) static ret noop_##name params { KFX_UNWIRED("UiPort"); stmt return dflt; }
#define KFX_PORT_RETS(ret, name, params, args, body) static ret noop_##name params { KFX_UNWIRED("UiPort"); body }
#include "ports/ui_port.def"
#undef KFX_PORT_VOID
#undef KFX_PORT_RET
#undef KFX_PORT_VOIDX
#undef KFX_PORT_RETX
#undef KFX_PORT_RETS

const struct UiPort ui_port_defaults = {
#define KFX_PORT_VOID(name, params, args) .name = &noop_##name,
#define KFX_PORT_RET(ret, name, params, args, dflt) .name = &noop_##name,
#define KFX_PORT_VOIDX(name, params, args, stmt) .name = &noop_##name,
#define KFX_PORT_RETX(ret, name, params, args, dflt, stmt) .name = &noop_##name,
#define KFX_PORT_RETS(ret, name, params, args, body) .name = &noop_##name,
#include "ports/ui_port.def"
#undef KFX_PORT_VOID
#undef KFX_PORT_RET
#undef KFX_PORT_VOIDX
#undef KFX_PORT_RETX
#undef KFX_PORT_RETS
};
const struct UiPort *ui_port = &ui_port_defaults;

void set_ui_port(const struct UiPort *port)
{
    ui_port = port ? port : &ui_port_defaults;
}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
