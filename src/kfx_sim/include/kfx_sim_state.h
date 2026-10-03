/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file kfx_sim_state.h
 *     Header file for kfx_sim_state.c.
 * @par Purpose:
 *     Holds struct Game's kfx_sim-owned field group, migrated out of
 *     game_legacy.h incrementally per docs/refactor/stage-05-god-headers.md's
 *     decision and docs/refactor/stage-06-kfx-sim.md's "Prerequisite from
 *     stage 5" section. Landing incrementally (see the stage 6.7 plan) --
 *     fields are added to this struct, and removed from struct Game, one
 *     cohesive group at a time. `struct Game` is raw-serialized wholesale
 *     (see src/net_resync.cpp, src/game_saves.c, src/main_game.c's
 *     clear_complete_game()); this struct grows the same way and those three
 *     call sites were updated once, in the first field group, to also cover
 *     it via sizeof(kfx_sim_state) -- no further changes needed there as
 *     later groups are added.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/

#ifndef DK_KFX_SIM_STATE_H
#define DK_KFX_SIM_STATE_H

#include "port_check.h"
#include "state_versions.h"
#include "bflib_basics.h"
#include "bflib_math.h"
#include "bflib_netsp.h"
#include "globals.h"
#include "map_data.h"
#include "map_columns.h"
#include "slab_data.h"
#include "thing_data.h"
#include "creature_control.h"
#include "room_data.h"
#include "dungeon_data.h"
#include "thing_list.h"
#include "player_computer_types.h"
#include "creature_battle.h"
#include "actionpt.h"
#include "map_events.h"
#include "config.h"
#include "game_time.h" // struct GameTime
#include "light_registry.h"

// struct TimerTime moved here from kfx_frontend_state.h (stage 13.2,
// docs/refactor/stage-13-enforce-and-document.md) -- the only global of
// this type is kfx_sim_state.Timer below.
struct TimerTime {
    unsigned char Hours;
    unsigned char Minutes;
    unsigned char Seconds;
    int64_t MSeconds;
};

// GUI_MESSAGES_COUNT/GUI_MESSAGES_DELAY, struct GuiMessage, struct
// TextScrollWindow moved here from kfx_frontend_state.h (stage 13.2,
// docs/refactor/stage-13-enforce-and-document.md) -- kfx_sim is the
// lowest-ranked of all real consumers of the message-display buffers
// below.
#define GUI_MESSAGES_COUNT      7
#define GUI_MESSAGES_DELAY      400
// Identical redefinitions of kfx_game's game_merge.h macros (legal
// per C11 6.10.3p2 -- same replacement list, no diagnostic) so files
// that only need these two constants don't have to reach into
// game_merge.h, a higher-ranked library. Must stay in sync manually.
#define MESSAGE_TEXT_LEN        1024
#define QUICK_MESSAGES_COUNT    256

struct GuiMessage {
    char text[64];
    int64_t plyr_idx; //not playernumber because it is abused for other icons too
    uint64_t expiration_turn;
    int64_t target_idx;
    char type;
    int64_t icon_idx;
};

struct TextScrollWindow {
    char text[MESSAGE_TEXT_LEN];
    int64_t start_y;
    char action;
    int64_t text_height;
    int64_t window_height;
};

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
// Mirrored from src/game_merge.h (kfx_game), moved here (stage 6.7
// increment 2) since these are only used for kfx_sim array sizing.
#define AROUND_MAP_LENGTH 9
#define AROUND_SLAB_LENGTH 9
#define AROUND_SLAB_EIGHT_LENGTH 8
#define SMALL_AROUND_SLAB_LENGTH 4

// Moved from game_legacy.h (stage 13, docs/refactor/
// stage-13-enforce-and-document.md) alongside the bookmark field below.
#define BOOKMARKS_COUNT 5

// Moved from game_legacy.h (stage 13, docs/refactor/
// stage-13-enforce-and-document.md) alongside the game_kind field below,
// which is its only user after struct Game's decomposition.
enum GameKinds {
    GKind_Unset = 0,
    GKind_NonInteractiveState,
    GKind_LocalGame,
    GKind_LimitedState,
    GKind_UnusedSlot,
    GKind_MultiGame,
};

