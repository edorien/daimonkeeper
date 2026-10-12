// kfx_game: keeper_assist_outbox.c -- the player's combat assistant profile sent as the player's own packets
// (docs/newFeatures/llm/01 section 10): one a turn, never over an action the player's own input put in the packet; the
// packets, applied, make exactly the profile's rules live; nothing in a multiplayer game, a replay, or for a keeper an
// agent or the built-in AI plays.
#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <memory>

#include "kfx_sim/tests/kfx_sim_test_fixtures.h"
#include "config_combat_assist.h"
#include "config_magic.h"
#include "keeper_assist_outbox.h"
#include "keeper_assist_packets.h"
#include "keeper_assist_state.h"
#include "kfx_config_state.h"
#include "kfx_sim_state.h"
#include "packet_data.h"
#include "player_data.h"

namespace {

struct Fixture : kfx_test::ResetSimAndConfig {
    KeeperAssistProfile saved;
    Fixture() {
        kfx_config_state.conf.magic_conf.power_types_count = PwrK_MKTUNNELLER + 1;
        kfx_sim_state.game_kind = GKind_LocalGame;
        replay.load_enable = false;
        PlayerInfo *pl = kfx_test::make_player_active(0);
        pl->player_type = PT_Keeper;
        saved = *keeper_assist_profile();
        KeeperAssistProfile *p = keeper_assist_profile();
        keeper_assist_profile_defaults(p);
        p->globals.enabled = 1;
        keeper_assist_default_rule(&p->custom[0], KAA_Cast, PwrK_HEALCRTR, KAT_AllyAny);
        p->custom[0].cond[0] = {KAF_HealthPct, KAO_Lt, 50};
        p->custom[0].cond_count = 1;
        keeper_assist_default_rule(&p->custom[1], KAA_PowerOff, PwrK_OBEY, KAT_None);
        p->custom_count = 2;
        keeper_assist_outbox_reset();
    }
    ~Fixture() {
        *keeper_assist_profile() = saved;
        keeper_assist_outbox_reset();
    }
    /** Runs the outbox for one turn on an empty packet (or one carrying `action`) and applies whatever it wrote. */
    uint8_t turn(uint8_t action = PckA_None) {
        Packet pk;
        std::memset(&pk, 0, sizeof(pk));
        pk.action = action;
        keeper_assist_outbox_tick(&pk, 0);
        if (pk.action != action || action == PckA_None) {
            if (pk.action != PckA_None) keeper_assist_apply_packet(&kfx_sim_state.keeper_assist[0], pk.action, pk.actn_par1, pk.actn_par2);
        }
        return pk.action;
    }
    int drain(int limit = 200) {
        int n = 0;
        while (turn() != PckA_None && n < limit) n++;
        return n;
    }
};

} // namespace

TEST_CASE_METHOD(Fixture, "the profile goes out one packet a turn and ends up live exactly", "[kfx_game][assist_outbox]") {
    const int sent = drain();
    CHECK(sent > 0);
    CHECK(sent <= KEEPER_ASSIST_MAX_PACKETS);
    const KeeperAssistState &st = kfx_sim_state.keeper_assist[0];
    CHECK(std::memcmp(&st.live[0], &keeper_assist_profile()->custom[0], sizeof(KeeperAssistRule)) == 0);
    CHECK(std::memcmp(&st.live[1], &keeper_assist_profile()->custom[1], sizeof(KeeperAssistRule)) == 0);
    CHECK(st.globals.enabled == 1);
    CHECK(keeper_assist_outbox_pending() == 0);
}

TEST_CASE_METHOD(Fixture, "the player's own action goes first: a packet already carrying one is left alone", "[kfx_game][assist_outbox]") {
    CHECK(turn() != PckA_None);                 // the first one goes out
    const int64_t left = keeper_assist_outbox_pending();
    CHECK(turn(PckA_TogglePause) == PckA_TogglePause);
    CHECK(turn(PckA_UsePwrObey) == PckA_UsePwrObey);
    CHECK(keeper_assist_outbox_pending() == left); // nothing was spent on those turns
    drain();
    CHECK(kfx_sim_state.keeper_assist[0].live[0].used == 1);
}

TEST_CASE_METHOD(Fixture, "rules already live (a loaded save with them) send nothing", "[kfx_game][assist_outbox]") {
    drain();
    keeper_assist_outbox_reset();
    CHECK(turn() == PckA_None);
    CHECK(keeper_assist_outbox_pending() == 0);
}

TEST_CASE_METHOD(Fixture, "a change is planned again on request: only the changed rule travels", "[kfx_game][assist_outbox]") {
    const int full = drain();
    keeper_assist_profile()->custom[0].cond[0].value = 30;
    keeper_assist_outbox_request();
    const int change = drain();
    CHECK(change == 2 + 1 + 1);                 // the rule, its money, one condition, the commit
    CHECK(change < full);
    CHECK(kfx_sim_state.keeper_assist[0].live[0].cond[0].value == 30);
}

TEST_CASE_METHOD(Fixture, "a profile that is off sends nothing; switching it off sends just that", "[kfx_game][assist_outbox]") {
    keeper_assist_profile()->globals.enabled = 0;
    keeper_assist_outbox_reset();
    CHECK(turn() == PckA_None);                 // a player who never uses it: not a single packet
    keeper_assist_profile()->globals.enabled = 1;
    keeper_assist_outbox_request();
    drain();
    CHECK(kfx_sim_state.keeper_assist[0].globals.enabled == 1);
    keeper_assist_profile()->globals.enabled = 0;
    keeper_assist_outbox_request();
    CHECK(turn() == PckA_AssistEnable);
    CHECK(turn() == PckA_None);
    CHECK(kfx_sim_state.keeper_assist[0].globals.enabled == 0);
    CHECK(kfx_sim_state.keeper_assist[0].live[0].used == 1); // the rules stay, off
}

TEST_CASE_METHOD(Fixture, "nothing for a keeper an agent or the built-in AI plays, in a replay, or in multiplayer", "[kfx_game][assist_outbox]") {
    PlayerInfo *pl = get_player(0);
    pl->allocflags |= PlaF_ExternalSeat;
    CHECK(turn() == PckA_None);
    pl->allocflags &= ~PlaF_ExternalSeat;
    pl->allocflags |= PlaF_CompCtrl;
    CHECK(turn() == PckA_None);
    pl->allocflags &= ~PlaF_CompCtrl;
    replay.load_enable = true;
    CHECK(turn() == PckA_None);
    replay.load_enable = false;
    kfx_sim_state.game_kind = GKind_MultiGame;
    CHECK(turn() == PckA_None);
    kfx_sim_state.game_kind = GKind_LocalGame;
    CHECK(kfx_sim_state.keeper_assist[0].live[0].used == 0);
    keeper_assist_outbox_reset();
    CHECK(turn() != PckA_None);                 // and as soon as none of those holds, it goes
}
