// kfx_sim coverage: a first pass over thing_creature.c (8000+ lines,
// almost entirely navigation/possession/combat simulation) picking out
// the handful of functions tractable without a navigation fixture: the
// thing_is_creature*/class-check family, the casted-spell-slot array
// bookkeeping (get_spell_slot/fill_spell_slot/free_spell_slot -- a plain
// fixed-size array scan+write, the same shape as this session's other
// array-bookkeeping passes), creature_is_invisible/_can_see_invisible,
// and creature_has_lair_room.
//
// thing_is_creature_digger/is_creature_droppable_on_path/
// thing_is_creature_special_digger read the model's flags through
// get_creature_model_flags(), which reads thing->model directly since
// refactor pass 2's S05 moved it into kfx_sim (it used to go through a
// ConfigReloadCallbacks accessor that defaulted to model 0 here, which
// left all three untestable).
//
// fill_spell_slot had only a same-file forward declaration -- added to
// thing_creature.h alongside get_spell_slot/free_spell_slot.
//
// Deliberately deferred: essentially everything else in this file --
// possession/control, spell-effect application (set_thing_spell_flags_f
// creates objects and computes speeds), combat setup, movement/altitude
// updates -- all navigation, thing-creation, or config-computation heavy.
#include <catch2/catch_test_macros.hpp>

#include "thing_creature.h"
#include "creature_control.h"
#include "config_magic.h"
#include "config_creature.h"
#include "kfx_sim_test_fixtures.h"
#include "thing_stats.h"

using namespace kfx_test;

TEST_CASE_METHOD(ResetSimAndConfig, "thing_is_creature/thing_is_dead_creature are direct class_id checks", "[kfx_sim][thing_creature]") {
    struct Thing *creature = make_creature(1, 1, 0);
    CHECK(thing_is_creature(creature));
    CHECK_FALSE(thing_is_dead_creature(creature));

    creature->class_id = TCls_DeadCreature;
    CHECK_FALSE(thing_is_creature(creature));
    CHECK(thing_is_dead_creature(creature));
}

TEST_CASE_METHOD(ResetSimAndConfig, "thing_is_creature_digger/is_creature_droppable_on_path/thing_is_creature_special_digger check the creature model's flags", "[kfx_sim][thing_creature]") {
    struct Thing *creature = make_creature(1, 1, 0);
    kfx_config_state.conf.crtr_conf.model_count = 4;
    kfx_config_state.conf.crtr_conf.model[1].model_flags = CMF_IsSpecDigger;
    kfx_config_state.conf.crtr_conf.model[2].model_flags = CMF_IsDiggingCreature;
    kfx_config_state.conf.crtr_conf.model[3].model_flags = CMF_DropOnPath;

    creature->model = 1;
    CHECK(thing_is_creature_digger(creature));
    CHECK(is_creature_droppable_on_path(creature));
    CHECK(thing_is_creature_special_digger(creature));

    creature->model = 2;
    CHECK(thing_is_creature_digger(creature));
    CHECK_FALSE(is_creature_droppable_on_path(creature));
    CHECK_FALSE(thing_is_creature_special_digger(creature));

    creature->model = 3;
    CHECK_FALSE(thing_is_creature_digger(creature));
    CHECK(is_creature_droppable_on_path(creature));

    creature->class_id = TCls_Object; // not a creature
    CHECK_FALSE(is_creature_droppable_on_path(creature));
}

TEST_CASE_METHOD(ResetSimAndConfig, "thing_is_creature_spectator compares thing->model directly against the configured spectator breed", "[kfx_sim][thing_creature]") {
    struct Thing *creature = make_creature(1, 1, 0);
    creature->model = 9;
    kfx_config_state.conf.crtr_conf.spectator_breed = 9;
    CHECK(thing_is_creature_spectator(creature));

    creature->model = 3;
    CHECK_FALSE(thing_is_creature_spectator(creature));

    struct Thing *not_a_creature = thing_get(2);
    not_a_creature->index = 2;
    not_a_creature->alloc_flags = TAlF_Exists;
    not_a_creature->class_id = TCls_Object;
    CHECK_FALSE(thing_is_creature_spectator(not_a_creature));
}

TEST_CASE_METHOD(ResetSimAndConfig, "get_spell_slot/fill_spell_slot/free_spell_slot round-trip a creature's casted-spell array", "[kfx_sim][thing_creature]") {
    struct Thing *creature = make_creature(1, 1, 0);
    CHECK(get_spell_slot(creature, 12) == -1); // not cast yet

    CHECK(fill_spell_slot(creature, 12, 100, 3, 0, 2));
    CHECK(get_spell_slot(creature, 12) == 2);

    struct CreatureControl *cctrl = creature_control_get(1);
    CHECK(cctrl->casted_spells[2].duration == 100);
    CHECK(cctrl->casted_spells[2].caster_level == 3);

    CHECK(free_spell_slot(creature, 2));
    CHECK(get_spell_slot(creature, 12) == -1);
    CHECK(cctrl->casted_spells[2].spkind == 0);

    CHECK_FALSE(fill_spell_slot(creature, 12, 1, 1, 0, CREATURE_MAX_SPELLS_CASTED_AT)); // out of range
    CHECK_FALSE(free_spell_slot(creature, -1));
}

TEST_CASE_METHOD(ResetSimAndConfig, "creature_is_invisible requires the Invisibility spell effect and force_visible not overriding it", "[kfx_sim][thing_creature]") {
    struct Thing *creature = make_creature(1, 1, 0);
    struct CreatureControl *cctrl = creature_control_get(1);
    CHECK_FALSE(creature_is_invisible(creature));

    cctrl->spell_flags |= CSAfF_Invisibility;
    CHECK(creature_is_invisible(creature));

    cctrl->force_visible = 1;
    CHECK_FALSE(creature_is_invisible(creature)); // force-visible overrides the spell
}

TEST_CASE_METHOD(ResetSimAndConfig, "creature_can_see_invisible is true from either the Sight spell or the creature model's own can_see_invisible config", "[kfx_sim][thing_creature]") {
    struct Thing *creature = make_creature(1, 1, 0);
    CHECK_FALSE(creature_can_see_invisible(creature));

    creature_control_get(1)->spell_flags |= CSAfF_Sight;
    CHECK(creature_can_see_invisible(creature));

    creature_control_get(1)->spell_flags = 0;
    kfx_config_state.conf.crtr_conf.model[0].can_see_invisible = 1; // creature_stats_get_from_thing() always resolves to model[0]
    CHECK(creature_can_see_invisible(creature));
}

TEST_CASE_METHOD(ResetSimAndConfig, "creature_has_lair_room requires a real lair and a room matching the Job_TAKE_SLEEP role", "[kfx_sim][thing_creature]") {
    struct Thing *creature = make_creature(1, 1, 0);
    CHECK_FALSE(creature_has_lair_room(creature)); // no lair at all

    struct CreatureControl *cctrl = creature_control_get(1);
    cctrl->lairtng_idx = 1;
    cctrl->lair_room_id = 1;
    struct Room *lair = make_room_at_slab(1, 0, 0, RoK_LAIR, 0);
    configure_job(Job_TAKE_SLEEP, RoRoF_LairStorage, 0);
    CHECK_FALSE(creature_has_lair_room(creature)); // room exists, but its role doesn't match Job_TAKE_SLEEP's role yet

    configure_room_role(RoK_LAIR, RoRoF_LairStorage);
    CHECK(creature_has_lair_room(creature));
    (void)lair;
}