// Moved from game_legacy.h alongside operation_flags/view_mode_flags below
// (stage 13, docs/refactor/stage-13-enforce-and-document.md).
enum GameOperationFlags {
    GOF_Paused           = 0x01,
    GOF_SingleLevel      = 0x02, /**< Play single level and then exit. */
    GOF_ShowGui          = 0x20, /**< Showing main Gui. */
    GOF_ShowPanel        = 0x40, /**< Showing the tabbed panel. */
    GOF_WorldInfluence   = 0x80, /**< Input to the in-game world is allowed. */
};

enum GameNumfieldDFlags {
    GNFldD_CreatureViewMode = 0x01,
    GNFldD_unusedparam02 = 0x02,
    GNFldD_ComputerPlayerProcessing = 0x04,
    GNFldD_CreaturePasngr = 0x08, // Possessing a creature as a passenger (no direct control)
    GNFldD_WaitSleepMode = 0x10,
    GNFldD_StatusPanelDisplay = 0x20,
    GNFldD_RoomFlameProcessing = 0x40,
    GNFldD_unusedparam80 = 0x80,
};

#pragma pack(1)

// Moved from game_legacy.h (stage 6.7 increment 3) -- only used by
// creature_scores below and by kfx_sim's player_comptask.c.
struct PerExpLevelValues {
    unsigned char value[10];
};

// Moved from game_legacy.h (stage 6.7 increment 5) -- only used by the
// pool field below.
struct CreaturePool {
    int64_t crtr_kind[CREATURE_TYPES_MAX];
    unsigned char is_empty;
};

// Moved from lvl_script.h (stage 10, docs/refactor/stage-10-kfx-frontend.md)
// -- exclusively used by kfx_sim's thing_effects.c alongside the
// fx_lines[]/active_fx_lines fields it embeds below.
struct ScriptFxLine
{
    int64_t used;
    struct Coord3d from;
    struct Coord3d here;
    struct Coord3d to;

    int64_t cx, cy; // midpoint

    int64_t curvature;
    int64_t spatial_step;
    int64_t steps_per_turn;
    int64_t partial_steps;
    int64_t effect;

    int64_t total_steps;
    int64_t step;
};

// Moved from kfx_game's game_merge.h (stage 13.3, docs/refactor/
// stage-13-enforce-and-document.md) -- all six read kfx_sim_state's
// random seeds directly, and kfx_sim is the lowest-ranked of their
// real consumers.
#define THING_RANDOM(thing, range) LbRandomSeries(range, &((struct Thing*)(thing))->random_seed, __func__, __LINE__)
#define GAME_RANDOM(range) LbRandomSeries(range, &kfx_sim_state.action_random_seed, __func__, __LINE__)
#define UNSYNC_RANDOM(range) LbRandomSeries(range, &kfx_sim_state.unsync_random_seed, __func__, __LINE__)
#define SOUND_RANDOM(range) LbRandomSeries(range, &kfx_sim_state.sound_random_seed, __func__, __LINE__)
#define AI_RANDOM(range) LbRandomSeries(range, &kfx_sim_state.ai_random_seed, __func__, __LINE__)
#define PLAYER_RANDOM(plyr, range) LbRandomSeries(range, &kfx_sim_state.player_random_seed, __func__, __LINE__)

// Moved from kfx_game's game_merge.h (stage 13.3, docs/refactor/
// stage-13-enforce-and-document.md) -- it's the flag type for
// system_flags below, and kfx_sim is the lowest-ranked of its real
// consumers.
enum GameSystemFlags {
    GSF_NetworkActive    = 0x0001,
    GSF_NetGameNoSync    = 0x0002,
    GSF_NetSeedNoSync    = 0x0004,
    // Retired (docs/refactor/renderer/gpu-v2/01-phase-b-2d-compositing.md
    // B2) -- nothing sets this bit any more. Kept defined rather than
    // deleted since removing a plain #define-style bitmask value has no
    // renumbering effect on its siblings anyway (each is its own literal,
    // not auto-incremented), so there is nothing to gain by deleting it.
    GSF_CaptureMovie     = 0x0008,
    GSF_CaptureSShot     = 0x0010,
    GSF_AllowOnePlayer   = 0x0040,
    GSF_RunAfterVictory  = 0x0080,
};

