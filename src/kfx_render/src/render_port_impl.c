/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file render_port_impl.c
 *     kfx_render's RenderPort table: the implementations next to their
 *     provider, installed by main.cpp's wire_ports(). Refactor pass 2, S15.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "render_port_impl.h"
#include "cursor_tag.h"
#include "engine_redraw.h"
#include "engine_textures.h"
#include "vidfade.h"
#include "lens_api.h"
#include "engine_arrays.h"
#include "custom_sprites.h"
#include "local_camera.h"
#include "render_creature_view.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
// engine_lenses.c owns lens_mode.
static unsigned char get_lens_mode(void)
{
    return lens_mode;
}

const struct RenderPort kfx_render_port = {
    .tag_cursor_blocks_place_room = &tag_cursor_blocks_place_room,
    .tag_cursor_blocks_sell_area = &tag_cursor_blocks_sell_area,
    .set_engine_view = &set_engine_view,
    .setup_engine_window = &setup_engine_window,
    .load_texture_map_file = &load_texture_map_file,
    .PaletteSetUserViewPalette = &PaletteSetUserViewPalette,
    .PaletteSetViewPalette = &PaletteSetViewPalette,
    .PaletteFadeToView = &PaletteFadeToView,
    .load_swipe_graphic_for_creature = &load_swipe_graphic_for_creature,
    .PaletteApplyPainToPlayer = &PaletteApplyPainToPlayer,
    .setup_eye_lens = &setup_eye_lens,
    .get_td_animation_sprite = &get_td_animation_sprite,
    .get_lens_mode = &get_lens_mode,
    .get_icon_id = &get_icon_id,
    .get_anim_id = &get_anim_id,
    .get_anim_id_ = &get_anim_id_,
    .get_ensign_id = &get_ensign_id,
    .init_custom_campaign_sprites = &init_custom_campaign_sprites,
    .load_sprites_for_multi_front = &load_sprites_for_multi_front,
    .local_view_type_settle = &local_view_type_settle,
};
/******************************************************************************/
#ifdef __cplusplus
}
#endif
