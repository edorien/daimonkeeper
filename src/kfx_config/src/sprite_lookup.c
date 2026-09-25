/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file sprite_lookup.c
 *     Callback-registration implementation. See sprite_lookup.h.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "sprite_lookup.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
static int64_t noop_get_icon_id(const char *name) { return 0; }
static int64_t noop_get_anim_id(const char *name, struct ObjectConfigStats *objst) { return 0; }
static int64_t noop_get_anim_id_(const char *name) { return 0; }
static const struct TbSprite *noop_get_button_sprite(int64_t sprite_idx) { return NULL; }
static const struct TbSprite *noop_get_panel_sprite(int64_t sprite_idx) { return NULL; }
static int64_t noop_get_ensign_id(const char *name) { return -1; }
static void noop_init_custom_campaign_sprites(const char *dir_path, const char *dir_desc) {}
static void noop_load_sprites_for_multi_front(LevelNumber lvnum) {}

static const struct SpriteLookupCallbacks default_sprite_lookup_callbacks = {
    &noop_get_icon_id,
    &noop_get_anim_id,
    &noop_get_anim_id_,
    &noop_get_button_sprite,
    &noop_get_panel_sprite,
    &noop_get_ensign_id,
    &noop_init_custom_campaign_sprites,
    &noop_load_sprites_for_multi_front,
};

const struct SpriteLookupCallbacks *sprite_lookup = &default_sprite_lookup_callbacks;

void set_sprite_lookup_callbacks(const struct SpriteLookupCallbacks *callbacks)
{
    sprite_lookup = callbacks ? callbacks : &default_sprite_lookup_callbacks;
}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
