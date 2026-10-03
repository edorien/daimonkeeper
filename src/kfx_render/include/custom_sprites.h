/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file game_heap.c
 *     Definition of heap, used for storing memory-expensive sounds and graphics.
 * @par Purpose:
 *     Functions to create and maintain memory heap.
 * @par Comment:
 *     None.
 * @author   KeeperFX Team
 * @date     06 Apr 2021
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/

#ifndef GIT_CUSTOM_SPRITES_H
#define GIT_CUSTOM_SPRITES_H

#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif
struct ObjectConfigStats;

#define SPRITE_LAST_LEVEL -1
// CUSTOM_ENSIGN_BASE moved to config.h (kfx_config) -- config_campaigns.c
// and lvl_script_commands.c need it and are both ranked below kfx_render.

static const char * const required_sprite_zips[] = {
    "colored_sprites.zip",
    "creatures.zip",
    "decorative_objects.zip",
    "druid.zip",
    "effects.zip",
    "maiden.zip",
    "natural_features.zip",
    "time_mage.zip",
    "trapsdoors.zip",
};

#define REQUIRED_SPRITE_ZIP_COUNT (sizeof(required_sprite_zips) / sizeof(required_sprite_zips[0]))

void init_custom_sprites(LevelNumber level_no);
void init_custom_campaign_sprites(const char *dir_path, const char *dir_desc);
void show_ignored_fxdata_zip_messages(void);
void load_sprites_for_multi_front(LevelNumber lvnum);

extern TbBigChecksum required_sprite_zip_checksums[REQUIRED_SPRITE_ZIP_COUNT];

int64_t get_anim_id(const char *name, struct ObjectConfigStats* objst);
int64_t get_anim_id_(const char* name);
int64_t get_icon_id(const char *name);
int64_t get_ensign_id(const char *name);
struct TbSpriteSheet *load_custom_ensigns_into_sheet(struct TbSpriteSheet *sheet, const unsigned char *palette);
const struct TbSprite *get_custom_ensign_sprite(struct TbSpriteSheet *sheet, int64_t ensign_id, int64_t frame);
const struct TbSprite *get_button_sprite_for_player(int64_t sprite_idx, PlayerNumber plyr_idx);
const struct TbSprite *get_button_sprite(int64_t sprite_idx);
const struct TbSprite *get_frontend_sprite(int64_t sprite_idx);
const struct TbSprite *get_new_icon_sprite(int64_t sprite_idx);
const struct TbSprite *get_panel_sprite(int64_t sprite_idx);
int64_t is_custom_icon(int64_t icon_idx);
/** True when get_panel_sprite(sprite_idx) would return a real sprite rather than the engine's magenta checkerboard placeholder (bad_icon). */
TbBool is_panel_sprite_drawable(int64_t sprite_idx);
int64_t get_custom_icon_frame_count(int64_t icon_idx);
// Lens overlay data structure
struct LensOverlayData {
    char *name;
    unsigned char *data;
    int64_t width;
    int64_t height;
};

// Lens mist data structure
struct LensMistData {
    char *name;
    unsigned char *data;  // 256x256 mist texture
};

// Get lens overlay data by name (returns NULL if not found)
const struct LensOverlayData* get_lens_overlay_data(const char *name);

// Get lens mist data by name (returns NULL if not found)
const struct LensMistData* get_lens_mist_data(const char *name);

extern int64_t bad_icon_id;
#ifdef __cplusplus
}
#endif

#endif //GIT_CUSTOM_SPRITES_H
