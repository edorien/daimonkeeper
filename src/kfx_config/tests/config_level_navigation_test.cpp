// kfx_config: config.c's campaign level-navigation functions, per
// docs/refactor/testing/comprehensive/stage-08-comprehensive-library-
// passes.md's kfx_config row -- the sequencing half of the level
// bookkeeping tested in config_levels_test.cpp's membership predicates.
// Pattern A on the module-level `campaign` global throughout, except
// next_singleplayer_level, which also reaches through
// game_get_intralvl_next_level()/clear_intralvl_next_level()
// -- kfx_config's own first pattern-B test (it owns the GamePort
// struct and calls through it here, the same "callback-struct seams are
// a real, load-bearing part of this codebase" case stage-08 §1 calls
// out).
#include <catch2/catch_test_macros.hpp>

#include "config.h"
#include "config_campaigns.h"
#include "ports/game_port.h"

#include <cstring>

namespace {
struct ResetCampaign {
    ResetCampaign() { std::memset(&campaign, 0, sizeof(campaign)); }
};

int64_t g_fake_intralvl_next_level = 0;
int64_t g_fake_intralvl_cleared = 0;
int64_t fake_get_intralvl_next_level(void) { return g_fake_intralvl_next_level; }
void fake_clear_intralvl_next_level(void) { g_fake_intralvl_cleared++; }

// next_singleplayer_level() calls get_intralvl_next_level() twice: once
// in the outer "> 0" guard, and again just after to capture next_level.
// A provider that returns the same value both times can therefore never
// reach the inner "next_level < 0" check -- the outer guard already
// requires a positive value first. This fake changes its answer between
// calls so that check is actually reachable, the only way it can be.
int64_t g_fake_intralvl_first_call_value = 0;
int64_t g_fake_intralvl_second_call_value = 0;
int64_t g_fake_intralvl_call_count = 0;
int64_t fake_get_intralvl_next_level_changing(void) {
    g_fake_intralvl_call_count++;
    return (g_fake_intralvl_call_count == 1) ? g_fake_intralvl_first_call_value : g_fake_intralvl_second_call_value;
}

struct GameCallbacksFixture : ResetCampaign {
    struct GamePort callbacks = game_port_defaults;
    GameCallbacksFixture() {
        g_fake_intralvl_next_level = 0;
        g_fake_intralvl_cleared = 0;
        callbacks.get_intralvl_next_level = fake_get_intralvl_next_level;
        callbacks.clear_intralvl_next_level = fake_clear_intralvl_next_level;
        set_game_port(&callbacks);
    }
    ~GameCallbacksFixture() { set_game_port(nullptr); } // restores the default no-op table
};
}

TEST_CASE_METHOD(ResetCampaign, "first_singleplayer_level reads the first slot, falling back to NOTSTARTED when empty", "[kfx_config][config]") {
    campaign.single_levels[0] = 5;
    CHECK(first_singleplayer_level() == 5);

    campaign.single_levels[0] = 0;
    CHECK(first_singleplayer_level() == SINGLEPLAYER_NOTSTARTED);
}

TEST_CASE_METHOD(ResetCampaign, "last_singleplayer_level reads single_levels[single_levels_count - 1]", "[kfx_config][config]") {
    campaign.single_levels[2] = 9;
    campaign.single_levels_count = 3;
    CHECK(last_singleplayer_level() == 9);
}

TEST_CASE_METHOD(ResetCampaign, "last_singleplayer_level falls back to NOTSTARTED for an out-of-range count", "[kfx_config][config]") {
    campaign.single_levels_count = 0;
    CHECK(last_singleplayer_level() == SINGLEPLAYER_NOTSTARTED);

    campaign.single_levels_count = CAMPAIGN_LEVELS_COUNT + 1;
    CHECK(last_singleplayer_level() == SINGLEPLAYER_NOTSTARTED);
}

TEST_CASE_METHOD(ResetCampaign, "first_multiplayer_level reads the first slot, falling back to NOTSTARTED when empty", "[kfx_config][config]") {
    campaign.multi_levels[0] = 7;
    CHECK(first_multiplayer_level() == 7);
}

TEST_CASE_METHOD(ResetCampaign, "first_extra_level skips zero slots up to extra_levels_index", "[kfx_config][config]") {
    campaign.extra_levels[0] = 0;
    campaign.extra_levels[1] = 0;
    campaign.extra_levels[2] = 5;
    campaign.extra_levels_index = 3;
    CHECK(first_extra_level() == 5);
}

TEST_CASE_METHOD(ResetCampaign, "first_extra_level is NOTSTARTED when extra_levels_index is zero, regardless of array contents", "[kfx_config][config]") {
    campaign.extra_levels[0] = 5; // present, but out of the (empty) scan range
    campaign.extra_levels_index = 0;
    CHECK(first_extra_level() == SINGLEPLAYER_NOTSTARTED);
}

TEST_CASE_METHOD(ResetCampaign, "get_extra_level converts a 1-based kind to a 0-based array slot", "[kfx_config][config]") {
    campaign.extra_levels[2] = 7;
    CHECK(get_extra_level(3) == 7); // kind 3 -> index 2
}

TEST_CASE_METHOD(ResetCampaign, "get_extra_level returns NOTSTARTED for an unset in-range slot", "[kfx_config][config]") {
    CHECK(get_extra_level(3) == SINGLEPLAYER_NOTSTARTED);
}

