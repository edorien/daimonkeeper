/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_texture_packs.h
 *     docs/refactor/editor/05-script-and-level-settings.md -- the fixed,
 *     built-in base-texture-set names, shared by the New Map/Level
 *     Settings dropdowns (editor_dialogs.cpp) and the Paint Texture
 *     toolbox tool's own picker (editor_toolbox.cpp). Mirrors
 *     texture_pack_desc[] (kfx_game's lvl_script_lib.h/
 *     lvl_script_commands.c) -- the same named table the classic
 *     SET_MAP_TEXTURE script command and its Lua equivalent already
 *     resolve names against -- kept as a small local copy rather than
 *     #including lvl_script_lib.h (kfx_game, otherwise unrelated to
 *     anything kfx_editor touches) just for one fixed, rarely-changing
 *     table. Index == the actual on-disk/kfx_config_state.texture_id (or
 *     slab_ext_data) value, so it can be used directly with FeCombo's
 *     current_item, no translation needed. BIG_BREASTS (id 6's later-added
 *     second alias in the real table) is deliberately omitted -- a
 *     dropdown can only show one label per id, and VOLUPTUOUS was the
 *     original.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_EDITOR_TEXTURE_PACKS_H
#define DK_EDITOR_TEXTURE_PACKS_H

namespace {
    const char *const kTexturePackItems[] = {
        "0: None", "1: Standard", "2: Ancient", "3: Winter", "4: Snake Key",
        "5: Stone Face", "6: Voluptuous", "7: Rough Ancient", "8: Skull Relief",
        "9: Desert Tomb", "10: Gypsum", "11: Lilac Stone", "12: Swamp Serpent",
        "13: Lava Cavern", "14: Laterite Cavern",
    };
    const int kTexturePackItemCount = (int)(sizeof(kTexturePackItems) / sizeof(kTexturePackItems[0]));
}

#endif
