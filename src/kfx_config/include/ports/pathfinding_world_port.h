/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file pathfinding_world_port.h
 *     PathfindingWorldPort: the map, door and creature queries Ariadne (kfx_pathfinding) makes of kfx_sim's world.
 * @par Purpose:
 *     Provided by kfx_sim, which builds the table (pathfinding_world_port_impl);
 *     main.cpp installs it with set_pathfinding_world_port(). Callers use the world_*()
 *     wrappers. The entries are listed once, in ports/pathfinding_world_port.def, which
 *     generates the struct, the wrappers, the unwired defaults (pathfinding_world_port.c)
 *     and their test. Refactor pass 2, S15
 *     (docs/refactor-pass2/stage-15-ports-and-events.md).
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_PORT_PATHFINDING_WORLD_PORT_H
#define DK_PORT_PATHFINDING_WORLD_PORT_H

#include "bflib_basics.h"
#include "globals.h"
#include "port_check.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
struct Thing;
struct Coord3d;
struct Map;
struct SlabMap;
struct Navigation;
struct Ariadne;

struct PathfindingWorldPort {
#define KFX_PORT_VOID(name, params, args) void (*name) params;
#define KFX_PORT_RET(ret, name, params, args, dflt) ret (*name) params;
#define KFX_PORT_VOIDX(name, params, args, stmt) KFX_PORT_VOID(name, params, args)
#define KFX_PORT_RETX(ret, name, params, args, dflt, stmt) KFX_PORT_RET(ret, name, params, args, dflt)
#define KFX_PORT_RETS(ret, name, params, args, body) KFX_PORT_RET(ret, name, params, args, 0)
#include "ports/pathfinding_world_port.def"
#undef KFX_PORT_VOID
#undef KFX_PORT_RET
#undef KFX_PORT_VOIDX
#undef KFX_PORT_RETX
#undef KFX_PORT_RETS
};
KFX_ASSERT_PORT_TABLE(struct PathfindingWorldPort);

/** The installed table; never NULL (the unwired defaults until main.cpp sets it). */
extern const struct PathfindingWorldPort *pathfinding_world_port;
/** The unwired defaults, for tests. */
extern const struct PathfindingWorldPort pathfinding_world_port_defaults;
void set_pathfinding_world_port(const struct PathfindingWorldPort *port);

#define KFX_PORT_VOID(name, params, args) static inline void world_##name params { pathfinding_world_port->name args; }
#define KFX_PORT_RET(ret, name, params, args, dflt) static inline ret world_##name params { return pathfinding_world_port->name args; }
#define KFX_PORT_VOIDX(name, params, args, stmt) KFX_PORT_VOID(name, params, args)
#define KFX_PORT_RETX(ret, name, params, args, dflt, stmt) KFX_PORT_RET(ret, name, params, args, dflt)
#define KFX_PORT_RETS(ret, name, params, args, body) KFX_PORT_RET(ret, name, params, args, 0)
#include "ports/pathfinding_world_port.def"
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