struct KfxSimState {
    /* Map geometry (stage 6.7 increment 1) */
    MapSubtlCoord map_subtiles_x;
    MapSubtlCoord map_subtiles_y;
    MapSlabCoord map_tiles_x;
    MapSlabCoord map_tiles_y;

    /* Map data arrays (stage 6.7 increment 2) */
    struct Column columns_data[COLUMNS_COUNT];
    int64_t slabset_num;
    struct SlabSet slabset[SLABSET_COUNT];
    int64_t slabobjs_num;
    int64_t slabobjs_idx[SLABSET_COUNT];
    struct SlabObj slabobjs[SLABOBJS_COUNT];
    struct Map map[MAX_SUBTILES_X*MAX_SUBTILES_Y];
    struct SlabMap slabmap[MAX_TILES_X*MAX_TILES_Y];
    int64_t around_map[AROUND_MAP_LENGTH];
    int64_t around_slab[AROUND_SLAB_LENGTH];
    int64_t around_slab_eight[AROUND_SLAB_EIGHT_LENGTH];
    int64_t small_around_slab[SMALL_AROUND_SLAB_LENGTH];

    /* Thing/creature state (stage 6.7 increment 3) */
    struct CreatureControl cctrl_data[CREATURES_COUNT];
    struct Thing things_data[THINGS_COUNT];
    struct PerExpLevelValues creature_scores[CREATURE_TYPES_MAX];
    int64_t synced_free_things[SYNCED_THINGS_COUNT];
    ThingIndex synced_free_things_count;
    int64_t unsynced_free_things[UNSYNCED_THINGS_COUNT];
    ThingIndex unsynced_free_things_count;

    /* Room/dungeon state (stage 6.7 increment 4) */
    struct Room rooms[ROOMS_COUNT];
    struct Dungeon dungeon[DUNGEONS_COUNT];
    struct StructureList thing_lists[13];
    struct GoldLookup gold_lookup[GOLD_LOOKUP_COUNT];
    HitPoints block_health[10];
    int64_t entrance_room_id;

    /* Player/computer-AI state (stage 6.7 increment 5) */
    struct PlayerInfo players[PLAYERS_COUNT];
    // Per-human-user state (struct UserState, player_data.h), one slot per
    // network user id -- not one per PlayerInfo/player slot, since a
    // computer-controlled player has no user. Upstream (PR #5220/#5229)
    // keeps this as game.user_states[]; kept here instead, alongside
    // players[] which it's the per-user counterpart to, per this state
    // struct's existing per-library-not-struct-Game convention.
    struct UserState user_states[MAX_NET_USERS];
    struct ComputerTask computer_task[COMPUTER_TASKS_COUNT];
    struct Computer2 computer[PLAYERS_COUNT];
    struct CreaturePool pool;

    /* Power/hand UI-adjacent state (stage 6.7 increment 6) */
    MapSubtlCoord hand_over_subtile_x;
    MapSubtlCoord hand_over_subtile_y;
    int64_t chosen_room_kind;
    int64_t chosen_room_spridx;
    int64_t chosen_room_tooltip;
    int64_t chosen_spell_type;
    int64_t chosen_spell_spridx;
    int64_t chosen_spell_tooltip;
    int64_t manufactr_element;
    int64_t manufactr_spridx;
    int64_t manufactr_tooltip;

    /* Battles + random seeds (stage 6.7 increment 7, final) */
    struct CreatureBattle battles[BATTLES_COUNT];
    // RNG state is 32-bit by definition (LbRandomSeries is a 32-bit LCG + rotate), not just an "int".
    uint32_t action_random_seed;
    uint32_t ai_random_seed;
    uint32_t player_random_seed;
    uint32_t unsync_random_seed;
    uint32_t sound_random_seed;

    /* Map ceiling-height computation state (stage 7.2) -- mischaracterized
       as "render" in stage-05's ownership table; used exclusively by
       kfx_sim's map_ceiling.c. */
    uint64_t ceiling_height_max;
    uint64_t ceiling_height_min;
    uint64_t ceiling_dist;
    uint64_t ceiling_search_dist;
    uint64_t ceiling_step;

