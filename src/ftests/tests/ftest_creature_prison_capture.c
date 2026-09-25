// Coverage-driven addition, same family as ftest_creature_temple_prayer.c
// (see docs/Architecture/testing-harness.md §7.7). Targets the "room
// owner vs. dropped-creature owner differ" mechanic that captures enemy
// creatures: thing_creature.c's controlled_creature_drop_thing(), in its
// RoRoF_Prison branch, only converts an unconscious dropped creature into
// a captive (make_creature_conscious() + CrSt_CreatureArrivedAtPrison +
// CCFlg_NoCompControl) when the *dropping* thing's owner matches the
// room's owner (`room->owner == creatng->owner`) -- the *dropped*
// creature's own owner is left untouched, which is exactly what makes
// capturing an enemy meaningful (jailbreak_possible(), further down in
// creature_states_prisn.c, keys off that same room-vs-captive owner
// mismatch to let the prisoner attempt to escape).
//
// Originally this test tried to reach that code path the "real" way, via
// the player's POWER_HAND spell (magic_use_available_power_on_thing +
// dump_first_held_thing_on_map). That is not how KeeperFX actually
// captures enemies: PwrK_HAND's config (config/fxdata/magic.cfg)
// Castability is `ANYWHERE OWNED_CRTRS CUSTODY_CRTRS ALL_FOOD ALL_GOLD
// OWNED_OBJECTS_PICKUP` -- no UNCONSC_CRTRS flag, and OWNED_CRTRS only
// (not enemy creatures), so can_cast_power_on_thing() (magic_powers.c)
// unconditionally refuses to let a player hand-pick-up an unconscious
// enemy. The real mechanic is creature-AI-driven: an imp/special-digger's
// own task stack (add_unclaimed_unconscious_bodies_to_imp_stack(),
// spdigger_stack.c) notices unconscious enemies and drags them to a
// prison room itself, eventually calling the same
// controlled_creature_drop_thing() this test now calls directly. Letting
// a spawned imp path-find and perform that drag naturally would be far
// slower and less deterministic than the pattern already established in
// this directory (temple/lair/garden force preconditions directly rather
// than waiting on organic AI), so this test places the captive straight
// at the prison's centre subtile (matching a drag-and-drop's end state)
// and calls controlled_creature_drop_thing() with a spawned PLAYER0
// "guard" creature standing in the same room as the synthetic "dropper"
// (the function only reads its ->owner, ->mappos, and CreatureControl --
// the dungeon heart thing doesn't provide a usable position/CreatureControl
// for this) to exercise the exact engine code a real capture goes through.
#include "ftest_creature_prison_capture.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include "../ftest.h"
#include "../ftest_util.h"

#include "game_legacy.h"
#include "config_keeperfx.h"
#include "config_creature.h"
#include "config_terrain.h"
#include "player_instances.h"
#include "dungeon_data.h"
#include "creature_states.h"
#include "creature_control.h"
#include "thing_creature.h"
#include "room_data.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

// User-specified size: a real 3x3 room, not the 4x4 this directory's
// other new tests use (bigger sizes were only ever a comfort margin, not
// a functional requirement).
#define PRISON_OFFSET_SLB 3
#define PRISON_SIZE 3

struct ftest_creature_prison_capture__variables
{
    ThingIndex captive_idx;
    ThingIndex guard_idx;
};
struct ftest_creature_prison_capture__variables ftest_creature_prison_capture__vars = {
    .captive_idx = 0,
    .guard_idx = 0,
};

FTestActionResult ftest_creature_prison_capture_action001__setup(struct FTestActionArgs* const args);
FTestActionResult ftest_creature_prison_capture_action002__capture(struct FTestActionArgs* const args);
FTestActionResult ftest_creature_prison_capture_action003__verify(struct FTestActionArgs* const args);

TbBool ftest_creature_prison_capture_init()
{
    ftest_append_action(ftest_creature_prison_capture_action001__setup, 0, &ftest_creature_prison_capture__vars);
    ftest_append_action(ftest_creature_prison_capture_action002__capture, 5, &ftest_creature_prison_capture__vars);
    ftest_append_action(ftest_creature_prison_capture_action003__verify, 1, &ftest_creature_prison_capture__vars);

    return true;
}

