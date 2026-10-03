#include "ftest_editor_session.h"

#ifdef FUNCTESTING

extern "C" {
#include "pre_inc.h"
#include "../ftest.h"
#include "../ftest_util.h"
#include "game_legacy.h"
#include "config_keeperfx.h"
#include "config.h"
#include "kfx_editor.h"
#include "editor_journal.h"
#include "config_terrain.h"
#include "packets.h"
#include "player_instances.h"
#include "kfx_sim_state.h"
#include "slab_data.h"
#include "map_blocks.h"
#include "thing_list.h"
#include "lvl_filesdk1.h"
#include "map_data.h"
#include "light_registry.h"
#include "game_lifecycle.h"
#include "actionpt.h"
#include "post_inc.h"
}

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

extern "C" {

#define FROZEN_FRAMES 400
#define ROUNDTRIP_LVNUM 90010

FTestActionResult ftest_editor_session_action001__open(struct FTestActionArgs* const args);
FTestActionResult ftest_editor_session_action002__stay_frozen(struct FTestActionArgs* const args);
FTestActionResult ftest_editor_session_action002b__preview(struct FTestActionArgs* const args);
FTestActionResult ftest_editor_session_action003__stock_roundtrip(struct FTestActionArgs* const args);
FTestActionResult ftest_editor_session_action004__blank_map(struct FTestActionArgs* const args);
FTestActionResult ftest_editor_session_action005__lua_roundtrip(struct FTestActionArgs* const args);
FTestActionResult ftest_editor_session_action006__blank_door_place(struct FTestActionArgs* const args);
FTestActionResult ftest_editor_session_action007__blank_door_undo(struct FTestActionArgs* const args);
FTestActionResult ftest_editor_session_action008__blank_door_check(struct FTestActionArgs* const args);

struct ftest_editor_session__variables
{
    uint64_t start_turn;
    uint64_t start_hash;
    int64_t frames;
};
static struct ftest_editor_session__variables s_vars = {0, 0, 0};

TbBool ftest_editor_session_init()
{
    ftest_append_action(ftest_editor_session_action001__open, 20, &s_vars);
    ftest_append_action(ftest_editor_session_action002__stay_frozen, 0, &s_vars);
    ftest_append_action(ftest_editor_session_action002b__preview, 0, &s_vars);
    ftest_append_action(ftest_editor_session_action003__stock_roundtrip, 0, &s_vars);
    ftest_append_action(ftest_editor_session_action004__blank_map, 0, &s_vars);
    ftest_append_action(ftest_editor_session_action005__lua_roundtrip, 0, &s_vars);
    ftest_append_action(ftest_editor_session_action006__blank_door_place, 0, &s_vars);
    ftest_append_action(ftest_editor_session_action007__blank_door_undo, 0, &s_vars);
    ftest_append_action(ftest_editor_session_action008__blank_door_check, 0, &s_vars);
    return true;
}

static uint64_t world_hash()
{
    uint64_t h = 1469598103934665603ULL;
    for (int64_t y = 0; y < kfx_sim_state.map_tiles_y; y++)
        for (int64_t x = 0; x < kfx_sim_state.map_tiles_x; x++)
        {
            const struct SlabMap* slb = get_slabmap_block(x, y);
            h = (h ^ (uint64_t)slb->kind) * 1099511628211ULL;
            h = (h ^ (uint64_t)slabmap_owner(slb)) * 1099511628211ULL;
        }
    for (ThingIndex i = 1; i < THINGS_COUNT; i++)
    {
        const struct Thing* t = thing_get(i);
        if (thing_exists(t) && (t->class_id == TCls_Creature || t->class_id == TCls_Object || t->class_id == TCls_Trap || t->class_id == TCls_Door))
        {
            h = (h ^ (uint64_t)t->mappos.x.val) * 1099511628211ULL;
            h = (h ^ (uint64_t)t->mappos.y.val) * 1099511628211ULL;
            h = (h ^ (uint64_t)t->health) * 1099511628211ULL;
        }
    }
    return h;
}

FTestActionResult ftest_editor_session_action001__open(struct FTestActionArgs* const args)
{
    ftest_util_reveal_map(PLAYER0);
    editor_open(1, false);
    if (!editor_is_active() || !simulation_is_suspended())
    {
        FTEST_FAIL_TEST("editor_open() did not activate the session and suspend the simulation");
        return FTRs_Go_To_Next_Action;
    }
    s_vars.start_turn = (uint64_t)get_gameturn();
    s_vars.start_hash = world_hash();
    s_vars.frames = 0;
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ftest_editor_session_action002__stay_frozen(struct FTestActionArgs* const args)
{
    if ((uint64_t)get_gameturn() != s_vars.start_turn)
    {
        FTEST_FAIL_TEST("game turn advanced from %" PRIu64 " to %" PRIu64 " while the editor was open",
            (uint64_t)(s_vars.start_turn), (uint64_t)get_gameturn());
        return FTRs_Go_To_Next_Action;
    }
    if (world_hash() != s_vars.start_hash)
    {
        FTEST_FAIL_TEST("the map or things changed after %" PRId64 " frames with the editor open", (int64_t)(s_vars.frames));
        return FTRs_Go_To_Next_Action;
    }
    if (++s_vars.frames < FROZEN_FRAMES)
        return FTRs_Repeat_Current_Action;
    return FTRs_Go_To_Next_Action;
}

// load_map_file() alone does not reset things, lights or action points (a real
// level start runs clear_game()/init_level() first): without this, the doors of
// the previous level would be saved into the next one.
static void reset_world_objects()
{
    clear_things_and_persons_data();
    light_initialise();
    delete_all_action_point_structures();
}

// Preview Motion runs the simulation, then puts everything back where the
// mapmaker had it.
static int64_t s_preview_stage = 0;
static uint64_t s_preview_turn0 = 0;
static uint64_t s_preview_moved_hash = 0;

FTestActionResult ftest_editor_session_action002b__preview(struct FTestActionArgs* const args)
{
    if (s_preview_stage == 0)
    {
        s_vars.start_hash = world_hash();
        s_preview_turn0 = (uint64_t)get_gameturn();
        editor_set_preview_motion(true);
        s_preview_stage = 1;
        s_vars.frames = 0;
        return FTRs_Repeat_Current_Action;
    }
    if (s_preview_stage == 1)
    {
        if (((uint64_t)get_gameturn() - s_preview_turn0) < 200 && ++s_vars.frames < 4000)
            return FTRs_Repeat_Current_Action;
        s_preview_moved_hash = world_hash();
        editor_set_preview_motion(false);
        s_preview_stage = 2;
        return FTRs_Repeat_Current_Action;
    }
    const uint64_t restored = world_hash();
    JUSTLOG("Preview motion: %" PRIu64 " turns ran, world %s while running, %s after restore",
        (uint64_t)get_gameturn() - s_preview_turn0, s_preview_moved_hash != s_vars.start_hash ? "changed" : "unchanged",
        restored == s_vars.start_hash ? "identical" : "DIFFERENT");
    if (restored != s_vars.start_hash)
        FTEST_FAIL_TEST("the world is not back as it was after a motion preview");
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ftest_editor_session_action003__stock_roundtrip(struct FTestActionArgs* const args)
{
    char dir[512];
    editor_level_save_dir(ROUNDTRIP_LVNUM, dir, sizeof(dir));
    int64_t checked = 0;
    for (int64_t pass = 0; pass < 2; pass++)
    for (LevelNumber lv = 1; lv <= 14; lv++)
    {
        // Pass 0 lets the editor choose the format, pass 1 forces the classic one.
        const enum EditorSaveFormat fmt = (pass == 0) ? EdSaveFmt_Auto : EdSaveFmt_ForceClassic;
        reset_world_objects();
        if (!load_map_file(lv))
            continue; // not every campaign layout has all of them
        const int64_t w = kfx_sim_state.map_tiles_x, h = kfx_sim_state.map_tiles_y;
        std::vector<unsigned char> kinds((size_t)(w * h)), owners((size_t)(w * h));
        for (int64_t y = 0; y < h; y++)
            for (int64_t x = 0; x < w; x++)
            {
                const struct SlabMap* slb = get_slabmap_block(x, y);
                kinds[(size_t)(y * w + x)] = (unsigned char)slb->kind;
                owners[(size_t)(y * w + x)] = (unsigned char)slabmap_owner(slb);
            }
        if (!editor_save_map(ROUNDTRIP_LVNUM, dir, fmt, "Round trip", 1, 0, NULL))
        {
            FTEST_FAIL_TEST("editor_save_map() failed for stock level %" PRId64, (int64_t)lv);
            return FTRs_Go_To_Next_Action;
        }
        reset_world_objects();
        if (!load_map_file(ROUNDTRIP_LVNUM))
        {
            FTEST_FAIL_TEST("load_map_file() failed for the saved copy of stock level %" PRId64, (int64_t)lv);
            return FTRs_Go_To_Next_Action;
        }
        if (kfx_sim_state.map_tiles_x != w || kfx_sim_state.map_tiles_y != h)
        {
            FTEST_FAIL_TEST("stock level %" PRId64 " changed size in the round trip", (int64_t)lv);
            return FTRs_Go_To_Next_Action;
        }
        int64_t bad = 0;
        std::string samples;
        for (int64_t y = 0; y < h; y++)
            for (int64_t x = 0; x < w; x++)
            {
                const struct SlabMap* slb = get_slabmap_block(x, y);
                if ((unsigned char)slb->kind != kinds[(size_t)(y * w + x)]
                    || (unsigned char)slabmap_owner(slb) != owners[(size_t)(y * w + x)])
                {
                    if (bad++ < 5)
                        samples += " (" + std::to_string(x) + "," + std::to_string(y) + ": " + std::to_string((int64_t)slb->kind) + "/"
                            + std::to_string((int64_t)slabmap_owner(slb)) + " was " + std::to_string((int64_t)kinds[(size_t)(y * w + x)])
                            + "/" + std::to_string((int64_t)owners[(size_t)(y * w + x)]) + ")";
                }
            }
        if (bad > 0)
        {
            FTEST_FAIL_TEST("stock level %" PRId64 ": %" PRId64 " slabs differ after the round trip:%s", (int64_t)lv, (int64_t)(bad), samples.c_str());
            return FTRs_Go_To_Next_Action;
        }
        if (pass == 1)
        // The classic save also writes the derived files: compare with the
        // stock originals (log only -- the engine rebuilds columns on load, so
        // a difference is information, not failure).
        {
            char orig[600], mine[600];
            const char* exts[3] = { "dat", "wib", "clm" };
            for (int64_t e = 0; e < 3; e++)
            {
                snprintf(orig, sizeof(orig), "campgns/keeporig/map%05" PRId64 ".%s", (int64_t)lv, exts[e]);
                snprintf(mine, sizeof(mine), "%s/map%05" PRId64 ".%s", dir, (int64_t)ROUNDTRIP_LVNUM, exts[e]);
                std::ifstream a(orig, std::ios::binary), b(mine, std::ios::binary);
                std::string sa((std::istreambuf_iterator<char>(a)), std::istreambuf_iterator<char>());
                std::string sb((std::istreambuf_iterator<char>(b)), std::istreambuf_iterator<char>());
                JUSTLOG("Derived .%s of level %" PRId64 ": original %zu bytes, saved %zu bytes, %s", exts[e], (int64_t)lv,
                    sa.size(), sb.size(), sa == sb ? "identical" : "different");
            }
        }
        checked++;
    }
    JUSTLOG("Stock-map round trip: %" PRId64 " levels identical", (int64_t)(checked));
    if (checked == 0)
        FTEST_FAIL_TEST("no stock level could be loaded for the round trip");
    return FTRs_Go_To_Next_Action;
}

// A New Map: the requested size, every slab solid rock and neutral, nothing on
// it, and it survives a save and reload in both formats.
FTestActionResult ftest_editor_session_action004__blank_map(struct FTestActionArgs* const args)
{
    const LevelNumber lv = 90021;
    reset_world_objects();
    if (!create_blank_map(lv, 40, 30, 2))
    {
        FTEST_FAIL_TEST("create_blank_map() failed");
        return FTRs_Go_To_Next_Action;
    }
    if (kfx_sim_state.map_tiles_x != 40 || kfx_sim_state.map_tiles_y != 30)
    {
        FTEST_FAIL_TEST("blank map is %" PRId64 " x %" PRId64 ", expected 40 x 30", (int64_t)kfx_sim_state.map_tiles_x, (int64_t)kfx_sim_state.map_tiles_y);
        return FTRs_Go_To_Next_Action;
    }
    for (int64_t y = 0; y < 30; y++)
        for (int64_t x = 0; x < 40; x++)
        {
            const struct SlabMap* slb = get_slabmap_block(x, y);
            if (slb->kind != SlbT_ROCK || slabmap_owner(slb) != kfx_config_state.neutral_player_num)
            {
                FTEST_FAIL_TEST("blank map slab (%" PRId64 ",%" PRId64 ") is not neutral rock", (int64_t)(x), (int64_t)(y));
                return FTRs_Go_To_Next_Action;
            }
        }
    for (ThingIndex i = 1; i < THINGS_COUNT; i++)
        if (thing_exists(thing_get(i)))
        {
            FTEST_FAIL_TEST("blank map already has a thing (%" PRId64 ")", (int64_t)i);
            return FTRs_Go_To_Next_Action;
        }
    char dir[512];
    editor_level_save_dir(lv, dir, sizeof(dir));
    for (int64_t pass = 0; pass < 2; pass++)
    {
        if (!editor_save_map(lv, dir, pass == 0 ? EdSaveFmt_ForceKeeperFX : EdSaveFmt_ForceClassic, "Blank", 1, 0, NULL)
            || (set_map_size(85, 85), init_map_size(lv), !load_map_file(lv)))
        {
            FTEST_FAIL_TEST("blank map did not save and reload (pass %" PRId64 ")", (int64_t)(pass));
            return FTRs_Go_To_Next_Action;
        }
        if (kfx_sim_state.map_tiles_x != 40 || kfx_sim_state.map_tiles_y != 30
            || get_slabmap_block(20, 15)->kind != SlbT_ROCK)
        {
            FTEST_FAIL_TEST("blank map changed in the save/reload round trip (pass %" PRId64 ")", (int64_t)(pass));
            return FTRs_Go_To_Next_Action;
        }
    }
    JUSTLOG("Blank map: created, saved and reloaded in both formats");
    return FTRs_Go_To_Next_Action;
}

// A level's map%05d.lua is read when the editor opens it, survives a Save As to
// another number byte-for-byte in both formats, and a Lua-only level is not
// given a .txt.
FTestActionResult ftest_editor_session_action005__lua_roundtrip(struct FTestActionArgs* const args)
{
    const LevelNumber src = 90022, dst = 90023;
    const std::string lua = "-- caf\xC3\xA9\r\nfunction OnGameStart()\r\n  RegisterTimerEvent(\"T\", 10, false)\r\nend\r\n";
    reset_world_objects();
    if (!create_blank_map(src, 40, 30, 2))
    {
        FTEST_FAIL_TEST("create_blank_map() failed");
        return FTRs_Go_To_Next_Action;
    }
    char src_dir[512], dst_dir[512];
    editor_level_save_dir(src, src_dir, sizeof(src_dir));
    editor_level_save_dir(dst, dst_dir, sizeof(dst_dir));
    const std::string src_lua = std::string(src_dir) + "/map90022.lua";
    const std::string dst_lua = std::string(dst_dir) + "/map90023.lua";
    const std::string dst_txt = std::string(dst_dir) + "/map90023.txt";
    auto slurp = [](const std::string& path, bool& exists) {
        std::ifstream f(path, std::ios::binary);
        exists = f.good();
        return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    };
    { std::ofstream f(src_lua, std::ios::binary); f << lua; }
    std::remove(dst_lua.c_str());
    std::remove(dst_txt.c_str());
    editor_open(src, false);
    if (!editor_current_level_has_lua() || lua != editor_current_level_lua_text())
    {
        FTEST_FAIL_TEST("editor_open() did not read map90022.lua");
        return FTRs_Go_To_Next_Action;
    }
    for (int64_t pass = 0; pass < 2; pass++)
    {
        std::remove(dst_lua.c_str());
        if (!editor_save_map(dst, dst_dir, pass == 0 ? EdSaveFmt_ForceKeeperFX : EdSaveFmt_ForceClassic, "Lua", 1, 0, NULL))
        {
            FTEST_FAIL_TEST("editor_save_map() failed (pass %" PRId64 ")", (int64_t)(pass));
            return FTRs_Go_To_Next_Action;
        }
        bool exists = false;
        if (slurp(dst_lua, exists) != lua || !exists)
        {
            FTEST_FAIL_TEST("saved .lua differs from the source (pass %" PRId64 ")", (int64_t)(pass));
            return FTRs_Go_To_Next_Action;
        }
    }
    // Lua-only: no script text, so no stub .txt appears.
    editor_set_current_level_script_text("");
    std::remove(dst_txt.c_str());
    if (!editor_save_map(dst, dst_dir, EdSaveFmt_ForceKeeperFX, "Lua", 1, 0, NULL))
    {
        FTEST_FAIL_TEST("editor_save_map() failed for the Lua-only save");
        return FTRs_Go_To_Next_Action;
    }
    bool has_txt = false;
    slurp(dst_txt, has_txt);
    if (has_txt)
    {
        FTEST_FAIL_TEST("a Lua-only level was given a stub .txt");
        return FTRs_Go_To_Next_Action;
    }
    // Dropping the Lua removes the stale file on the next save.
    editor_set_current_level_lua_text("", false);
    if (!editor_save_map(dst, dst_dir, EdSaveFmt_ForceKeeperFX, "Lua", 1, 0, NULL))
    {
        FTEST_FAIL_TEST("editor_save_map() failed after removing the Lua");
        return FTRs_Go_To_Next_Action;
    }
    bool still = false;
    slurp(dst_lua, still);
    if (still)
    {
        FTEST_FAIL_TEST("a stale .lua was left behind");
        return FTRs_Go_To_Next_Action;
    }
    // New Map with the Lua option starts with the template and no .txt.
    editor_set_new_map_lua(true);
    editor_open(dst, true);
    if (!editor_current_level_has_lua() || std::string(editor_current_level_lua_text()).find("OnGameStart") == std::string::npos
        || editor_current_level_script_text()[0] != '\0')
    {
        FTEST_FAIL_TEST("New Map with the Lua option did not start from the Lua template");
        return FTRs_Go_To_Next_Action;
    }
    editor_open(dst, true); // one-shot: the next new map has no Lua
    if (editor_current_level_has_lua())
    {
        FTEST_FAIL_TEST("the New Map Lua option was not one-shot");
        return FTRs_Go_To_Next_Action;
    }
    JUSTLOG("Lua: read, saved byte-identical in both formats, Lua-only has no .txt, new-map option");
    return FTRs_Go_To_Next_Action;
}

// A door placed on a claimed path in a New Map, then undone with the toolbox's
// Undo, leaves the owner's claimed path.
static int64_t s_door_polls = 0;
#define BLANK_DOOR_X 20
#define BLANK_DOOR_Y 15

FTestActionResult ftest_editor_session_action006__blank_door_place(struct FTestActionArgs* const args)
{
    reset_world_objects();
    if (!create_blank_map(90031, 40, 30, 2))
    {
        FTEST_FAIL_TEST("create_blank_map() failed");
        return FTRs_Go_To_Next_Action;
    }
    editor_open(90031, true);
    // A short claimed path, PLAYER0's, in solid neutral rock.
    for (int64_t k = -1; k <= 1; k++)
        place_slab_type_on_map(SlbT_CLAIMED, slab_subtile(BLANK_DOOR_X + k, 0), slab_subtile(BLANK_DOOR_Y, 0), PLAYER0, 0);
    for (int64_t k = -3; k <= 3; k++)
        for (int64_t m = -2; m <= 2; m++)
            do_slab_efficiency_alteration(BLANK_DOOR_X + k, BLANK_DOOR_Y + m);
    // A real click lands on any of the slab's nine subtiles, rarely the middle one.
    const int64_t off_x = (BLANK_DOOR_X * 3 + 0) * 256 + 100, off_y = (BLANK_DOOR_Y * 3 + 2) * 256 + 100;
    set_players_packet_action(get_player(PLAYER0), PckA_EditorRedoDoor, off_x, off_y, 1, PLAYER0);
    s_door_polls = 0;
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ftest_editor_session_action007__blank_door_undo(struct FTestActionArgs* const args)
{
    struct Thing* door = find_base_thing_on_mapwho(TCls_Door, 0, slab_subtile(BLANK_DOOR_X, 1), slab_subtile(BLANK_DOOR_Y, 1));
    if (thing_is_invalid(door))
    {
        if (++s_door_polls > 60)
        {
            FTEST_FAIL_TEST("the door never appeared in the new map (slab kind %" PRId64 ")", (int64_t)get_slabmap_block(BLANK_DOOR_X, BLANK_DOOR_Y)->kind);
            return FTRs_Go_To_Next_Action;
        }
        return FTRs_Repeat_Current_Action;
    }
    JUSTLOG("Blank-map door placed; slab kind %" PRId64 ", undo entries %" PRId64, (int64_t)get_slabmap_block(BLANK_DOOR_X, BLANK_DOOR_Y)->kind, (int64_t)(editor_journal_undo_count()));
    editor_journal_do_undo();
    s_door_polls = 0;
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ftest_editor_session_action008__blank_door_check(struct FTestActionArgs* const args)
{
    struct Thing* door = find_base_thing_on_mapwho(TCls_Door, 0, slab_subtile(BLANK_DOOR_X, 1), slab_subtile(BLANK_DOOR_Y, 1));
    if (!thing_is_invalid(door))
    {
        if (++s_door_polls > 60)
        {
            FTEST_FAIL_TEST("the door is still there after Undo");
            return FTRs_Go_To_Next_Action;
        }
        return FTRs_Repeat_Current_Action;
    }
    const struct SlabMap* slb = get_slabmap_block(BLANK_DOOR_X, BLANK_DOOR_Y);
    JUSTLOG("Blank-map door undone: slab kind %" PRId64 " (%s) owner %" PRId64, (int64_t)slb->kind, slab_code_name(slb->kind), (int64_t)slabmap_owner(slb));
    if (slb->kind != SlbT_CLAIMED || slabmap_owner(slb) != PLAYER0)
        FTEST_FAIL_TEST("undoing the door left slab kind %" PRId64 " (%s), owner %" PRId64 "; expected the claimed path owned by PLAYER0",
            (int64_t)slb->kind, slab_code_name(slb->kind), (int64_t)slabmap_owner(slb));
    return FTRs_Go_To_Next_Action;
}

} // extern "C"

#endif // FUNCTESTING
