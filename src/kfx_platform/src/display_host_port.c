/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file display_host_port.c
 *     The unwired defaults of DisplayHostPort, generated from ports/display_host_port.def.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "ports/display_host_port.h"
#include "port_check.h"
#include "bflib_video.h" // struct VideoScaleValues, for the unwired default
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
static const struct VideoScaleValues *unscaled_video_scale_values(void)
{
    static const struct VideoScaleValues unscaled = {16, 16, 16, 16, 16};
    return &unscaled;
}

#define KFX_PORT_VOID(name, params, args) static void noop_##name params { KFX_UNWIRED("DisplayHostPort"); }
#define KFX_PORT_VOIDX(name, params, args, stmt) static void noop_##name params { KFX_UNWIRED("DisplayHostPort"); stmt }
#define KFX_PORT_RET(ret, name, params, args, dflt) static ret noop_##name params { KFX_UNWIRED("DisplayHostPort"); return dflt; }
#define KFX_PORT_RETX(ret, name, params, args, dflt, stmt) static ret noop_##name params { KFX_UNWIRED("DisplayHostPort"); stmt return dflt; }
#define KFX_PORT_RETS(ret, name, params, args, body) static ret noop_##name params { KFX_UNWIRED("DisplayHostPort"); body }
#include "ports/display_host_port.def"
#undef KFX_PORT_VOID
#undef KFX_PORT_RET
#undef KFX_PORT_VOIDX
#undef KFX_PORT_RETX
#undef KFX_PORT_RETS

const struct DisplayHostPort display_host_port_defaults = {
#define KFX_PORT_VOID(name, params, args) .name = &noop_##name,
#define KFX_PORT_RET(ret, name, params, args, dflt) .name = &noop_##name,
#define KFX_PORT_VOIDX(name, params, args, stmt) .name = &noop_##name,
#define KFX_PORT_RETX(ret, name, params, args, dflt, stmt) .name = &noop_##name,
#define KFX_PORT_RETS(ret, name, params, args, body) .name = &noop_##name,
#include "ports/display_host_port.def"
#undef KFX_PORT_VOID
#undef KFX_PORT_RET
#undef KFX_PORT_VOIDX
#undef KFX_PORT_RETX
#undef KFX_PORT_RETS
};
const struct DisplayHostPort *display_host_port = &display_host_port_defaults;

void set_display_host_port(const struct DisplayHostPort *port)
{
    display_host_port = port ? port : &display_host_port_defaults;
}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