    /* Mischaracterized as kfx_game in stage 9's initial research (see
       docs/refactor/stage-09-kfx-game.md) -- actual usage (actionpt.c,
       creature_states_pray.c, map_locations.c, power_specials.c) is
       exclusively kfx_sim. */
    struct ActionPoint action_points[ACTN_POINTS_COUNT];
    struct Coord3d triggered_object_location; //Position of `TRIGGERED_OBJECT`
    int64_t script_current_player;
    GameTurn current_player_turn; // Actually it is a hack. We need to rewrite scripting for current player

    /* Mischaracterized as kfx_frontend in stage 10's initial research
       (see docs/refactor/stage-10-kfx-frontend.md) -- fx_lines/
       active_fx_lines/evntbox_text_buffer are exclusively used by
       kfx_sim's thing_effects.c/map_events.c. */
    struct ScriptFxLine fx_lines[FX_LINES_COUNT];
    int64_t active_fx_lines;
    char evntbox_text_buffer[MESSAGE_TEXT_LEN];

    /* Moved from struct Game (stage 13, docs/refactor/
       stage-13-enforce-and-document.md) -- read by kfx_render/kfx_game
       too, but kfx_sim is the lowest-ranked of its three consumers. */
    unsigned char applied_lens_type;

    /* Moved from struct Game (stage 13, docs/refactor/
       stage-13-enforce-and-document.md) -- each of these is read by at
       least one other library (kfx_render/kfx_game/kfx_frontend/kfx_net/
       kfx_script/kfx_apploop in various combinations), but kfx_sim is the
       lowest-ranked of every one of their consumer sets, so relocating
       here resolves every edge at once. */
    unsigned char system_flags; // flags in enum GameSystemFlags below
    unsigned char operation_flags;
    unsigned char view_mode_flags; //flags in enum GameNumfieldDFlags
    unsigned char mode_flags;
    ColumnIndex unrevealed_column_idx;
    struct Event event[EVENTS_COUNT];
    // Sized TEXTURE_BLOCKS_COUNT (engine_textures.h, kfx_render) -- kfx_sim
    // can't include that header, so the value is duplicated here as a
    // literal. Must stay in sync manually if engine_textures.h's texture
    // block counts ever change.
    int64_t top_cube[1544]; // if you ask for top cube on a column without cubes, it'll return the first cube it finds with said texture at the top
    unsigned char small_map_state;
    enum GameKinds game_kind; /**< Kind of the game being played, from GameKinds enumeration. Originally was GameMode. */
    struct Bookmark bookmark[BOOKMARKS_COUNT];
    struct Coord3d armageddon_mappos;
    GameTurn armageddon_cast_turn;
    GameTurn armageddon_over_turn;
    PlayerNumber armageddon_caster_idx;
    GameTurn turn_last_checked_for_gold;
    uint8_t max_custom_box_kind;
    int64_t nodungeon_creatr_list_start; /**< Linked list of creatures which have no dungeon (neutral and owned by nonexisting players) */

    /* Moved from struct Game (stage 13, docs/refactor/
       stage-13-enforce-and-document.md) -- also read by kfx_platform's
       bflib_sndlib.cpp, which gets pointer access via
       SoundHostPort instead (kfx_platform is the lowest-ranked
       library, can't reach kfx_sim_state directly). */
    TbBool easter_eggs_enabled;

    /* Moved from kfx_frontend_state (stage 13.2, docs/refactor/
       stage-13-enforce-and-document.md) -- comment at the old site said
       "Used for GUI only; the real tendency is a flag inside Dungeon",
       but kfx_sim reads/writes these directly too (player_compchecks.c,
       player_utils.c), which is the lowest-ranked of all real
       consumers (kfx_game/kfx_net/kfx_script also read them). */
    TbBool creatures_tend_imprison;
    TbBool creatures_tend_flee;

    /* Moved from kfx_frontend_state (stage 13.2, docs/refactor/
       stage-13-enforce-and-document.md) -- read/written by kfx_sim's
       player_instances.c/player_utils.c, the lowest-ranked of all real
       consumers (kfx_apploop/kfx_frontend/kfx_game also read them). */
    TbClockMSec timerstarttime;
    struct TimerTime Timer;
    TbBool TimerGame;
    TbBool TimerNoReset;
    TbBool TimerFreeze;
    TbBool TimerGameReal;          // -timer game real: draw in-game time (seconds) instead of turns
    uint64_t GameSeconds;
    struct GameTime GameT;

