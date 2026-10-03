/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file pathfinding_world_port.c
 *     The unwired defaults of PathfindingWorldPort, generated from ports/pathfinding_world_port.def.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "ports/pathfinding_world_port.h"
#include "port_check.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
#define KFX_PORT_VOID(name, params, args) static void noop_##name params { KFX_UNWIRED("PathfindingWorldPort"); }
#define KFX_PORT_VOIDX(name, params, args, stmt) static void noop_##name params { KFX_UNWIRED("PathfindingWorldPort"); stmt }
#define KFX_PORT_RET(ret, name, params, args, dflt) static ret noop_##name params { KFX_UNWIRED("PathfindingWorldPort"); return dflt; }
#define KFX_PORT_RETX(ret, name, params, args, dflt, stmt) static ret noop_##name params { KFX_UNWIRED("PathfindingWorldPort"); stmt return dflt; }
#define KFX_PORT_RETS(ret, name, params, args, body) static ret noop_##name params { KFX_UNWIRED("PathfindingWorldPort"); body }
#include "ports/pathfinding_world_port.def"
#undef KFX_PORT_VOID
#undef KFX_PORT_RET
#undef KFX_PORT_VOIDX
#undef KFX_PORT_RETX
#undef KFX_PORT_RETS

const struct PathfindingWorldPort pathfinding_world_port_defaults = {
#define KFX_PORT_VOID(name, params, args) .name = &noop_##name,
#define KFX_PORT_RET(ret, name, params, args, dflt) .name = &noop_##name,
#define KFX_PORT_VOIDX(name, params, args, stmt) .name = &noop_##name,
#define KFX_PORT_RETX(ret, name, params, args, dflt, stmt) .name = &noop_##name,
#define KFX_PORT_RETS(ret, name, params, args, body) .name = &noop_##name,
#include "ports/pathfinding_world_port.def"
#undef KFX_PORT_VOID
#undef KFX_PORT_RET
#undef KFX_PORT_VOIDX
#undef KFX_PORT_RETX
#undef KFX_PORT_RETS
};
const struct PathfindingWorldPort *pathfinding_world_port = &pathfinding_world_port_defaults;

void set_pathfinding_world_port(const struct PathfindingWorldPort *port)
{
    pathfinding_world_port = port ? port : &pathfinding_world_port_defaults;
}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