TEST_CASE_METHOD(ResetCampaign, "get_extra_level returns LEVELNUMBER_ERROR for an out-of-range kind", "[kfx_config][config]") {
    CHECK(get_extra_level(0) == LEVELNUMBER_ERROR);              // kind 0 -> index -1
    CHECK(get_extra_level(EXTRA_LEVELS_COUNT + 1) == LEVELNUMBER_ERROR);
}

TEST_CASE_METHOD(ResetCampaign, "next_multiplayer_level returns the next slot, or FINISHED once the list runs out", "[kfx_config][config]") {
    campaign.multi_levels[0] = 10;
    campaign.multi_levels[1] = 20;
    campaign.multi_levels[2] = 30;
    CHECK(next_multiplayer_level(10) == 20);
    CHECK(next_multiplayer_level(30) == SINGLEPLAYER_FINISHED); // next slot is 0
    CHECK(next_multiplayer_level(999) == LEVELNUMBER_ERROR);    // not found at all
}

TEST_CASE_METHOD(ResetCampaign, "next_multiplayer_level handles the FINISHED and NOTSTARTED sentinels", "[kfx_config][config]") {
    campaign.multi_levels[0] = 10;
    CHECK(next_multiplayer_level(SINGLEPLAYER_FINISHED) == SINGLEPLAYER_FINISHED);
    CHECK(next_multiplayer_level(SINGLEPLAYER_NOTSTARTED) == 10); // delegates to first_multiplayer_level
}

TEST_CASE_METHOD(ResetCampaign, "prev_singleplayer_level returns the previous slot, or NOTSTARTED at the start", "[kfx_config][config]") {
    campaign.single_levels[0] = 10;
    campaign.single_levels[1] = 20;
    CHECK(prev_singleplayer_level(20) == 10);
    CHECK(prev_singleplayer_level(10) == SINGLEPLAYER_NOTSTARTED); // already the first slot
    CHECK(prev_singleplayer_level(999) == LEVELNUMBER_ERROR);
}

TEST_CASE_METHOD(ResetCampaign, "prev_singleplayer_level(FINISHED) delegates to last_singleplayer_level", "[kfx_config][config]") {
    campaign.single_levels[2] = 30;
    campaign.single_levels_count = 3;
    CHECK(prev_singleplayer_level(SINGLEPLAYER_FINISHED) == 30);
}

TEST_CASE_METHOD(ResetCampaign, "next_extra_level skips zero slots between the match and the next real level", "[kfx_config][config]") {
    campaign.extra_levels[0] = 5;
    campaign.extra_levels[1] = 0;
    campaign.extra_levels[2] = 7;
    CHECK(next_extra_level(5) == 7); // skips the zero slot at index 1
}

TEST_CASE_METHOD(ResetCampaign, "next_extra_level is FINISHED once no further non-zero slot exists, and LEVELNUMBER_ERROR when unmatched", "[kfx_config][config]") {
    campaign.extra_levels[2] = 7;
    CHECK(next_extra_level(7) == SINGLEPLAYER_FINISHED); // nothing after it
    CHECK(next_extra_level(999) == LEVELNUMBER_ERROR);
}

TEST_CASE_METHOD(ResetCampaign, "next_singleplayer_level walks single_levels under the default (no-op) intralevel-jump provider", "[kfx_config][config]") {
    campaign.single_levels[0] = 10;
    campaign.single_levels[1] = 20;
    campaign.single_levels[2] = 30;
    CHECK(next_singleplayer_level(10, false) == 20);
    CHECK(next_singleplayer_level(30, false) == SINGLEPLAYER_FINISHED); // next slot is 0
    CHECK(next_singleplayer_level(999, false) == LEVELNUMBER_ERROR);
    CHECK(next_singleplayer_level(SINGLEPLAYER_FINISHED, false) == SINGLEPLAYER_FINISHED);
    CHECK(next_singleplayer_level(SINGLEPLAYER_NOTSTARTED, false) == 10);
}

TEST_CASE_METHOD(GameCallbacksFixture, "next_singleplayer_level jumps to the intralevel-provided level when the provider reports one and ignore is false", "[kfx_config][config]") {
    campaign.single_levels[0] = 10;
    campaign.single_levels[1] = 20;
    g_fake_intralvl_next_level = 20;

    CHECK(next_singleplayer_level(10, false) == 20); // jumps straight to 20, not the sequential next slot
    CHECK(g_fake_intralvl_cleared == 1); // clear_intralvl_next_level() was called
}

TEST_CASE_METHOD(GameCallbacksFixture, "next_singleplayer_level ignores the intralevel provider when ignore is true", "[kfx_config][config]") {
    campaign.single_levels[0] = 10;
    campaign.single_levels[1] = 20;
    g_fake_intralvl_next_level = 999; // would be an error if consulted -- not present in single_levels

    CHECK(next_singleplayer_level(10, true) == 20); // falls through to the sequential-next behavior instead
    CHECK(g_fake_intralvl_cleared == 0); // provider never consulted
}

TEST_CASE_METHOD(GameCallbacksFixture, "next_singleplayer_level treats a negative intralevel jump target as FINISHED", "[kfx_config][config]") {
    campaign.single_levels[0] = 10;
    g_fake_intralvl_call_count = 0;
    g_fake_intralvl_first_call_value = 1;   // passes the outer "> 0" guard
    g_fake_intralvl_second_call_value = -5; // captured as next_level, hitting the "< 0" branch
    callbacks.get_intralvl_next_level = fake_get_intralvl_next_level_changing;

    CHECK(next_singleplayer_level(10, false) == SINGLEPLAYER_FINISHED);
}
