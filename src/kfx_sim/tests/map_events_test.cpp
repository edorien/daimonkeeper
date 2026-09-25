// kfx_sim coverage, per docs/refactor/testing/comprehensive/
// stage-08b-kfx-sim-clusters.md's "still open" list: map_events.c, a
// previously wholly-untouched file. The accessor/search/lifecycle cluster
// (event_is_invalid/_exists, the get_event_of_*_for_player search family,
// event_allocate_free_event_structure, event_delete_event_structure,
// event_update_last_use, clear_events, get_thing_index_event_is_attached_to)
// needs no sim_feedback fixture at all -- either it doesn't call sim_feedback,
// or (clear_events) only calls members the default no-op table already
// handles safely.
//
// event_create_event and friends are different: event_initialise_event
// unconditionally dereferences sim_feedback->get_event_button_info(evkind),
// whose *default* implementation returns NULL (noop_get_event_button_info,
// sim_feedback.c) -- calling it without a fake would crash, not just
// return a wrong value. EventFeedbackFixture below fakes it to return a
// button-less EventTypeInfo (bttn_sprite==0), which also makes
// event_add_to_event_buttons_list_or_replace_button's more involved
// button-list logic take its early "no button" return -- confirmed by
// reading both bodies, not assumed.
#include <catch2/catch_test_macros.hpp>

#include "map_events.h"
#include "thing_data.h"
#include "dungeon_data.h"
#include "player_data.h"
#include "sim_feedback.h"
#include "kfx_config_state.h"
#include "kfx_sim_state.h"

#include <cstring>

namespace {
struct ResetState {
    ResetState() {
        std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
        std::memset(&kfx_config_state, 0, sizeof(kfx_config_state));
        kfx_config_state.neutral_player_num = PLAYER_NEUTRAL;
    }
};

const struct EventTypeInfo buttonless_event_info{}; // bttn_sprite==0 -- "no button" branch

struct EventFeedbackFixture : ResetState {
    struct SimFeedbackCallbacks callbacks;

    EventFeedbackFixture() : callbacks(*sim_feedback) {
        callbacks.get_event_button_info = fake_get_event_button_info;
        set_sim_feedback_callbacks(&callbacks);
    }
    ~EventFeedbackFixture() { set_sim_feedback_callbacks(nullptr); }

    static const struct EventTypeInfo *fake_get_event_button_info(EventKind evkind) {
        (void)evkind;
        return &buttonless_event_info;
    }
};
}

TEST_CASE_METHOD(ResetState, "event_is_invalid checks pointer range against the reserved slot 0 and EVENTS_COUNT", "[kfx_sim][map_events]") {
    CHECK(event_is_invalid(nullptr));
    CHECK(event_is_invalid(&kfx_sim_state.event[0]));
    CHECK_FALSE(event_is_invalid(&kfx_sim_state.event[1]));
    CHECK_FALSE(event_is_invalid(&kfx_sim_state.event[EVENTS_COUNT - 1]));
}

TEST_CASE_METHOD(ResetState, "event_exists is false for an invalid pointer, else reads EvF_Exists", "[kfx_sim][map_events]") {
    CHECK_FALSE(event_exists(&kfx_sim_state.event[0]));
    struct Event *event = &kfx_sim_state.event[1];
    CHECK_FALSE(event_exists(event));
    event->flags |= EvF_Exists;
    CHECK(event_exists(event));
}

TEST_CASE_METHOD(ResetState, "get_event_of_type_for_player finds an existing event matching owner and kind", "[kfx_sim][map_events]") {
    struct Event *event = &kfx_sim_state.event[3];
    event->flags = EvF_Exists;
    event->owner = 2;
    event->kind = EvKind_HeartAttacked;

    CHECK(get_event_of_type_for_player(EvKind_HeartAttacked, 2) == event);
    CHECK(get_event_of_type_for_player(EvKind_HeartAttacked, 3) == INVALID_EVENT); // wrong owner
    CHECK(get_event_of_type_for_player(EvKind_Objective, 2) == INVALID_EVENT); // wrong kind
}