    /* Moved from kfx_frontend_state (stage 13.2, docs/refactor/
       stage-13-enforce-and-document.md) -- message-display buffers
       written by kfx_sim (thing_creature.c, creature_instances.c,
       map_events.c) as well as by kfx_game/kfx_script/kfx_frontend;
       kfx_sim is the lowest-ranked of all real consumers. */
    struct GuiMessage messages[GUI_MESSAGES_COUNT];
    char quick_messages[QUICK_MESSAGES_COUNT][MESSAGE_TEXT_LEN];
    char evntbox_text_objective[PLAYERS_COUNT][MESSAGE_TEXT_LEN];
    struct TextScrollWindow evntbox_scroll_window;
    char box_tooltip[CUSTOM_BOX_COUNT][MESSAGE_TEXT_LEN];
    unsigned char active_messages_count;

    /* Moved from kfx_frontend_state (stage 13.2, docs/refactor/
       stage-13-enforce-and-document.md) -- read/written by kfx_sim's
       thing_creature.c, the lowest-ranked of all real consumers
       (kfx_net/kfx_frontend also read/write it). */
    char active_panel_mnu_idx; /**< The MenuID of currently active panel menu, or 0 if none. */

    /* Moved from game_legacy.h's standalone extern (stage 13.3,
       docs/refactor/stage-13-enforce-and-document.md) -- read broadly
       (kfx_apploop/kfx_frontend/kfx_game/kfx_net/kfx_render), but
       kfx_sim (thing_data.c/power_process.c/player_utils.c) is the
       lowest-ranked of its real consumer set. */
    int64_t turns_per_second;

    /* Moved from kfx_render's vidfade.c (stage 13.3, docs/refactor/
       stage-13-enforce-and-document.md) -- read/written by kfx_render
       (vidmode.c/engine_render.c) and kfx_game (main_game.c), but
       kfx_sim (creature_graphics.c) is the lowest-ranked of its real
       consumers. Dimension hardcoded from vidfade.h's
       COLOUR_TABLE_DIMENSION (1<<COLOUR_TABLE_BITS_PER_VALUE == 16),
       since that macro/typedef stay in kfx_render for
       compute_rgb2idx_table()'s signature. */
    unsigned char colours[16][16][16];

    /* Moved from kfx_frontend's frontend.h/frontend.cpp (stage 13.3,
       docs/refactor/stage-13-enforce-and-document.md) -- new_objective
       written by kfx_sim's map_events.c, read by kfx_frontend's
       frontmenu_ingame_tabs.c. kfx_sim is the lowest-ranked of its real
       consumers. (A default_tag_mode copy used to live here too, made
       once in setup_game() from kfx_config's own keeperfx_ui_config --
       removed because clear_complete_game()'s wholesale kfx_sim_state
       memset, which runs right after that one-time copy, permanently
       zeroed it for the rest of the process. Read
       keeperfx_ui_config.default_tag_mode directly instead -- kfx_config
       outranks kfx_sim so this is a normal downward include, and that
       struct isn't touched by clear_complete_game().) */
    unsigned char new_objective;

    /* Moved from kfx_game's kfx_game_state.h (stage 13.4, docs/refactor/
       stage-13-enforce-and-document.md) -- loaded_level_number/
       continue_level_number/selected_level_number are read/written by
       kfx_net (net_checksums.c/net_game.c/packets_misc.c) and kfx_sim
       itself (lvl_filesdk1.c/power_specials.c/thing_factory.c/
       player_utils.c) alongside their many kfx_game/kfx_frontend/
       kfx_script/kfx_apploop readers -- kfx_sim is the lowest-ranked of
       every one of their real consumer sets. computer_chat_flags is
       written by kfx_sim's player_comp*.c (the AI implementation itself)
       and only read by kfx_game for display (loaded_swipe_idx, which was
       here too, is kfx_render's since refactor pass 4, S08);
       heart_lost_display_message is also written by kfx_sim's
       map_events.c (see kfx_game_state.h's comment on its 3 siblings,
       which have no kfx_sim consumer and stayed put). */
    LevelNumber continue_level_number;
    LevelNumber selected_level_number;
    int64_t loaded_level_number;
    /* The game turn: moved from kfx_game_state in refactor pass 2 (S10),
       since the whole sim reads it. get_gameturn() (globals.h) reads it
       through a pointer main.cpp installs. */
    GameTurn play_gameturn;
    /* Session values moved from kfx_net_state in refactor pass 2 (S10):
       the sim reads or writes each of them. level_human_player is the
       level's designated human seat (campaign.human_player), the same on
       every machine -- not the local machine's player, which is
       my_player_number (it was kfx_net_state.local_plyr_idx); replay_active
       is -packetload replay mode (was packet_load_enable). */
    PlayerNumber level_human_player;
    int64_t human_players_count;
    TbBool replay_active;
    int64_t computer_chat_flags;
    TbBool heart_lost_display_message;

