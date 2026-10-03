// Dungeon specials and loose gold for an External seat. The view lists the special boxes the seat can see (kind, and
// whether its hand can reach them: its own land only), and its dead creatures once a resurrect box is among them.
// use_special: an increase-level box levels the seat's creatures; a resurrect box brings back the dead creature the
// order names (refused MISSING_CHOICE without one); a transfer box takes the creature the order names; a box on land
// not the seat's is CANNOT_REACH. Loose gold outside the treasure room is listed and carried into it with the hand.
#include "ftest_ai_seat_specials_gold.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <string.h>
#include <json.h>
#include <json-dom.h>

#include "../ftest.h"
#include "../ftest_util.h"

#include "api_seat_view.h"
#include "config_creature.h"
#include "config_keeperfx.h"
#include "creature_control.h"
#include "dungeon_data.h"
#include "external_seat.h"
#include "frontend.h"
#include "game_legacy.h"
#include "kfx_config_state.h"
#include "net_game.h"
#include "player_availability.h"
#include "player_data.h"
#include "room_data.h"
#include "slab_data.h"
#include "thing_corpses.h"
#include "thing_list.h"
#include "thing_objects.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

static int64_t s_failures = 0;
#define SOFT_FAIL(...) do { s_failures++; FTESTLOG("CHECK FAILED: " __VA_ARGS__); } while (0)
#define CHECK_TRUE(what, cond) do { if (!(cond)) SOFT_FAIL("%s", what); } while (0)

static PlayerNumber P = -1;
static NetUserId U = -1;
static MapSlabCoord hsx, hsy;
static ThingIndex s_level_box = 0, s_res_box = 0, s_trans_box = 0, s_far_box = 0, s_orc = 0, s_troll = 0, s_gold = 0;
static RoomIndex s_treasure = 0;
static int64_t s_orcs_before = 0, s_treasure_gold_before = 0;
static CrtrExpLevel s_orc_level_before = 0;

static struct ExtSeatVerb special(ThingIndex box)
{
    struct ExtSeatVerb v; memset(&v, 0, sizeof(v)); v.kind = ESV_UseSpecial; v.has_thing = true; v.thing_id = box; return v;
}
static const char* submit(struct ExtSeatVerb v) { return extseat_submit_verb_ex(U, P, &v, true, NULL); }
static struct Thing* box_at(ThingModel model, MapSlabCoord sx, MapSlabCoord sy)
{
    struct Coord3d pos; set_coords_to_slab_center(&pos, sx, sy);
    return create_object(&pos, model, kfx_config_state.neutral_player_num, -1);
}
static int64_t count_owned(const char* kind)
{
    const ThingModel m = (ThingModel)creature_model_id(kind);
    int64_t n = 0;
    int64_t idx = kfx_sim_state.thing_lists[TngList_Creatures].index;
    for (int64_t g = 0; (idx > 0) && (g < 4000); g++) { const struct Thing* t = thing_get(idx); idx = t->next_of_class; if (thing_is_creature(t) && t->owner == P && t->model == m) n++; }
    return n;
}
static TbBool view_lists(const char* list, ThingIndex id, TbBool* reachable)
{
    VALUE v; api_seat_build_view(&v, P);
    VALUE* own = value_dict_get(&v, "own");
    VALUE* arr = (strcmp(list, "gold") == 0) ? value_dict_get(value_dict_get(own, "loose_gold"), "piles") : value_dict_get(own, list);
    TbBool found = false;
    for (size_t i = 0; arr && i < value_array_size(arr); i++) {
        VALUE* e = value_array_get(arr, i);
        if (value_int32(value_dict_get(e, "id")) != id) continue;
        found = true;
        if (reachable) *reachable = value_bool(value_dict_get(e, "reachable"));
    }
    value_fini(&v);
    return found;
}
static TbBool dead_listed(const char* kind, int level)
{
    VALUE v; api_seat_build_view(&v, P);
    VALUE* dead = value_dict_get(value_dict_get(&v, "own"), "dead_creatures");
    TbBool found = false;
    for (size_t i = 0; dead && i < value_array_size(dead); i++) {
        VALUE* e = value_array_get(dead, i);
        if (strcmp(value_string(value_dict_get(e, "kind")), kind) == 0 && value_int32(value_dict_get(e, "level")) == level) found = true;
    }
    value_fini(&v);
    return found;
}

