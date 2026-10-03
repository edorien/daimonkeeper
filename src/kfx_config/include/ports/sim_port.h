/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file sim_port.h
 *     SimPort: the calls from kfx_config into kfx_sim: applying a changed config to the live game (rooms, doors, traps, creatures, research) and the sim state config parsing reads (map size, slab sets, level strings).
 * @par Purpose:
 *     Provided by kfx_sim, which builds the table (sim_port_impl);
 *     main.cpp installs it with set_sim_port(). Callers use the simport_*()
 *     wrappers. The entries are listed once, in ports/sim_port.def, which
 *     generates the struct, the wrappers, the unwired defaults (sim_port.c)
 *     and their test. Refactor pass 2, S15
 *     (docs/refactor-pass2/stage-15-ports-and-events.md).
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_PORT_SIM_PORT_H
#define DK_PORT_SIM_PORT_H

#include "bflib_basics.h"
#include "globals.h"
#include "port_check.h"
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
struct Thing;
struct SlabSet;
struct SlabObj;
struct SlabMap;
struct Dungeon;

struct SimPort {
#define KFX_PORT_VOID(name, params, args) void (*name) params;
#define KFX_PORT_RET(ret, name, params, args, dflt) ret (*name) params;
#define KFX_PORT_VOIDX(name, params, args, stmt) KFX_PORT_VOID(name, params, args)
#define KFX_PORT_RETX(ret, name, params, args, dflt, stmt) KFX_PORT_RET(ret, name, params, args, dflt)
#define KFX_PORT_RETS(ret, name, params, args, body) KFX_PORT_RET(ret, name, params, args, 0)
#include "ports/sim_port.def"
#undef KFX_PORT_VOID
#undef KFX_PORT_RET
#undef KFX_PORT_VOIDX
#undef KFX_PORT_RETX
#undef KFX_PORT_RETS
};
KFX_ASSERT_PORT_TABLE(struct SimPort);

/** The installed table; never NULL (the unwired defaults until main.cpp sets it). */
extern const struct SimPort *sim_port;
/** The unwired defaults, for tests. */
extern const struct SimPort sim_port_defaults;
void set_sim_port(const struct SimPort *port);

#define KFX_PORT_VOID(name, params, args) static inline void simport_##name params { sim_port->name args; }
#define KFX_PORT_RET(ret, name, params, args, dflt) static inline ret simport_##name params { return sim_port->name args; }
#define KFX_PORT_VOIDX(name, params, args, stmt) KFX_PORT_VOID(name, params, args)
#define KFX_PORT_RETX(ret, name, params, args, dflt, stmt) KFX_PORT_RET(ret, name, params, args, dflt)
#define KFX_PORT_RETS(ret, name, params, args, body) KFX_PORT_RET(ret, name, params, args, 0)
#include "ports/sim_port.def"
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