    /* docs/refactor/editor/01-entry-and-editor-session.md §3 -- a neutral
       "force GOF_Paused and don't let anything lift it" flag, not an
       "editor_mode" flag: the in-game level editor is its first user, but
       nothing here names the editor, so a later feature (demo scrubbing, a
       replay inspector) can reuse it. Blob-safe like every other field in
       this struct (survives the save/resync/reset memcpy's). */
    TbBool simulation_suspended;

    /* The lights (light_registry.h): moved from kfx_render's lish in
       refactor pass 2 (S11). The sim creates, moves and deletes them and
       reads their intensity back; kfx_render shades from them. */
    struct LightRegistry light_registry;
};

#pragma pack()
/******************************************************************************/
extern struct KfxSimState kfx_sim_state;
/* In every file that includes this header, not only the struct's own: a file that sees another layout reads the
   state at other offsets than the rest of the game (P4-F17). */
KFX_STATIC_ASSERT(sizeof(struct KfxSimState) == KFX_SIM_STATE_SIZE,
    "struct KfxSimState has another size in this file than state_versions.h says: a #pragma pack leaking into the headers it includes (refactor pass 4, P4-F17), or a layout change (bump KFX_SIM_STATE_VER and update KFX_SIM_STATE_SIZE)");


// Moved from kfx_game's game_legacy.c/.h (stage 13.3, docs/refactor/
// stage-13-enforce-and-document.md) -- only reads kfx_sim_state.system_flags,
// and kfx_config is the lowest-ranked of its real consumers (kfx_apploop/
// kfx_frontend/kfx_game/kfx_net/kfx_script/kfx_sim also call it).
static inline TbBool network_is_active(void)
{
    return flag_is_set(kfx_sim_state.system_flags, GSF_NetworkActive);
}

// Moved from kfx_game's kfx_game_state.h as static inlines (stage 13.4,
// docs/refactor/stage-13-enforce-and-document.md) -- see the field-group
// comment above for why kfx_sim is these fields' true owner now.
static inline LevelNumber get_loaded_level_number(void)
{
    return kfx_sim_state.loaded_level_number;
}
static inline LevelNumber set_loaded_level_number(LevelNumber lvnum)
{
    if (lvnum > 0)
        kfx_sim_state.loaded_level_number = lvnum;
    return kfx_sim_state.loaded_level_number;
}
static inline LevelNumber get_continue_level_number(void)
{
    return kfx_sim_state.continue_level_number;
}
static inline LevelNumber set_continue_level_number(LevelNumber lvnum)
{
    if (is_singleplayer_like_level(lvnum))
        kfx_sim_state.continue_level_number = lvnum;
    return kfx_sim_state.continue_level_number;
}
static inline LevelNumber get_selected_level_number(void)
{
    return kfx_sim_state.selected_level_number;
}
static inline LevelNumber set_selected_level_number(LevelNumber lvnum)
{
    if (lvnum >= 0)
        kfx_sim_state.selected_level_number = lvnum;
    return kfx_sim_state.selected_level_number;
}
static inline LevelNumber get_level_number(void)
{
    LevelNumber lvnum = get_selected_level_number();
    if (lvnum <= 0)
        lvnum = get_loaded_level_number();
    return lvnum;
}

// docs/refactor/editor/01-entry-and-editor-session.md §3 -- read side of
// simulation_suspended. game_session_loop.cpp's per-turn block already
// gates on GOF_Paused directly (that's the actual freeze); this accessor
// is for callers that want to know *why* the sim is paused (was it forced
// suspended, vs. a plain user pause) without reaching into operation_flags
// themselves.
static inline TbBool simulation_is_suspended(void)
{
    return kfx_sim_state.simulation_suspended;
}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