TEST_CASE_METHOD(ResetState, "get_event_of_target_and_type_for_player additionally matches on target", "[kfx_sim][map_events]") {
    struct Event *event = &kfx_sim_state.event[3];
    event->flags = EvF_Exists;
    event->owner = 2;
    event->kind = EvKind_NewCreature;
    event->target = 42;

    CHECK(get_event_of_target_and_type_for_player(42, EvKind_NewCreature, 2) == event);
    CHECK(get_event_of_target_and_type_for_player(99, EvKind_NewCreature, 2) == INVALID_EVENT);
}

TEST_CASE_METHOD(ResetState, "get_event_nearby_of_type_for_player matches owner/kind within max_dist", "[kfx_sim][map_events]") {
    struct Event *event = &kfx_sim_state.event[3];
    event->flags = EvF_Exists;
    event->owner = 2;
    event->kind = EvKind_HeartAttacked;
    event->mappos_x = 1000;
    event->mappos_y = 1000;

    CHECK(get_event_nearby_of_type_for_player(1005, 1000, 50, EvKind_HeartAttacked, 2) == event);
    CHECK(get_event_nearby_of_type_for_player(5000, 5000, 50, EvKind_HeartAttacked, 2) == INVALID_EVENT); // too far
}

TEST_CASE_METHOD(ResetState, "event_allocate_free_event_structure claims the first non-existing slot from index 1 onward", "[kfx_sim][map_events]") {
    kfx_sim_state.event[1].flags = EvF_Exists;

    struct Event *event = event_allocate_free_event_structure();

    CHECK(event == &kfx_sim_state.event[2]);
    CHECK((event->flags & EvF_Exists) != 0);
    CHECK(event->index == 2);
}

TEST_CASE_METHOD(ResetState, "event_allocate_free_event_structure returns INVALID_EVENT once every slot is taken", "[kfx_sim][map_events]") {
    for (int64_t i = 1; i < EVENTS_COUNT; i++)
        kfx_sim_state.event[i].flags = EvF_Exists;

    CHECK(event_allocate_free_event_structure() == INVALID_EVENT);
}

TEST_CASE_METHOD(ResetState, "event_delete_event_structure zeroes the slot", "[kfx_sim][map_events]") {
    kfx_sim_state.event[2].flags = EvF_Exists;
    kfx_sim_state.event[2].kind = EvKind_HeartAttacked;

    event_delete_event_structure(2);

    CHECK(kfx_sim_state.event[2].flags == 0);
    CHECK(kfx_sim_state.event[2].kind == 0);
}

TEST_CASE_METHOD(ResetState, "event_update_last_use writes the current gameturn into the dungeon's per-kind timestamp", "[kfx_sim][map_events]") {
    struct Event event{};
    event.owner = 0;
    event.kind = EvKind_HeartAttacked;

    event_update_last_use(&event);

    CHECK(get_dungeon(0)->event_last_run_turn[EvKind_HeartAttacked] == 0); // default GetGameTurnFunc returns 0
}

TEST_CASE_METHOD(ResetState, "event_update_last_use rejects an out-of-range event kind", "[kfx_sim][map_events]") {
    struct Event event{};
    event.owner = 0;
    event.kind = EVENT_KIND_COUNT; // out of range

    event_update_last_use(&event); // must not crash; nothing to assert but the lack of one
    CHECK(true);
}

TEST_CASE_METHOD(ResetState, "clear_events zeroes every event slot and the scroll window/text buffer", "[kfx_sim][map_events]") {
    kfx_sim_state.event[5].flags = EvF_Exists;
    kfx_sim_state.evntbox_text_buffer[0] = 'x';

    clear_events();

    CHECK(kfx_sim_state.event[5].flags == 0);
    CHECK(kfx_sim_state.evntbox_text_buffer[0] == 0);
}

