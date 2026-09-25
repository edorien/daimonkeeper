/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file sprite_lookup.h
 *     Header file for sprite_lookup.c.
 * @par Purpose:
 *     Callback-registration interface letting kfx_config resolve
 *     sprite/icon/animation name strings (parsed from TOML config
 *     files) to numeric IDs, and look up button/panel sprites by
 *     index, without depending on custom_sprites.h directly (that's
 *     kfx_render layer, above kfx_config). See
 *     docs/refactor/stage-13-enforce-and-document.md.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/

#ifndef DK_SPRITE_LOOKUP_H
#define DK_SPRITE_LOOKUP_H

#include "bflib_basics.h"
#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
struct ObjectConfigStats;
struct TbSprite;

struct SpriteLookupCallbacks {
    int64_t (*get_icon_id)(const char *name);
    int64_t (*get_anim_id)(const char *name, struct ObjectConfigStats *objst);
    int64_t (*get_anim_id_)(const char *name);
    const struct TbSprite *(*get_button_sprite)(int64_t sprite_idx);
    const struct TbSprite *(*get_panel_sprite)(int64_t sprite_idx);
    // custom_sprites.h -- config_campaigns.c resolves a campaign's
    // SET_LEVEL_ENSIGN-style config text to a custom-ensign slot, and
    // triggers loading that campaign's custom ensign sprite sheet.
    int64_t (*get_ensign_id)(const char *name);
    void (*init_custom_campaign_sprites)(const char *dir_path, const char *dir_desc);
    // custom_sprites.h -- lvl_filesdk1.c triggers loading a multiplayer
    // map's own custom ensign/sprite sheet (map%05lu.zip) while parsing
    // its .lof file.
    void (*load_sprites_for_multi_front)(LevelNumber lvnum);
};
void set_sprite_lookup_callbacks(const struct SpriteLookupCallbacks *callbacks);
extern const struct SpriteLookupCallbacks *sprite_lookup;
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