FTestActionResult sg01_setup(struct FTestActionArgs* const args);
FTestActionResult sg02_check_view_then_use(struct FTestActionArgs* const args);
FTestActionResult sg03_check_uses(struct FTestActionArgs* const args);

void ftest_ai_seat_specials_gold_pre_start() { fe_computer_players = 1; }

TbBool ftest_ai_seat_specials_gold_init()
{
    s_failures = 0;
    ftest_append_action(sg01_setup, 0, NULL);
    ftest_append_action(sg02_check_view_then_use, 2, NULL);
    ftest_append_action(sg03_check_uses, 2, NULL);
    return true;
}

FTestActionResult sg01_setup(struct FTestActionArgs* const args)
{
    for (PlayerNumber p = 0; p < PLAYERS_COUNT; p++)
        if (p != my_player_number && p != PLAYER_GOOD && p != PLAYER_NEUTRAL && player_exists(get_player(p)) && !thing_is_invalid(find_players_dungeon_heart(p))) { P = p; break; }
    if (P < 0 || (U = net_add_external_seat(P)) < 1) { FTEST_FAIL_TEST("no seat"); return FTRs_Go_To_Next_Action; }
    const struct Thing* heart = find_players_dungeon_heart(P);
    hsx = subtile_slab(heart->mappos.x.stl.num); hsy = subtile_slab(heart->mappos.y.stl.num);
    ftest_util_replace_slabs(hsx - 6, hsy + 3, hsx + 8, hsy + 6, SlbT_CLAIMED, P);
    ftest_util_replace_slabs(hsx - 6, hsy + 7, hsx + 8, hsy + 8, SlbT_PATH, kfx_config_state.neutral_player_num);
    set_room_available(P, RoK_TREASURE, 1, 1);
    ftest_util_replace_slabs(hsx + 6, hsy + 3, hsx + 8, hsy + 5, SlbT_TREASURE, P);
    ftest_util_reveal_map(P);
    const struct Room* treasure = slab_room_get(hsx + 7, hsy + 4);
    if (room_is_invalid(treasure)) { FTEST_FAIL_TEST("no treasure room"); return FTRs_Go_To_Next_Action; }
    s_treasure = treasure->index;
    s_treasure_gold_before = treasure->capacity_used_for_storage;
    // The seat's creatures, a death on record, and the boxes (one on unclaimed path the hand cannot reach).
    struct Thing* orc = ftest_util_create_creature(subtile_coord_center(slab_subtile_center(hsx - 4)), subtile_coord_center(slab_subtile_center(hsy + 4)), P, 1, (ThingModel)creature_model_id("ORC"));
    struct Thing* troll = ftest_util_create_creature(subtile_coord_center(slab_subtile_center(hsx - 2)), subtile_coord_center(slab_subtile_center(hsy + 4)), P, 1, (ThingModel)creature_model_id("TROLL"));
    s_orc = orc->index; s_troll = troll->index;
    add_item_to_dead_creature_list(get_players_dungeon(get_player(P)), (ThingModel)creature_model_id("ORC"), 2);
    s_level_box = box_at(ObjMdl_SpecboxIncreaseLevel, hsx, hsy + 4)->index;
    s_res_box = box_at(ObjMdl_SpecboxResurect, hsx + 1, hsy + 4)->index;
    s_trans_box = box_at(ObjMdl_SpecboxTransfer, hsx + 2, hsy + 4)->index;
    s_far_box = box_at(ObjMdl_SpecboxRevealMap, hsx, hsy + 7)->index;
    struct Coord3d gpos; set_coords_to_slab_center(&gpos, hsx + 3, hsy + 5);
    struct Thing* gold = drop_gold_pile(400, &gpos);
    s_gold = thing_is_invalid(gold) ? 0 : gold->index;
    return FTRs_Go_To_Next_Action;
}

