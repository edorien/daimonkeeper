// kfx_sim "thing" cluster, hard-tail depth increment: thing_list.c was at
// 0% coverage (per out/coverage/coverage-html) despite thing_get/
// thing_is_invalid living in the neighboring thing_data.c, not here.
// Most of this file is generic map/list-traversal plumbing needing a real
// Ariadne/map fixture, but the model-matching predicates
// (creature_model_matches_model/creature_matches_model/thing_matches_model)
// and the Thing_Maximizer_Filter family are pure functions of a Thing and
// a struct CompoundTngFilterParam -- no traversal needed to call them
// directly. creature_stats_get (unlike creature_stats_get_from_thing) is a
// direct by-model-id lookup, not routed through SimPort, so
// real per-model creature config (model_flags) is testable here.
#include <catch2/catch_test_macros.hpp>

#include "thing_list.h"
#include "thing_data.h"
#include "player_data.h"
#include "kfx_config_state.h"
#include "kfx_sim_state.h"
#include "kfx_sim_test_fixtures.h"
#include "thing_stats.h"

#include <cstring>

namespace {
struct ResetState {
    ResetState() {
        std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
        std::memset(&kfx_config_state, 0, sizeof(kfx_config_state));
    }
};
}

TEST_CASE_METHOD(ResetState, "creature_model_matches_model with a concrete (non-wildcard) target is a plain equality check", "[kfx_sim][thing_list]") {
    CHECK(creature_model_matches_model(5, 0, 5));
    CHECK_FALSE(creature_model_matches_model(5, 0, 6));
}

TEST_CASE_METHOD(ResetState, "creature_model_matches_model CREATURE_ANY excludes only the player's spectator breed", "[kfx_sim][thing_list]") {
    kfx_config_state.conf.crtr_conf.spectator_breed = 7;

    CHECK(creature_model_matches_model(3, 0, CREATURE_ANY));
    CHECK_FALSE(creature_model_matches_model(7, 0, CREATURE_ANY));
}

TEST_CASE_METHOD(ResetState, "creature_model_matches_model CREATURE_NONE never matches", "[kfx_sim][thing_list]") {
    CHECK_FALSE(creature_model_matches_model(3, 0, CREATURE_NONE));
    CHECK_FALSE(creature_model_matches_model(0, 0, CREATURE_NONE));
}

TEST_CASE_METHOD(ResetState, "creature_model_matches_model CREATURE_DIGGER matches only models flagged CMF_IsSpecDigger", "[kfx_sim][thing_list]") {
    struct PlayerInfo *player = get_player(0);
    player->player_type = PT_Keeper; // not roaming -- creature_kind_is_for_dungeon_diggers_list's own early-out
    kfx_config_state.conf.crtr_conf.model[5].model_flags = CMF_IsSpecDigger;

    CHECK(creature_model_matches_model(5, 0, CREATURE_DIGGER));
    CHECK_FALSE(creature_model_matches_model(6, 0, CREATURE_DIGGER)); // no flag set
}

TEST_CASE_METHOD(ResetState, "creature_model_matches_model CREATURE_NOT_A_DIGGER excludes diggers and the spectator breed", "[kfx_sim][thing_list]") {
    struct PlayerInfo *player = get_player(0);
    player->player_type = PT_Keeper;
    kfx_config_state.conf.crtr_conf.spectator_breed = 7;
    kfx_config_state.conf.crtr_conf.model[5].model_flags = CMF_IsSpecDigger;

    CHECK_FALSE(creature_model_matches_model(5, 0, CREATURE_NOT_A_DIGGER)); // is a digger
    CHECK_FALSE(creature_model_matches_model(7, 0, CREATURE_NOT_A_DIGGER)); // is the spectator breed
    CHECK(creature_model_matches_model(3, 0, CREATURE_NOT_A_DIGGER)); // neither
}

TEST_CASE_METHOD(ResetState, "creature_matches_model requires class TCls_Creature before delegating to the model check", "[kfx_sim][thing_list]") {
    struct Thing *thing = thing_get(1);
    thing->class_id = TCls_Object;
    thing->model = 5;
    CHECK_FALSE(creature_matches_model(thing, 5)); // wrong class, regardless of model match

    thing->class_id = TCls_Creature;
    thing->owner = 0;
    CHECK(creature_matches_model(thing, 5));
    CHECK_FALSE(creature_matches_model(thing, 6));
}

TEST_CASE_METHOD(ResetState, "thing_matches_model routes creatures through creature_matches_model, everything else through a plain model check", "[kfx_sim][thing_list]") {
    struct Thing *creatng = thing_get(1);
    creatng->class_id = TCls_Creature;
    creatng->model = 5;
    creatng->owner = 0;
    CHECK(thing_matches_model(creatng, 5));
    CHECK_FALSE(thing_matches_model(creatng, 6));

    struct Thing *objtng = thing_get(2);
    objtng->class_id = TCls_Object;
    objtng->model = 9;
    CHECK(thing_matches_model(objtng, 9));
    CHECK(thing_matches_model(objtng, -1)); // -1 is a wildcard for non-creature things
    CHECK_FALSE(thing_matches_model(objtng, 10));
}