TEST_CASE_METHOD(ResetState, "get_thing_index_event_is_attached_to returns target only for thing-attached event kinds", "[kfx_sim][map_events]") {
    struct Event event{};
    event.target = 42;

    event.kind = EvKind_NewCreature;
    CHECK(get_thing_index_event_is_attached_to(&event) == 42);

    event.kind = EvKind_Information; // not in the thing-attached kind list
    CHECK(get_thing_index_event_is_attached_to(&event) == 0);
}

TEST_CASE_METHOD(EventFeedbackFixture, "event_create_event refuses the neutral player and an out-of-range event kind", "[kfx_sim][map_events]") {
    CHECK(event_create_event(0, 0, EvKind_HeartAttacked, PLAYER_NEUTRAL, 0) == INVALID_EVENT);
    CHECK(event_create_event(0, 0, EVENT_KIND_COUNT, 0, 0) == INVALID_EVENT);
}

TEST_CASE_METHOD(EventFeedbackFixture, "event_create_event allocates and initializes a fresh event", "[kfx_sim][map_events]") {
    struct Event *event = event_create_event(100, 200, EvKind_HeartAttacked, 0, 42);

    CHECK(event != INVALID_EVENT);
    CHECK(event->mappos_x == 100);
    CHECK(event->mappos_y == 200);
    CHECK(event->kind == EvKind_HeartAttacked);
    CHECK(event->owner == 0);
    CHECK(event->target == 42);
    CHECK(event->icon_idx == -1);
    CHECK((event->flags & EvF_Exists) != 0);
}

TEST_CASE_METHOD(EventFeedbackFixture, "event_create_event is blocked while the per-kind cooldown hasn't elapsed", "[kfx_sim][map_events]") {
    struct SimFeedbackCallbacks cooldown_callbacks = *sim_feedback;
    static const struct EventTypeInfo cooldown_info = []{
        struct EventTypeInfo info{};
        info.turns_between_events = 100;
        return info;
    }();
    cooldown_callbacks.get_event_button_info = [](EventKind) -> const struct EventTypeInfo* { return &cooldown_info; };
    set_sim_feedback_callbacks(&cooldown_callbacks);

    get_dungeon(0)->event_last_run_turn[EvKind_HeartAttacked] = 5; // last fired at turn 5, cooldown 100 -- still active at turn 0

    CHECK(event_create_event(0, 0, EvKind_HeartAttacked, 0, 0) == INVALID_EVENT);

    set_sim_feedback_callbacks(nullptr);
}

TEST_CASE_METHOD(EventFeedbackFixture, "event_create_event_or_update_nearby_existing_event creates once, then updates the nearby event instead of duplicating it", "[kfx_sim][map_events]") {
    // EventIndex is uint8_t (globals.h) -- unsigned. The function's own doc
    // comment promises "negative index of updated event", but
    // "-(EventIndex)event->index" computes -1 in int, then narrows to the
    // EventIndex return type: (uint8_t)-1 wraps to 255, not a true negative
    // value. Confirmed by an empirical run (a CAPTURE probe showed
    // first==1, second==255) before asserting it, not assumed from the
    // doc comment's promise -- a real, if surprising, effect of the return
    // type being narrower than the sentinel scheme intends.
    EventIndex first = event_create_event_or_update_nearby_existing_event(100, 100, EvKind_HeartAttacked, 0, 1);
    CHECK(first > 0); // newly created -- positive index

    EventIndex second = event_create_event_or_update_nearby_existing_event(105, 105, EvKind_HeartAttacked, 0, 2);
    CHECK(second == 255); // "updated" sentinel, wrapped from the intended -1

    struct Event *event = &kfx_sim_state.event[first];
    CHECK(event->mappos_x == 105);
    CHECK(event->target == 2);
}
