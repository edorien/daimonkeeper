// Coverage-driven addition: the merged unit-test+ftest coverage report
// (docs/Architecture/testing-harness.md §7.7) showed kfx_sim's biggest
// absolute gaps concentrated in real gameplay interactions unit tests
// don't naturally reach -- creature_states_combt.c (10.3% line coverage),
// thing_creature.c, magic_powers.c, power_hand.c. This test drives all
// four directly: a real creature-vs-creature fight (combat state
// machine, damage application) and a real POWER_HAND pickup/drop (the
// headless-safe way -- magic_use_available_power_on_thing(), not the
// cursor/mouse-picking path ftest_bug_invisible_units_cant_select.c
// found doesn't resolve under -headless).
//
// Uses map00011 ("Hearth") from the original campaign: its own script
// already makes POWER_HAND/POWER_SLAP available and stocks ORC/TROLL/
// BARBARIAN in creatrs/ -- picked over building a level=1 arena from
// scratch (like most of this directory's other tests) so the test roster
// matches a real, played level rather than an arbitrary choice.
#include "ftest_creature_combat_power_hand.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include "../ftest.h"
#include "../ftest_util.h"

#include "game_legacy.h"
#include "config_keeperfx.h"
#include "config_creature.h"
#include "player_instances.h"
#include "player_computer.h"
#include "magic_powers.h"
#include "power_hand.h"
#include "thing_stats.h"
#include "dungeon_data.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

// Spawns next to the player's own dungeon heart rather than carving a
// fresh arena elsewhere on the map: a first attempt carving SlbT_CLAIMED
// at an arbitrary, disconnected location (e.g. slab (40,40)) turned out
// to silently resolve to SlbT_PATH owned by PLAYER_NEUTRAL instead --
// replace_slab_from_script()'s claim logic evidently depends on adjacency
// to already-owned territory, unlike a plain slab-kind paint. The area
// immediately around a real starting dungeon heart is always genuinely
// CLAIMED and owned by the player, with no such risk.
#define SPAWN_OFFSET_STL 3

struct ftest_creature_combat_power_hand__variables
{
    ThingIndex ally_idx;
    ThingIndex enemy_idx;
    HitPoints enemy_health_before_combat;
    TbBool hand_grab_attempted;
    TbBool hand_grab_succeeded;
    MapSubtlCoord drop_stl_x;
    MapSubtlCoord drop_stl_y;
};
struct ftest_creature_combat_power_hand__variables ftest_creature_combat_power_hand__vars = {
    .ally_idx = 0,
    .enemy_idx = 0,
    .enemy_health_before_combat = 0,
    .hand_grab_attempted = false,
    .hand_grab_succeeded = false,
    .drop_stl_x = 0,
    .drop_stl_y = 0,
};

FTestActionResult ftest_creature_combat_power_hand_action001__setup(struct FTestActionArgs* const args);
FTestActionResult ftest_creature_combat_power_hand_action002__wait_for_combat(struct FTestActionArgs* const args);
FTestActionResult ftest_creature_combat_power_hand_action003__power_hand_pickup_and_drop(struct FTestActionArgs* const args);
FTestActionResult ftest_creature_combat_power_hand_action004__verify_and_end(struct FTestActionArgs* const args);

TbBool ftest_creature_combat_power_hand_init()
{
    ftest_append_action(ftest_creature_combat_power_hand_action001__setup, 0, &ftest_creature_combat_power_hand__vars);
    ftest_append_action(ftest_creature_combat_power_hand_action002__wait_for_combat, 10, &ftest_creature_combat_power_hand__vars);
    ftest_append_action(ftest_creature_combat_power_hand_action003__power_hand_pickup_and_drop, 0, &ftest_creature_combat_power_hand__vars);
    ftest_append_action(ftest_creature_combat_power_hand_action004__verify_and_end, 5, &ftest_creature_combat_power_hand__vars);

    return true;
}

FTestActionResult ftest_creature_combat_power_hand_action001__setup(struct FTestActionArgs* const args)
{
    struct ftest_creature_combat_power_hand__variables* const vars = args->data;

    ftest_util_reveal_map(PLAYER0);

    struct Dungeon* dungeon = get_players_dungeon(get_player(PLAYER0));
    struct Thing* heart = thing_get(dungeon->dnheart_idx);
    if (thing_is_invalid(heart))
    {
        FTEST_FAIL_TEST("Failed to find PLAYER0's dungeon heart");
        return FTRs_Go_To_Next_Action;
    }
    MapSubtlCoord heart_stl_x = heart->mappos.x.stl.num;
    MapSubtlCoord heart_stl_y = heart->mappos.y.stl.num;

    ThingModel ally_model = (ThingModel)creature_model_id("ORC");
    ThingModel enemy_model = (ThingModel)creature_model_id("BARBARIAN");
    if (ally_model < 1 || enemy_model < 1)
    {
        FTEST_FAIL_TEST("Failed to resolve creature model id (ORC=%" PRId64 ", BARBARIAN=%" PRId64 ")", (int64_t)ally_model, (int64_t)enemy_model);
        return FTRs_Go_To_Next_Action;
    }

    struct Coord3d ally_pos;
    struct Coord3d enemy_pos;
    ally_pos.x.val = subtile_coord_center(heart_stl_x - SPAWN_OFFSET_STL);
    ally_pos.y.val = subtile_coord_center(heart_stl_y);
    ally_pos.z.val = 0;
    enemy_pos.x.val = subtile_coord_center(heart_stl_x + SPAWN_OFFSET_STL);
    enemy_pos.y.val = subtile_coord_center(heart_stl_y);
    enemy_pos.z.val = 0;

    struct Thing* ally = ftest_util_create_creature(ally_pos.x.val, ally_pos.y.val, PLAYER0, 9, ally_model);
    if (thing_is_invalid(ally))
    {
        FTEST_FAIL_TEST("Failed to create ally creature (ORC)");
        return FTRs_Go_To_Next_Action;
    }

    // Enemy is owned by the hero (good) side, not another keeper -- same
    // "attack anything not your own" combat path either way, and avoids
    // this test depending on a second dungeon heart/player setup.
    struct Thing* enemy = ftest_util_create_creature(enemy_pos.x.val, enemy_pos.y.val, PLAYER_GOOD, 1, enemy_model);
    if (thing_is_invalid(enemy))
    {
        FTEST_FAIL_TEST("Failed to create enemy creature (BARBARIAN)");
        return FTRs_Go_To_Next_Action;
    }
    // Nerf so combat resolves within this test's turn budget, same
    // technique ftest_bug_ai_bridge.c uses -- the point here is exercising
    // the combat/damage state machine, not a realistic fight duration.
    enemy->health = 1;

    vars->ally_idx = ally->index;
    vars->enemy_idx = enemy->index;
    vars->enemy_health_before_combat = enemy->health;
    // Drop target for action003, right next to the heart -- guaranteed
    // CLAIMED/owned ground, unlike this file's first (reverted) attempt
    // at carving a fresh arena elsewhere (see the comment above).
    vars->drop_stl_x = heart_stl_x - SPAWN_OFFSET_STL;
    vars->drop_stl_y = heart_stl_y + SPAWN_OFFSET_STL;

    ftest_util_move_camera_to_thing(heart, PLAYER0);

    return FTRs_Go_To_Next_Action;
}

