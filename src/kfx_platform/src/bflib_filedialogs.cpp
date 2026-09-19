/******************************************************************************/
// Bullfrog Engine Emulation Library - for use to remake classic games like
// Syndicate Wars, Magic Carpet or Dungeon Keeper.
/******************************************************************************/
/** @file bflib_filedialogs.cpp
 *     See bflib_filedialogs.h.
 * @par Comment:
 *     None.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "bflib_filedialogs.h"

#include "tinyfiledialogs.h"

#include "post_inc.h"

/******************************************************************************/
const char *platform_pick_folder_dialog(const char *title, const char *default_path)
{
    return tinyfd_selectFolderDialog(title, default_path);
}

const char *platform_pick_open_file_dialog(const char *title, const char *default_path_and_file,
    const char *filter_pattern, const char *filter_description)
{
    if (filter_pattern != NULL)
    {
        const char *patterns[1] = { filter_pattern };
        return tinyfd_openFileDialog(title, default_path_and_file, 1, patterns, filter_description, 0);
    }
    return tinyfd_openFileDialog(title, default_path_and_file, 0, NULL, NULL, 0);
}
/******************************************************************************/