FTestActionResult ftest_creature_prison_capture_action001__setup(struct FTestActionArgs* const args)
{
    struct ftest_creature_prison_capture__variables* const vars = args->data;

    ftest_util_reveal_map(PLAYER0);

    struct Dungeon* dungeon = get_players_dungeon(get_player(PLAYER0));
    struct Thing* heart = thing_get(dungeon->dnheart_idx);
    if (thing_is_invalid(heart))
    {
        FTEST_FAIL_TEST("Failed to find PLAYER0's dungeon heart");
        return FTRs_Go_To_Next_Action;
    }
    MapSlabCoord heart_slb_x = subtile_slab(heart->mappos.x.stl.num);
    MapSlabCoord heart_slb_y = subtile_slab(heart->mappos.y.stl.num);

    MapSlabCoord room_slb_x = heart_slb_x + PRISON_OFFSET_SLB;
    MapSlabCoord room_slb_y = heart_slb_y;

    if (!ftest_util_replace_slabs(room_slb_x, room_slb_y, room_slb_x + PRISON_SIZE, room_slb_y + PRISON_SIZE, SlbT_PRISON, PLAYER0))
    {
        FTEST_FAIL_TEST("Failed to build prison room at slab (%" PRId64 ",%" PRId64 ")", (int64_t)(room_slb_x), (int64_t)(room_slb_y));
        return FTRs_Go_To_Next_Action;
    }
    // map00011's script leaves PRISON researchable but not buildable
    // (ROOM_AVAILABLE(ALL_PLAYERS,PRISON,1,0)) -- same wrinkle
    // ftest_creature_temple_prayer.c found for TEMPLE.
    set_room_available(PLAYER0, RoK_PRISON, 1, 1);

    // Centre of the 3x3 room (offset+1 lands on its middle slab) -- the
    // user-specified scenario shape: a creature placed/dropped right into
    // the middle of the room, not walked in from outside.
    MapSlabCoord room_centre_slb_x = room_slb_x + 1;
    MapSlabCoord room_centre_slb_y = room_slb_y + 1;
    struct Coord3d room_centre_pos;
    set_coords_to_slab_center(&room_centre_pos, room_centre_slb_x, room_centre_slb_y);

    ThingModel orc_model = (ThingModel)creature_model_id("ORC");
    if (orc_model < 1)
    {
        FTEST_FAIL_TEST("Failed to resolve creature model id (ORC)");
        return FTRs_Go_To_Next_Action;
    }
    // A PLAYER0-owned creature standing in the prison plays the role of
    // "creatng" (the dropper) in controlled_creature_drop_thing() below --
    // the function only reads its ->owner and ->mappos (to resolve which
    // room it's standing in), plus a valid CreatureControl, none of which
    // the dungeon heart thing provides usefully here.
    struct Thing* guard = ftest_util_create_creature(room_centre_pos.x.val, room_centre_pos.y.val, PLAYER0, 1, orc_model);
    if (thing_is_invalid(guard))
    {
        FTEST_FAIL_TEST("Failed to create guard creature (ORC) in the prison");
        return FTRs_Go_To_Next_Action;
    }
    vars->guard_idx = guard->index;

    ThingModel creature_model = (ThingModel)creature_model_id("BARBARIAN");
    if (creature_model < 1)
    {
        FTEST_FAIL_TEST("Failed to resolve creature model id (BARBARIAN)");
        return FTRs_Go_To_Next_Action;
    }

    // Owned by PLAYER_GOOD (the hero side), not PLAYER0 -- the whole
    // point is capturing an enemy, not one of PLAYER0's own creatures.
    // Placed directly at the prison's centre subtile, matching a
    // just-dragged-and-dropped captive's end position.
    struct Thing* captive = ftest_util_create_creature(room_centre_pos.x.val, room_centre_pos.y.val, PLAYER_GOOD, 1, creature_model);
    if (thing_is_invalid(captive))
    {
        FTEST_FAIL_TEST("Failed to create captive creature (BARBARIAN)");
        return FTRs_Go_To_Next_Action;
    }
    // Only unconscious creatures can be captured on drop
    // (creature_is_being_unconscious() check in thing_creature.c's
    // RoRoF_Prison drop handler) -- matches how a real defeated-in-combat
    // enemy would arrive at this point, without needing an actual fight.
    make_creature_unconscious(captive);

    vars->captive_idx = captive->index;

    ftest_util_move_camera_to_thing(heart, PLAYER0);

    return FTRs_Go_To_Next_Action;
}

FTestActionResult ftest_creature_prison_capture_action002__capture(struct FTestActionArgs* const args)
{
    struct ftest_creature_prison_capture__variables* const vars = args->data;

    struct Thing* captive = thing_get(vars->captive_idx);
    if (thing_is_invalid(captive) || !thing_is_creature(captive))
    {
        FTEST_FAIL_TEST("Captive creature is no longer present before capture");
        return FTRs_Go_To_Next_Action;
    }
    struct Thing* guard = thing_get(vars->guard_idx);
    if (thing_is_invalid(guard) || !thing_is_creature(guard))
    {
        FTEST_FAIL_TEST("Guard creature is no longer present before capture");
        return FTRs_Go_To_Next_Action;
    }

    // See this file's header comment: PwrK_HAND can't target an
    // unconscious enemy at all (config/fxdata/magic.cfg's Castability has
    // no UNCONSC_CRTRS flag and only OWNED_CRTRS), so a real capture never
    // goes through the player's hand. Instead call the same engine
    // function a real drag-and-drop capture (imp AI, or direct-control
    // drop) ultimately reaches: thing_creature.c's
    // controlled_creature_drop_thing(). `guard` stands in for the
    // "dropper" -- only its ->owner and ->mappos (which room it's
    // standing in) matter to the RoRoF_Prison branch being exercised.
    controlled_creature_drop_thing(guard, captive, PLAYER0);

    return FTRs_Go_To_Next_Action;
}

