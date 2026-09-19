/******************************************************************************/
// Bullfrog Engine Emulation Library - for use to remake classic games like
// Syndicate Wars, Magic Carpet or Dungeon Keeper.
/******************************************************************************/
/** @file bflib_filedialogs.h
 *     Header file for bflib_filedialogs.cpp.
 * @par Purpose:
 *     Thin wrapper over the vendored tinyfiledialogs (deps/tinyfiledialogs)
 *     for native OS folder/file pickers -- used by the editor's Open/Save
 *     As dialogs (docs/refactor/editor/phase3/03-slice4-file-dialogs.md) in
 *     place of a free-text path field.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef BFLIB_FILEDIALOGS_H
#define BFLIB_FILEDIALOGS_H

#include "bflib_basics.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

// Opens a native "select folder" dialog. Returns a pointer to a buffer
// owned by tinyfiledialogs itself (reused across calls, same "copy it
// immediately, don't hold the pointer" convention as prepare_file_fmtpath()
// elsewhere in this codebase) -- or NULL if the user cancelled.
// `default_path` may be NULL/"" for no starting suggestion.
const char *platform_pick_folder_dialog(const char *title, const char *default_path);

// Opens a native "open file" dialog filtered to one pattern (e.g. "*.cfg").
// Same buffer-ownership/cancel convention as platform_pick_folder_dialog()
// above. `default_path_and_file` may be NULL/""; `filter_pattern`/
// `filter_description` may be NULL for no filter.
const char *platform_pick_open_file_dialog(const char *title, const char *default_path_and_file,
    const char *filter_pattern, const char *filter_description);

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
