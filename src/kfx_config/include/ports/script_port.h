/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file script_port.h
 *     ScriptPort: the calls into kfx_script: Lua event triggers, Lua-registered functions, the HTTP API events, script lifecycle and the Lua resync payload.
 * @par Purpose:
 *     Provided by kfx_script, which builds the table (script_port_impl);
 *     main.cpp installs it with set_script_port(). Callers use the script_*()
 *     wrappers. The entries are listed once, in ports/script_port.def, which
 *     generates the struct, the wrappers, the unwired defaults (script_port.c)
 *     and their test. Refactor pass 2, S15
 *     (docs/refactor-pass2/stage-15-ports-and-events.md).
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_PORT_SCRIPT_PORT_H
#define DK_PORT_SCRIPT_PORT_H

#include "bflib_basics.h"
#include "globals.h"
#include "port_check.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
struct Thing;
struct Room;
struct ApiEventData;
struct NamedCommand;

// Mirrors api.h's enum ApiEventDataType/struct ApiEventData (kfx_script,
// above kfx_sim) -- kfx_sim's actionpt.c is the lowest-ranked real
// producer of event data payloads, so the small vocabulary type lives
// here alongside the port entry that consumes it (api_event_with_data), same "moved to the
// lowest layer that needs it" treatment as this codebase's other small
// enums (e.g. globals.h's MessageTypes).
enum ApiEventDataType {
    API_EVENT_DATA_INT32,
    API_EVENT_DATA_UINT32,
    API_EVENT_DATA_INT64,
    API_EVENT_DATA_UINT64,
    API_EVENT_DATA_FLOAT,
    API_EVENT_DATA_DOUBLE,
    API_EVENT_DATA_BOOL,
    API_EVENT_DATA_STRING,
};

struct ApiEventData {
    const char *name;
    enum ApiEventDataType type;
    union {
        int64_t int32_value;
        uint64_t uint32_value;
        int64_t int64_value;
        uint64_t uint64_value;
        double float_value;
        double double_value;
        bool bool_value;
        const char *string_value;
    } value;
};

struct ScriptPort {
#define KFX_PORT_VOID(name, params, args) void (*name) params;
#define KFX_PORT_RET(ret, name, params, args, dflt) ret (*name) params;
#define KFX_PORT_VOIDX(name, params, args, stmt) KFX_PORT_VOID(name, params, args)
#define KFX_PORT_RETX(ret, name, params, args, dflt, stmt) KFX_PORT_RET(ret, name, params, args, dflt)
#define KFX_PORT_RETS(ret, name, params, args, body) KFX_PORT_RET(ret, name, params, args, 0)
#include "ports/script_port.def"
#undef KFX_PORT_VOID
#undef KFX_PORT_RET
#undef KFX_PORT_VOIDX
#undef KFX_PORT_RETX
#undef KFX_PORT_RETS
};
KFX_ASSERT_PORT_TABLE(struct ScriptPort);

/** The installed table; never NULL (the unwired defaults until main.cpp sets it). */
extern const struct ScriptPort *script_port;
/** The unwired defaults, for tests. */
extern const struct ScriptPort script_port_defaults;
void set_script_port(const struct ScriptPort *port);

#define KFX_PORT_VOID(name, params, args) static inline void script_##name params { script_port->name args; }
#define KFX_PORT_RET(ret, name, params, args, dflt) static inline ret script_##name params { return script_port->name args; }
#define KFX_PORT_VOIDX(name, params, args, stmt) KFX_PORT_VOID(name, params, args)
#define KFX_PORT_RETX(ret, name, params, args, dflt, stmt) KFX_PORT_RET(ret, name, params, args, dflt)
#define KFX_PORT_RETS(ret, name, params, args, body) KFX_PORT_RET(ret, name, params, args, 0)
#include "ports/script_port.def"
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