FTestActionResult ftest_creature_prison_capture_action003__verify(struct FTestActionArgs* const args)
{
    struct ftest_creature_prison_capture__variables* const vars = args->data;

    struct Thing* captive = thing_get(vars->captive_idx);
    if (thing_is_invalid(captive) || !thing_is_creature(captive))
    {
        FTEST_FAIL_TEST("Captive creature is no longer present after being dropped");
        return FTRs_Go_To_Next_Action;
    }

    // controlled_creature_drop_thing()'s RoRoF_Prison branch sets
    // CrSt_CreatureArrivedAtPrison synchronously, but that state's own
    // handler (creature_arrived_at_prison(), creature_states_prisn.c)
    // only runs on the *next* game turn, and immediately advances the
    // creature on to CrSt_CreatureInPrison via add_creature_to_work_room().
    // From there the room's normal idle "shuffle to a new spot" behaviour
    // (process_prison_visuals()) can transiently bounce active_state
    // through CrSt_MoveToPosition (continue_state left at
    // CrSt_CreatureInPrison) every so often -- entirely healthy captivity
    // behaviour, not a failure. So rather than pin one snapshot of
    // active_state, wait for the one signal that's stable across all of
    // that churn: cctrl->work_room_id being set to our prison room
    // (add_creature_to_work_room() only clears it if the creature leaves
    // the room, e.g. on jailbreak or death).
    struct CreatureControl* cctrl = creature_control_get_from_thing(captive);
    if (cctrl->work_room_id == 0)
    {
        if (get_gameturn() >= args->intended_start_at_game_turn + 50)
        {
            FTEST_FAIL_TEST("Captive never got added to the prison's work room within the turn budget (active_state=%" PRId64 ", continue_state=%" PRId64 ")",
                (int64_t)captive->active_state, (int64_t)captive->continue_state);
            return FTRs_Go_To_Next_Action;
        }
        return FTRs_Repeat_Current_Action;
    }
    struct Room* prison_room = room_get(cctrl->work_room_id);
    if (room_is_invalid(prison_room) || !room_role_matches(prison_room->kind, RoRoF_Prison))
    {
        FTEST_FAIL_TEST("Captive's work_room_id (%" PRId64 ") does not refer to a prison room (kind=%" PRId64 ")",
            (int64_t)cctrl->work_room_id, (int64_t)(room_is_invalid(prison_room) ? -1 : (int64_t)prison_room->kind));
        return FTRs_Go_To_Next_Action;
    }

    if (creature_is_being_unconscious(captive))
    {
        FTEST_FAIL_TEST("Captive is still unconscious after being dropped in the prison -- expected make_creature_conscious() to have run");
        return FTRs_Go_To_Next_Action;
    }
    if (captive->owner != PLAYER_GOOD)
    {
        FTEST_FAIL_TEST("Captive's owner unexpectedly changed on capture (owner=%" PRId64 ")", (int64_t)captive->owner);
        return FTRs_Go_To_Next_Action;
    }

    // This is the ownership-differs behaviour itself:
    // room_initially_valid_as_type_for_thing() (creature_states.c) --
    // which creature_arrived_at_prison() had to pass for work_room_id to
    // get set at all above -- only accepts a room whose owner differs from
    // the creature's own owner when enemies_may_work_in_room(room->kind)
    // is true (CAPTIVITY's job config allows ENEMY_CREATURES). Asserting
    // it here directly, rather than only inferring it from work_room_id
    // being set, pins down *which* branch of that OR let the differently-
    // owned captive in.
    //
    // (jailbreak_possible() also keys off this same room-vs-captive owner
    // mismatch, but additionally requires the captive's own player to
    // hold map territory adjacent to the prison -- never true for
    // PLAYER_GOOD on this single-player level, so it isn't a reliable
    // signal here and is intentionally not asserted.)
    if (prison_room->owner == captive->owner)
    {
        FTEST_FAIL_TEST("Test setup bug: prison room owner (%" PRId64 ") matches captive owner (%" PRId64 "), doesn't exercise the ownership-differs path",
            (int64_t)prison_room->owner, (int64_t)captive->owner);
        return FTRs_Go_To_Next_Action;
    }
    if (!room_initially_valid_as_type_for_thing(prison_room, get_room_role_for_job(Job_CAPTIVITY), captive))
    {
        FTEST_FAIL_TEST("room_initially_valid_as_type_for_thing() unexpectedly false for a differently-owned captive (room owner=%" PRId64 ", captive owner=%" PRId64 ") -- enemies_may_work_in_room(PRISON) may have regressed",
            (int64_t)prison_room->owner, (int64_t)captive->owner);
        return FTRs_Go_To_Next_Action;
    }

    FTESTLOG("Captive was captured and added to the prison's work room at turn %" PRId64 ", still owned by the hero side (owner=%" PRId64 ") despite the room being owned by player %" PRId64,
        (int64_t)(get_gameturn()), (int64_t)captive->owner, (int64_t)prison_room->owner);
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
