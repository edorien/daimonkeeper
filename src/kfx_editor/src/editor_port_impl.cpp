/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_port_impl.cpp
 *     kfx_editor's EditorPort table: the implementations next to their
 *     provider, installed by main.cpp's wire_ports(). Refactor pass 2, S15.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "editor_port_impl.h"
#include "kfx_editor.h"
#include "content_tools.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

const struct EditorPort kfx_editor_port = {
    .request_open = &editor_open,
    .is_active = &editor_is_active,
    .frame = &editor_frame,
    .content_tools_is_available = &content_tools_is_available,
    .content_tools_open = &content_tools_open,
    .content_tools_frame = &content_tools_frame,
    .content_tools_is_open = &content_tools_is_open,
    .record_placement = &editor_journal_record_placement,
    .record_rect_terrain = &editor_journal_record_rect_terrain,
    .record_door_lock = &editor_journal_record_door_lock,
};
/******************************************************************************/
#ifdef __cplusplus
}
#endif