TEST_CASE_METHOD(ResetState, "near_map_block_thing_filter_is_owned_by matches class and (for creatures) ownership, scored by proximity", "[kfx_sim][thing_list]") {
    struct Thing *creatng = thing_get(1);
    creatng->class_id = TCls_Creature;
    creatng->owner = 3;
    creatng->mappos.x.val = 100;
    creatng->mappos.y.val = 100;

    struct CompoundTngFilterParam param{};
    param.class_id = TCls_Creature;
    param.plyr_idx = 3;
    param.primary_number = 100;
    param.secondary_number = 100;

    CHECK(near_map_block_thing_filter_is_owned_by(creatng, &param, 0) == INT32_MAX); // exact position match

    param.plyr_idx = 4; // wrong owner
    CHECK(near_map_block_thing_filter_is_owned_by(creatng, &param, 0) == -1);

    param.class_id = TCls_Object; // wrong class
    param.plyr_idx = 3;
    CHECK(near_map_block_thing_filter_is_owned_by(creatng, &param, 0) == -1);
}

TEST_CASE_METHOD(ResetState, "near_map_block_thing_filter_is_owned_by accepts any owner when plyr_idx is -1", "[kfx_sim][thing_list]") {
    struct Thing *objtng = thing_get(1);
    objtng->class_id = TCls_Object;
    objtng->owner = 5;

    struct CompoundTngFilterParam param{};
    param.class_id = TCls_Object;
    param.plyr_idx = -1;

    CHECK(near_map_block_thing_filter_is_owned_by(objtng, &param, 0) != -1);
}

TEST_CASE_METHOD(ResetState, "anywhere_thing_filter_is_of_class_and_model_and_owned_by requires class, model, and owner to all match", "[kfx_sim][thing_list]") {
    struct Thing *objtng = thing_get(1);
    objtng->class_id = TCls_Object;
    objtng->model = 9;
    objtng->owner = 2;

    struct CompoundTngFilterParam param{};
    param.class_id = TCls_Object;
    param.model_id = 9;
    param.plyr_idx = 2;

    CHECK(anywhere_thing_filter_is_of_class_and_model_and_owned_by(objtng, &param, 0) == INT32_MAX);

    param.model_id = 10; // wrong model
    CHECK(anywhere_thing_filter_is_of_class_and_model_and_owned_by(objtng, &param, 0) == -1);
}

// thing_is_shootable's creature branch is a genuinely multi-way predicate:
// two "always allow, unless the flag says otherwise" guards (Armour spell,
// PreventDamage), then a three-way owned/enemy/allied split gated by three
// independent HitTargetFlags bits. Uses kfx_sim_test_fixtures.h's
// ResetSimAndConfig/make_creature/make_player_active rather than this
// file's own minimal ResetState, since players_are_enemies() needs both
// players to real-exist (make_player_active), not just be zeroed slots.
TEST_CASE_METHOD(kfx_test::ResetSimAndConfig, "thing_is_shootable's creature branch gates on the owned/enemy/allied HitTargetFlags bits", "[kfx_sim][thing_list]") {
    kfx_test::make_player_active(0);
    kfx_test::make_player_active(1);
    struct Thing *creatng = kfx_test::make_creature(1, 1, 1); // owned by player 1

    CHECK_FALSE(thing_is_shootable(creatng, 1, 0)); // shot_owner == owner, but HitTF_OwnedCreatures not set
    CHECK(thing_is_shootable(creatng, 1, HitTF_OwnedCreatures));

    CHECK_FALSE(thing_is_shootable(creatng, 0, 0)); // player 0 is an enemy (not allied), flag not set
    CHECK(thing_is_shootable(creatng, 0, HitTF_EnemyCreatures));
    CHECK_FALSE(thing_is_shootable(creatng, 0, HitTF_AlliedCreatures)); // wrong bit for an enemy

    CHECK(thing_is_shootable(creatng, -1, HitTF_EnemyCreatures)); // negative shot_owner is always treated as enemy

    get_player(0)->allied_players |= to_flag(1);
    get_player(1)->allied_players |= to_flag(0);
    CHECK_FALSE(thing_is_shootable(creatng, 0, HitTF_EnemyCreatures)); // now allied, not enemy
    CHECK(thing_is_shootable(creatng, 0, HitTF_AlliedCreatures));
}

TEST_CASE_METHOD(kfx_test::ResetSimAndConfig, "thing_is_shootable's creature branch can be blocked by the Armour spell or PreventDamage, unless the matching flag allows it", "[kfx_sim][thing_list]") {
    kfx_test::make_player_active(0);
    kfx_test::make_player_active(1);
    struct Thing *creatng = kfx_test::make_creature(1, 1, 1);
    struct CreatureControl *cctrl = creature_control_get(1);

    cctrl->spell_flags |= CSAfF_Armour;
    CHECK_FALSE(thing_is_shootable(creatng, 1, HitTF_OwnedCreatures)); // armour blocks it by default
    CHECK(thing_is_shootable(creatng, 1, HitTF_OwnedCreatures | HitTF_ArmourAffctdCreatrs));

    cctrl->spell_flags &= ~CSAfF_Armour;
    cctrl->creature_control_flags |= CCFlg_PreventDamage;
    CHECK_FALSE(thing_is_shootable(creatng, 1, HitTF_OwnedCreatures));
    CHECK(thing_is_shootable(creatng, 1, HitTF_OwnedCreatures | HitTF_PreventDmgCreatrs));
}