FTestActionResult ftest_creature_combat_power_hand_action002__wait_for_combat(struct FTestActionArgs* const args)
{
    struct ftest_creature_combat_power_hand__variables* const vars = args->data;

    struct Thing* enemy = thing_get(vars->enemy_idx);
    if (thing_is_invalid(enemy) || !thing_is_creature(enemy))
    {
        // Enemy slot no longer holds a live creature -- died and was
        // recycled. Combat resolved; proceed.
        FTESTLOG("Enemy creature no longer present at turn %" PRId64 " -- treating as killed in combat", (int64_t)(get_gameturn()));
        return FTRs_Go_To_Next_Action;
    }

    if (enemy->health < vars->enemy_health_before_combat)
    {
        FTESTLOG("Enemy creature took damage (health %" PRId64 " -> %" PRId64 ") at turn %" PRId64, (int64_t)vars->enemy_health_before_combat, (int64_t)enemy->health, (int64_t)(get_gameturn()));
        return FTRs_Go_To_Next_Action;
    }

    // Give the ally's AI a bounded number of turns to notice and engage.
    if (get_gameturn() >= args->intended_start_at_game_turn + 200)
    {
        FTEST_FAIL_TEST("Enemy creature took no damage within the turn budget -- combat never engaged");
        return FTRs_Go_To_Next_Action;
    }

    return FTRs_Repeat_Current_Action;
}

FTestActionResult ftest_creature_combat_power_hand_action003__power_hand_pickup_and_drop(struct FTestActionArgs* const args)
{
    struct ftest_creature_combat_power_hand__variables* const vars = args->data;

    struct Thing* ally = thing_get(vars->ally_idx);
    if (thing_is_invalid(ally) || !thing_is_creature(ally))
    {
        FTEST_FAIL_TEST("Ally creature is no longer present -- cannot test POWER_HAND");
        return FTRs_Go_To_Next_Action;
    }

    vars->hand_grab_attempted = true;
    TbResult pickup_result = magic_use_available_power_on_thing(PLAYER0, PwrK_HAND, 0,
        ally->mappos.x.stl.num, ally->mappos.y.stl.num, ally, PwMod_Default);
    if (pickup_result != Lb_SUCCESS)
    {
        FTEST_FAIL_TEST("magic_use_available_power_on_thing(PwrK_HAND) failed to pick up ally creature");
        return FTRs_Go_To_Next_Action;
    }
    vars->hand_grab_succeeded = true;

    if (!dump_first_held_thing_on_map(PLAYER0, vars->drop_stl_x, vars->drop_stl_y, 1))
    {
        FTEST_FAIL_TEST("dump_first_held_thing_on_map failed to drop ally creature at (%" PRId64 ",%" PRId64 ")", (int64_t)(vars->drop_stl_x), (int64_t)(vars->drop_stl_y));
        return FTRs_Go_To_Next_Action;
    }

    return FTRs_Go_To_Next_Action;
}

FTestActionResult ftest_creature_combat_power_hand_action004__verify_and_end(struct FTestActionArgs* const args)
{
    struct ftest_creature_combat_power_hand__variables* const vars = args->data;

    if (!vars->hand_grab_attempted || !vars->hand_grab_succeeded)
    {
        FTEST_FAIL_TEST("POWER_HAND pickup was never successfully attempted");
        return FTRs_Go_To_Next_Action;
    }

    struct Thing* ally = thing_get(vars->ally_idx);
    if (thing_is_invalid(ally) || !thing_is_creature(ally))
    {
        FTEST_FAIL_TEST("Ally creature is missing after being dropped");
        return FTRs_Go_To_Next_Action;
    }
    if (thing_is_picked_up(ally))
    {
        FTEST_FAIL_TEST("Ally creature is still held in hand after drop");
        return FTRs_Go_To_Next_Action;
    }

    FTESTLOG("Test passed: combat + POWER_HAND pickup/drop both exercised successfully");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