FTestActionResult sg02_check_view_then_use(struct FTestActionArgs* const args)
{
    TbBool reach = false;
    CHECK_TRUE("the increase-level box is listed, reachable", view_lists("specials", s_level_box, &reach) && reach);
    CHECK_TRUE("the box on unclaimed path is listed, not reachable", view_lists("specials", s_far_box, &reach) && !reach);
    CHECK_TRUE("with a resurrect box about, the seat's dead are listed", dead_listed("ORC", 3));
    CHECK_TRUE("the loose gold pile is listed", s_gold && view_lists("gold", s_gold, &reach) && reach);
    const char* e = submit(special(s_far_box));
    CHECK_TRUE("a box the hand cannot reach is CANNOT_REACH", e && strcmp(e, "CANNOT_REACH") == 0);
    e = submit(special(s_res_box));
    CHECK_TRUE("resurrect without saying whom is MISSING_CHOICE", e && strcmp(e, "MISSING_CHOICE") == 0);
    struct ExtSeatVerb res = special(s_res_box); snprintf(res.name, sizeof(res.name), "ORC"); res.level = 2;
    e = submit(res);
    CHECK_TRUE("resurrecting a creature the seat never lost is NO_SUCH_DEAD", e && strcmp(e, "NO_SUCH_DEAD") == 0);
    s_orcs_before = count_owned("ORC");
    s_orc_level_before = creature_control_get_from_thing(thing_get(s_orc))->exp_level;
    res.level = 3;
    CHECK_TRUE("resurrect the level-3 orc", submit(res) == NULL);
    struct ExtSeatVerb tr = special(s_trans_box); tr.has_target_thing = true; tr.target_thing = s_troll;
    CHECK_TRUE("transfer the troll", submit(tr) == NULL);
    CHECK_TRUE("use the increase-level box", submit(special(s_level_box)) == NULL);
    struct ExtSeatVerb carry; memset(&carry, 0, sizeof(carry));
    carry.kind = ESV_PickUpAndDrop; carry.has_thing = true; carry.thing_id = s_gold; carry.has_room = true; carry.room_id = s_treasure;
    const char* ce = submit(carry);
    CHECK_TRUE("carry the gold into the treasure room", ce == NULL);
    if (ce) FTESTLOG("gold carry refused: %s", ce);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult sg03_check_uses(struct FTestActionArgs* const args)
{
    if (!extseat_idle(U) && ((args->intended_start_at_game_turn <= 0) || ((int64_t)get_gameturn() < args->intended_start_at_game_turn + 300)))
        return FTRs_Repeat_Current_Action;
    if ((args->intended_start_at_game_turn > 0) && ((int64_t)get_gameturn() < args->intended_start_at_game_turn + 20)) return FTRs_Repeat_Current_Action;
    CHECK_TRUE("a resurrected orc joined the seat", count_owned("ORC") == s_orcs_before + 1);
    CHECK_TRUE("and it left the dead list", !dead_listed("ORC", 3));
    const struct Thing* troll = thing_get(s_troll);
    CHECK_TRUE("the transferred troll is gone", thing_is_invalid(troll) || !thing_exists(troll) || !thing_is_creature(troll) || troll->index != s_troll || troll->owner != P);
    CHECK_TRUE("the increase-level box levelled the orc", creature_control_get_from_thing(thing_get(s_orc))->exp_level > s_orc_level_before);
    const struct Room* treasure = room_get(s_treasure);
    // Dropped gold falls as coins (drop_gold_coins) and joins the hoards when it lands: give it time.
    if (((int64_t)treasure->capacity_used_for_storage < s_treasure_gold_before + 400)
     && (args->intended_start_at_game_turn > 0) && ((int64_t)get_gameturn() < args->intended_start_at_game_turn + 200)) return FTRs_Repeat_Current_Action;
    FTESTLOG("treasure gold %" PRId64 " -> %" PRId64, s_treasure_gold_before, (int64_t)treasure->capacity_used_for_storage);
    CHECK_TRUE("the carried gold is in the treasure room", (int64_t)treasure->capacity_used_for_storage >= s_treasure_gold_before + 400);
    if (s_failures > 0) { FTEST_FAIL_TEST("%" PRId64 " specials/gold check(s) failed", s_failures); return FTRs_Go_To_Next_Action; }
    FTESTLOG("Test passed: specials are listed and used (with the resurrect and transfer choices), loose gold carried home");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
