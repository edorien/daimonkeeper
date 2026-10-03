/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file display_host_port_impl.cpp
 *     kfx_frontend's DisplayHostPort table: the implementations next to their
 *     provider, installed by main.cpp's wire_ports(). Refactor pass 2, S15.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "display_host_port_impl.h"
#include "gui/FrontendImGui.h"
#include "frontgui_screens.h"
#include "vidmode.h"
#include "gui_draw.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

const struct DisplayHostPort kfx_frontend_display_host_port = {
    .imgui_ensure = &FrontendImGuiEnsure,
    .imgui_renderer_destroying = &FrontendImGuiRendererDestroying,
    .imgui_begin_frame = &FrontendImGuiBeginFrame,
    .imgui_submit = &FrontendImGuiFrame,
    .imgui_render = &FrontendImGuiRender,
    .imgui_process_event = &FrontendImGuiProcessEvent,
    .imgui_is_active = &FrontendImGuiIsActive,
    .imgui_screen_owned = &FrontendImGuiScreenOwned,
    .imgui_set_demo_visible = &FrontendImGuiSetDemoVisible,
    .get_video_scale_values = &get_video_scale_values,
    .draw_slab_background_immediate = &draw_slab64k_background_immediate,
};
/******************************************************************************/
#ifdef __cplusplus
}
#endif
