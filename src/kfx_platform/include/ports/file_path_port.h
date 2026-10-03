/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file file_path_port.h
 *     FilePathPort: resolving game data file paths (install directories, mods, per-level map archives), which kfx_config owns, for kfx_platform's sound, input and zip code.
 * @par Purpose:
 *     Provided by kfx_config, which builds the table (file_path_port_impl);
 *     main.cpp installs it with set_file_path_port(). Callers use the filepath_*()
 *     wrappers. The entries are listed once, in ports/file_path_port.def, which
 *     generates the struct, the wrappers, the unwired defaults (file_path_port.c)
 *     and their test. Refactor pass 2, S15
 *     (docs/refactor-pass2/stage-15-ports-and-events.md).
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_PORT_FILE_PATH_PORT_H
#define DK_PORT_FILE_PATH_PORT_H

#include "bflib_basics.h"
#include "globals.h"
#include "port_check.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

struct FilePathPort {
#define KFX_PORT_VOID(name, params, args) void (*name) params;
#define KFX_PORT_RET(ret, name, params, args, dflt) ret (*name) params;
#define KFX_PORT_VOIDX(name, params, args, stmt) KFX_PORT_VOID(name, params, args)
#define KFX_PORT_RETX(ret, name, params, args, dflt, stmt) KFX_PORT_RET(ret, name, params, args, dflt)
#define KFX_PORT_RETS(ret, name, params, args, body) KFX_PORT_RET(ret, name, params, args, 0)
#include "ports/file_path_port.def"
#undef KFX_PORT_VOID
#undef KFX_PORT_RET
#undef KFX_PORT_VOIDX
#undef KFX_PORT_RETX
#undef KFX_PORT_RETS
};
KFX_ASSERT_PORT_TABLE(struct FilePathPort);

/** The installed table; never NULL (the unwired defaults until main.cpp sets it). */
extern const struct FilePathPort *file_path_port;
/** The unwired defaults, for tests. */
extern const struct FilePathPort file_path_port_defaults;
void set_file_path_port(const struct FilePathPort *port);

#define KFX_PORT_VOID(name, params, args) static inline void filepath_##name params { file_path_port->name args; }
#define KFX_PORT_RET(ret, name, params, args, dflt) static inline ret filepath_##name params { return file_path_port->name args; }
#define KFX_PORT_VOIDX(name, params, args, stmt) KFX_PORT_VOID(name, params, args)
#define KFX_PORT_RETX(ret, name, params, args, dflt, stmt) KFX_PORT_RET(ret, name, params, args, dflt)
#define KFX_PORT_RETS(ret, name, params, args, body) KFX_PORT_RET(ret, name, params, args, 0)
#include "ports/file_path_port.def"
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
