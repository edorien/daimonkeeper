/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file file_path_port_impl.c
 *     kfx_config's FilePathPort table: the implementations next to their
 *     provider, installed by main.cpp's wire_ports(). Refactor pass 2, S15.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "file_path_port_impl.h"
#include "config.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
// Wrapper registered with ports/file_path_port.h's FilePathPort; resolves the
// already-formatted "mapNNNNN.zip" filename to a full path.
static char *prepare_map_zip_path(LevelNumber lvnum, const char *fname)
{
    return prepare_file_path(get_level_fgroup(lvnum), fname);
}


const struct FilePathPort kfx_config_file_path_port = {
    .prepare_file_path = &prepare_file_path,
    .prepare_file_path_mod = &prepare_file_path_mod,
    .prepare_file_path_buf = &prepare_file_path_buf,
    .prepare_map_zip_path = &prepare_map_zip_path,
};
/******************************************************************************/
#ifdef __cplusplus
}
#endif
