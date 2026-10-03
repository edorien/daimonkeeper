/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file kfx_config_state.h
 *     Header file for kfx_config_state.c.
 * @par Purpose:
 *     Holds struct Game's kfx_config-owned field group, migrated out of
 *     game_legacy.h per docs/refactor/stage-13-enforce-and-document.md
 *     (mirrors kfx_sim_state.h/kfx_net_state.h/kfx_game_state.h/
 *     kfx_frontend_state.h/kfx_render_state.h's approach). `struct Game`
 *     is raw-serialized wholesale (see kfx_net/src/net_resync.cpp,
 *     kfx_game/src/game_saves.c, kfx_game/src/main_game.c's
 *     clear_complete_game()); this struct is synced/saved/reset the same
 *     way, alongside the other five, at all three call sites.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/

#ifndef DK_KFX_CONFIG_STATE_H
#define DK_KFX_CONFIG_STATE_H

#include "port_check.h"
#include "state_versions.h"
#include "bflib_basics.h"
#include "globals.h"
#include "config_magic.h"
#include "config_trapdoor.h"
#include "config_objects.h"
#include "config_cubes.h"
#include "config_powerhands.h"
#include "config_creature.h"
#include "config_effects.h"
#include "config_rules.h"
#include "config_players.h"
#include "config_slabsets.h"
#include "config_terrain.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

// Mirrors player_data.h's identical #define (kfx_sim, rank2) -- kfx_config
// can't depend on that header, so the value is duplicated here under the
// same name (legal for identical macro redefinition). Must stay in sync
// manually if PLAYERS_COUNT ever changes. Used by config_cubes.c/
// config_rules.c/config_crtrmodel.c.
#define PLAYERS_COUNT 9

// Moved from kfx_render's engine_textures.h (stage 13.3, docs/refactor/
// stage-13-enforce-and-document.md) -- pure texture-count constants
// with no type dependency on the actual texture-buffer storage that
// stays in engine_textures.h; kfx_config (config_cubes.c/
// config_textures.c) is the lowest-ranked of their real consumers
// (kfx_render/kfx_sim also read them).
#define TEXTURE_VARIATIONS_COUNT      32
#define TEXTURE_BLOCKS_STAT_COUNT_A   544
#define TEXTURE_BLOCKS_STAT_COUNT_B   544
#define TEX_B_START_POINT             1000
#define TEXTURE_BLOCKS_STAT_COUNT   (TEXTURE_BLOCKS_STAT_COUNT_A + TEXTURE_BLOCKS_STAT_COUNT_B)
#define TEXTURE_BLOCKS_ANIM_FRAMES    8
#define TEXTURE_BLOCKS_ANIM_COUNT    TEX_B_START_POINT - TEXTURE_BLOCKS_STAT_COUNT_A
#define TEXTURE_BLOCKS_COUNT         (TEXTURE_BLOCKS_STAT_COUNT + TEXTURE_BLOCKS_ANIM_COUNT)
#define TEXTURE_LAND_MARKED_LAND     578
#define TEXTURE_LAND_MARKED_GOLD     579

#pragma pack(1)

// Moved from game_legacy.h (stage 13, docs/refactor/
// stage-13-enforce-and-document.md) alongside the conf field below that
// embeds Configs by value -- Configs' own sub-structs (SlabsConfig,
// PowerHandConfig, ...) are all kfx_config-owned types, so this was
// always really kfx_config's own aggregate type, just homed in
// game_legacy.h since stage 9.
#define LUA_FUNCS_MAX       256
#define LUA_FUNCNAME_LENGTH 256

struct LuaFuncsConf {
    char lua_funcs[LUA_FUNCS_MAX][LUA_FUNCNAME_LENGTH];
};

// Moved from kfx_game's game_merge.h (stage 13.3, docs/refactor/
// stage-13-enforce-and-document.md) -- config_rules.c's
// PRESERVECLASSICBUGS named-field table is the lowest-ranked of its
// real consumers (kfx_sim/kfx_game also read it).
enum ClassicBugFlags {
    ClscBug_None                          = 0x0000,
    ClscBug_ResurrectForever              = 0x0001,
    ClscBug_Overflow8bitVal               = 0x0002,
    ClscBug_ClaimRoomAllThings            = 0x0004,
    ClscBug_ResurrectRemoved              = 0x0008,
    ClscBug_NoHandPurgeOnDefeat           = 0x0010,
    ClscBug_MustObeyKeepsNotDoJobs        = 0x0020,
    ClscBug_BreakNeutralWalls             = 0x0040,
    ClscBug_AlwaysTunnelToRed             = 0x0080,
    ClscBug_FullyHappyWithGold            = 0x0100,
    ClscBug_FaintedImmuneToBoulder        = 0x0200,
    ClscBug_RebirthKeepsSpells            = 0x0400,
    ClscBug_FriendlyFaint                 = 0x0800,
    ClscBug_PassiveNeutrals               = 0x1000,
    ClscBug_NeutralTortureConverts        = 0x2000,
    ClscBug_LibraryExtraBook              = 0x4000, // dAImon Keeper: pass 3 finding F9
    ClscBug_CreatureStatsWrap             = 0x8000, // dAImon Keeper: pass 3 finding F10
    ClscBug_CornerWallUnrevealed          = 0x10000, // dAImon Keeper: pass 3 finding F2
    ClscBug_CrookedSightLines             = 0x20000, // dAImon Keeper: pass 3 finding F3
    ClscBug_ListEnd                       = 0x40000,
};

struct Configs {
    struct SlabsConfig slab_conf;
    struct PowerHandConfig power_hand_conf;
    struct MagicConfig magic_conf;
    struct CubesConfig cube_conf;
    struct TrapDoorConfig trapdoor_conf;
    struct EffectsConfig effects_conf;
    struct CreatureConfig crtr_conf;
    struct ObjectsConfig object_conf;
    // Sized PLAYERS_COUNT (player_data.h, kfx_sim) -- kfx_config can't
    // depend on that header, so the value is duplicated here as a
    // literal. Must stay in sync manually if PLAYERS_COUNT ever changes.
    struct RulesConfig rules[9];
    struct PlayerStateConfig plyr_conf;
    struct ColumnConfig column_conf;
    struct LuaFuncsConf lua;
};

struct KfxConfigState {
    // texture_animation/texture_id: read by kfx_render/kfx_sim/kfx_script
    // too, but kfx_config (lvl_filesdk1.c) is the lowest-ranked of their
    // consumer sets. Sized TEXTURE_BLOCKS_ANIM_FRAMES*TEXTURE_BLOCKS_ANIM_COUNT
    // as a literal, not the macro expression -- TEXTURE_BLOCKS_ANIM_COUNT's
    // definition lacks parens around its subtraction, so using it directly
    // in a multiplication would silently compute the wrong value.
    int64_t texture_animation[3648];
    unsigned char texture_id;

    // col_static_entries: read by kfx_sim too, kfx_config is lower-ranked.
    int64_t col_static_entries[18];

    // neutral_player_num/slab_ext_data(_initial): each read by several
    // other libraries, but kfx_config is the lowest-ranked of every one
    // of their consumer sets.
    PlayerNumber neutral_player_num;
    unsigned char slab_ext_data[MAX_TILES_X*MAX_TILES_Y];
    unsigned char slab_ext_data_initial[MAX_TILES_X*MAX_TILES_Y];

    // conf/pay_day_progress: moved together (stage 13, docs/refactor/
    // stage-13-enforce-and-document.md) -- config_rules.c's
    // PayDayProgress script field computes its offset as
    // offsetof(_, pay_day_progress) - offsetof(_, conf.rules), which only
    // works if both stay in the same base struct. Previously that base
    // was struct Game; now it's this one. get_rules_base() and the
    // offsetof expressions in config_rules.c were updated to match.
    struct Configs conf;
    GameTurnDelta pay_day_progress[9]; // PLAYERS_COUNT (kfx_sim) -- see comment above.

    // Everything above is the game's: upstream's struct Game held it, so saves and resyncs carry it
    // (KFX_CONFIG_STATE_SAVED_*, refactor pass 4 P4-F16). Everything below is daimonkeeper.cfg's settings.

    // Moved from kfx_game's sounds.c (stage 13.3, docs/refactor/
    // stage-13-enforce-and-document.md) -- only written by kfx_config's
    // config_keeperfx.c, kfx_game's sounds.c is its only real reader.
    int64_t atmos_sound_frequency;

    // Moved from kfx_render's engine_camera.c (stage 13.3, docs/refactor/
    // stage-13-enforce-and-document.md) -- CFG settings, written by
    // kfx_config's config_keeperfx.c, read by kfx_render's own
    // engine_camera.c and kfx_net's net_game.c (session sync)/packets.c;
    // kfx_config is the lowest-ranked of their real consumers.
    int64_t zoom_distance_setting;
    int64_t frontview_zoom_distance_setting;

    // Moved from kfx_frontend's gui_draw.c (stage 13.3, docs/refactor/
    // stage-13-enforce-and-document.md) -- both CFG settings, read
    // broadly by kfx_frontend/kfx_render, but kfx_config is the
    // lowest-ranked of their real consumers (engine_render.c reads
    // both; config_spritecolors.c also reads neutral_flash_rate).
    int64_t gui_blink_rate;
    int64_t neutral_flash_rate;
};

// Moved from kfx_render's engine_camera.h (stage 13.3, docs/refactor/
// stage-13-enforce-and-document.md) -- read by kfx_config's
// config_keeperfx.c and kfx_net's packets.c, kfx_config is the
// lowest-ranked of their real consumers. MINMAX_*/struct MinMax stay in
// kfx_render's engine_camera.h -- kfx_render is their only real consumer.
// CAMERA_TILT_* are in kfx_sim's player_camera.h since refactor pass 2's
// S07 (config_settings.c already locally duplicates the tilt values it
// needs).
#define CAMERA_ZOOM_MAX 12000
#define CAMERA_ZOOM_MIN 520 // Originally 4100, adjusted for view distance
#define FRONTVIEW_CAMERA_ZOOM_MAX 65536
#define FRONTVIEW_CAMERA_ZOOM_MIN 3000 // Originally 16384, adjusted for view distance
// docs/refactor/editor/04-views-camera-overlays.md -- editor camera profile.
// A map-editing session wants to zoom out further than gameplay's own hard
// floor (CAMERA_ZOOM_MIN) ever allows, to see more of the map at once while
// placing far-apart features.
//
// Reminder since it's tripped up manual retuning before: LOWER means
// further ZOOMED OUT (more of the map visible, everything smaller on
// screen) -- see engine_camera.h's own "zoom max is zoomed in... zoom min
// is zoomed out" comment. 100 is a MORE extreme zoom-out than 350, not a
// safer/more conservative one.
//
// Found live: this value broke terrain rendering at extreme zoom-out on a
// large, open map (part of the screen went solid black) at every value
// tried so far below stock (130, then 350, then -- worse again -- 100).
// Root cause (engine_render.c, engine_camera.h): compute_cells_away()
// derives how many subtile-scale "cells" away the visible screen edges are
// from the camera, and how far that can grow before it stops mattering
// depends on how much open, revealed floor extends in the camera's own
// view direction -- not on this constant's exact value. MAX_I_CAN_SEE_
// OVERHEAD (the hard clamp compute_cells_away() hits) is what actually
// needed raising, not this number -- see MINMAX_LENGTH's own comment
// (engine_camera.h) for that fix (512 -> 2048, done alongside this).
// Reset to a cautious value again -- closer to the stock floor (520) than
// any previous attempt -- pending live-test confirmation that the
// MINMAX_LENGTH headroom fixes the dropout at all before pushing back
// toward a more useful (lower/further-out) number.
#define EDITOR_CAMERA_ZOOM_MIN 450

#pragma pack()
/******************************************************************************/
extern struct KfxConfigState kfx_config_state;
/* In every file that includes this header, not only the struct's own: a file that sees another layout reads the
   state at other offsets than the rest of the game (P4-F17). */
KFX_STATIC_ASSERT(sizeof(struct KfxConfigState) == KFX_CONFIG_STATE_SIZE,
    "struct KfxConfigState has another size in this file than state_versions.h says: a #pragma pack leaking into the headers it includes (refactor pass 4, P4-F17), or a layout change (bump KFX_CONFIG_STATE_VER and update KFX_CONFIG_STATE_SIZE)");
/** The part of kfx_config_state a save (SGC_KfxConfigState) and a network resync carry: from texture_animation to
 *  pay_day_progress (the level's configuration as its script changed it, the payday progress, the slabs' texture
 *  packs...: what upstream's struct Game held). Refactor pass 4, P4-F16. */
#define KFX_CONFIG_STATE_SAVED_OFFSET offsetof(struct KfxConfigState, texture_animation)
#define KFX_CONFIG_STATE_SAVED_LEN (offsetof(struct KfxConfigState, atmos_sound_frequency) - KFX_CONFIG_STATE_SAVED_OFFSET)
#define KFX_CONFIG_STATE_SAVED_PTR ((void *)((char *)&kfx_config_state + KFX_CONFIG_STATE_SAVED_OFFSET))
KFX_STATIC_ASSERT(KFX_CONFIG_STATE_SAVED_LEN == KFX_CONFIG_STATE_SAVED_SIZE,
    "kfx_config_state's saved part changed size: bump KFX_CONFIG_STATE_VER and update KFX_CONFIG_STATE_SAVED_SIZE in state_versions.h");
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
