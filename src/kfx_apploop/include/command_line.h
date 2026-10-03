/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file command_line.h
 *     Header file for command_line.cpp.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_COMMAND_LINE_H
#define DK_COMMAND_LINE_H

#include "bflib_basics.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
/** Resets start_params to the defaults a plain launch uses. */
TbBool set_default_startup_parameters(void);
/** Parses the launch arguments into start_params and the runtime globals.
 * @return 1 on success; 0 for an unknown parameter, -1 for a malformed one. */
int64_t process_command_line(int64_t argc, char *argv[]);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
