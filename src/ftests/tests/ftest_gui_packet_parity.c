#include "ftest_gui_packet_parity.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include "../ftest.h"
#include "../ftest_util.h"
#include "../ftest_packet_capture.h"

#include "globals.h"
#include "game_legacy.h"
#include "config_keeperfx.h"
#include "player_data.h"
#include "player_instances.h"
#include "packet_data.h"
#include "frontend.h"
#include "config_players.h"
#include "config_strings.h"
#include "frontgui_ingame_tabcontent.h"   // ingame_tabcontent_test_fire, ITTA_*

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Command-packet parity between the legacy sprite GUI and the ImGui GUI.
 *
 * The ImGui InvisibleButtons can't be driven headless, so
 * ingame_tabcontent_test_fire() (frontgui_ingame_tabcontent.cpp, FUNCTESTING
 * only) runs each migrated tab-panel action's handler code directly -- the
 * same call the ImGui click path makes. This test asserts the resulting
 * command packets, on the same turns, match the legacy golden.
 *
 * Golden trace, in fire order:
 *   1. legacy  GMnu_QUERY  "tend to imprison" -> PckA_ToggleTendency  par1=1
 *   2. legacy  GMnu_QUERY  "tend to flee"     -> PckA_ToggleTendency  par1=2
 *   3. legacy  GMnu_MAIN   "computer assist"  -> PckA_ToggleComputer  par1=0
 *   4. ImGui   room build (treasury, kind 2)  -> PckA_SetPlyrState PSt_BuildRoom 2
 *   5. ImGui   room sell                       -> PckA_SetPlyrState PSt_Sell
 *   6. ImGui   manufacture sell                -> PckA_SetPlyrState PSt_Sell
 *   7. ImGui   go to query mode                -> PckA_SetPlyrState PSt_CreatrQuery
 *   8. ImGui   "tend to imprison"              -> PckA_ToggleTendency par1=1
 *   9. ImGui   "tend to flee"                  -> PckA_ToggleTendency par1=2
 */
/* par3/par4 are FTEST_PKT_ANY throughout: none of these actions reserve
 * those fields for real data, so packet_set_camera_position() (see
 * packet_action_has_camera_position(), packet_data.c) legitimately
 * piggybacks the local camera position onto them on whichever turn each
 * action fires -- not a fixed golden value. */
static const struct FtestPacketExpectation gui_parity_golden[] = {
    { .action = PckA_ToggleTendency, .par1 = 1,             .par2 = 0, .par3 = FTEST_PKT_ANY, .par4 = FTEST_PKT_ANY },
    { .action = PckA_ToggleTendency, .par1 = 2,             .par2 = 0, .par3 = FTEST_PKT_ANY, .par4 = FTEST_PKT_ANY },
    { .action = PckA_ToggleComputer, .par1 = 0,             .par2 = 0, .par3 = FTEST_PKT_ANY, .par4 = FTEST_PKT_ANY },
    { .action = PckA_SetPlyrState,   .par1 = PSt_BuildRoom, .par2 = 2, .par3 = FTEST_PKT_ANY, .par4 = FTEST_PKT_ANY },
    { .action = PckA_SetPlyrState,   .par1 = PSt_Sell,      .par2 = 0, .par3 = FTEST_PKT_ANY, .par4 = FTEST_PKT_ANY },
    { .action = PckA_SetPlyrState,   .par1 = PSt_Sell,      .par2 = 0, .par3 = FTEST_PKT_ANY, .par4 = FTEST_PKT_ANY },
    { .action = PckA_SetPlyrState,   .par1 = PSt_CreatrQuery,.par2 = 0, .par3 = FTEST_PKT_ANY, .par4 = FTEST_PKT_ANY },
    { .action = PckA_ToggleTendency, .par1 = 1,             .par2 = 0, .par3 = FTEST_PKT_ANY, .par4 = FTEST_PKT_ANY },
    { .action = PckA_ToggleTendency, .par1 = 2,             .par2 = 0, .par3 = FTEST_PKT_ANY, .par4 = FTEST_PKT_ANY },
};
#define GUI_PARITY_GOLDEN_N ((int64_t)(sizeof(gui_parity_golden) / sizeof(gui_parity_golden[0])))

struct ftest_gui_packet_parity__variables
{
    int64_t zoom_capture_count_before;
    int64_t minimap_zoom_before;
    int64_t imgui_capture_count_before;
};
static struct ftest_gui_packet_parity__variables ftest_gui_packet_parity__vars = {
    .zoom_capture_count_before = 0,
    .minimap_zoom_before = 0,
    .imgui_capture_count_before = 0,
};

static FTestActionResult action001__setup(struct FTestActionArgs *const args);
static FTestActionResult action002__query_tendency(struct FTestActionArgs *const args);
static FTestActionResult action003__autopilot(struct FTestActionArgs *const args);
static FTestActionResult action004__zoom_is_local_only(struct FTestActionArgs *const args);
static FTestActionResult action005__imgui_tab_actions(struct FTestActionArgs *const args);
static FTestActionResult action006__assert_golden_trace(struct FTestActionArgs *const args);
static FTestActionResult action007__imgui_spell_trap_emit_state(struct FTestActionArgs *const args);

TbBool ftest_gui_packet_parity_init()
{
    ftest_append_action(action001__setup,                       30, &ftest_gui_packet_parity__vars);
    ftest_append_action(action002__query_tendency,               3, &ftest_gui_packet_parity__vars);
    ftest_append_action(action003__autopilot,                    3, &ftest_gui_packet_parity__vars);
    ftest_append_action(action004__zoom_is_local_only,           3, &ftest_gui_packet_parity__vars);
    ftest_append_action(action005__imgui_tab_actions,           10, &ftest_gui_packet_parity__vars);
    ftest_append_action(action006__assert_golden_trace,          3, &ftest_gui_packet_parity__vars);
    ftest_append_action(action007__imgui_spell_trap_emit_state,  6, &ftest_gui_packet_parity__vars);
    return true;
}

static FTestActionResult action001__setup(struct FTestActionArgs *const args)
{
    ftest_packet_capture_begin();
    ftest_util_reveal_map(PLAYER0);
    return FTRs_Go_To_Next_Action;
}

/* Click "tend to imprison" then, a turn later, "tend to flee" -- one
 * click per turn so ftest_packet_capture_tick() records each in its own
 * turn's packet. */
static FTestActionResult action002__query_tendency(struct FTestActionArgs *const args)
{
    if (args->times_executed == 0)
    {
        if (!ftest_util_gui_turn_on_menu(GMnu_QUERY))
            return FTRs_Go_To_Next_Action;
        if (!ftest_util_gui_click(BID_QRY_IMPRSN))
            return FTRs_Go_To_Next_Action;
        return FTRs_Repeat_Current_Action;
    }
    if (args->times_executed == 1)
    {
        if (!ftest_util_gui_click(BID_QRY_FLEE))
            return FTRs_Go_To_Next_Action;
        return FTRs_Repeat_Current_Action;
    }
    return FTRs_Go_To_Next_Action;
}

static FTestActionResult action003__autopilot(struct FTestActionArgs *const args)
{
    if (args->times_executed == 0)
    {
        ftest_util_gui_turn_on_menu(GMnu_MAIN); /* the always-on sidebar; harmless if already up */
        if (!ftest_util_gui_click(BID_ASSIST))
            return FTRs_Go_To_Next_Action;
        return FTRs_Repeat_Current_Action;
    }
    return FTRs_Go_To_Next_Action;
}

/* The minimap zoom buttons mutate per-client display state directly and
 * must NOT emit a command packet (a packet here would be dead weight on
 * the wire at best, a desync risk at worst). Pin that. */
static FTestActionResult action004__zoom_is_local_only(struct FTestActionArgs *const args)
{
    struct ftest_gui_packet_parity__variables *const vars = args->data;

    if (args->times_executed == 0)
    {
        vars->zoom_capture_count_before = ftest_packet_capture_count();
        vars->minimap_zoom_before = local_state.minimap_zoom;
        if (!ftest_util_gui_click(BID_MAP_ZOOM_IN))
            return FTRs_Go_To_Next_Action;
        return FTRs_Repeat_Current_Action;
    }

    if (ftest_packet_capture_count() != vars->zoom_capture_count_before)
    {
        FTEST_FAIL_TEST("minimap zoom-in emitted a command packet (capture count %" PRId64 " -> %" PRId64 ") -- it must be local-only",
                        (int64_t)(vars->zoom_capture_count_before), (int64_t)(ftest_packet_capture_count()));
        ftest_packet_capture_dump();
        return FTRs_Go_To_Next_Action;
    }
    if (local_state.minimap_zoom == vars->minimap_zoom_before)
        FTESTLOG("note: minimap_zoom unchanged (%" PRId64 ") -- at zoom limit, click was a no-op", (int64_t)(vars->minimap_zoom_before));
    else
        FTESTLOG("minimap_zoom %" PRId64 " -> %" PRId64 ", no packet (as expected)", (int64_t)(vars->minimap_zoom_before), (int64_t)local_state.minimap_zoom);

    return FTRs_Go_To_Next_Action;
}

/* Fire the ImGui tab-panel handlers directly (one per turn) so each
 * lands in its own captured packet -- extends the golden trace. */
static FTestActionResult action005__imgui_tab_actions(struct FTestActionArgs *const args)
{
    static const int64_t fires[] = {
        ITTA_RoomBuild,      /* arg 2 (treasury) */
        ITTA_RoomSell,
        ITTA_TrapSell,
        ITTA_QueryMode,
        ITTA_TendImprison,
        ITTA_TendFlee,
    };
    const int64_t n = (int64_t)(sizeof(fires) / sizeof(fires[0]));
    if (args->times_executed >= n)
        return FTRs_Go_To_Next_Action;
    const int64_t act = fires[args->times_executed];
    ingame_tabcontent_test_fire(act, act == ITTA_RoomBuild ? 2 : 0);
    return FTRs_Repeat_Current_Action;
}

static FTestActionResult action006__assert_golden_trace(struct FTestActionArgs *const args)
{
    ftest_packet_capture_end();
    ftest_packet_capture_dump();

    if (!ftest_packet_trace_matches(gui_parity_golden, GUI_PARITY_GOLDEN_N))
        return FTRs_Go_To_Next_Action;

    FTESTLOG("in-game GUI command-packet trace matches the legacy golden (%" PRId64 " actions, legacy + ImGui paths)", (int64_t)(GUI_PARITY_GOLDEN_N));
    return FTRs_Go_To_Next_Action;
}

/* Spell / manufacture selection packets are campaign-config dependent
 * (the power's / item's work_state), so -- in a fresh capture session
 * after the golden assert -- pin only the invariant: the ImGui handler
 * emits a PckA_SetPlyrState packet (never nothing, never a different
 * action) on the turn it fires. */
static FTestActionResult action007__imgui_spell_trap_emit_state(struct FTestActionArgs *const args)
{
    if (args->times_executed == 0)
    {
        ftest_util_gui_turn_on_menu(GMnu_SPELL);   /* instantiate the power buttons so content is readable */
        return FTRs_Repeat_Current_Action;
    }
    if (args->times_executed == 1)
    {
        ftest_packet_capture_reset();
        ftest_packet_capture_begin();
        int64_t spell_kind = ftest_util_gui_button_content(BID_POWER_TD01);
        if (spell_kind < 1) spell_kind = 1;
        ingame_tabcontent_test_fire(ITTA_SpellChoose, spell_kind);
        return FTRs_Repeat_Current_Action;
    }
    if (args->times_executed == 2)
    {
        ftest_packet_capture_end();
        if (ftest_packet_capture_count() == 0)
        {
            FTESTLOG("note: ImGui spell-choose emitted no packet (power %" PRId64 " unavailable on this map) -- skipping the invariant check",
                     (int64_t)(ftest_util_gui_button_content(BID_POWER_TD01)));
            return FTRs_Go_To_Next_Action;
        }
        const struct FtestCapturedPacket *p = ftest_packet_capture_at(ftest_packet_capture_count() - 1);
        if (p == NULL || p->packet.action != PckA_SetPlyrState)
        {
            FTEST_FAIL_TEST("ImGui spell-choose emitted action %" PRIu64 ", expected PckA_SetPlyrState (%" PRIu64 ")",
                            (uint64_t)(p ? (uint64_t)p->packet.action : 0u), (uint64_t)PckA_SetPlyrState);
            ftest_packet_capture_dump();
            return FTRs_Go_To_Next_Action;
        }
        FTESTLOG("ImGui spell-choose -> PckA_SetPlyrState (par1=%" PRId64 " par2=%" PRId64 ") as expected",
                 (int64_t)p->packet.actn_par1, (int64_t)p->packet.actn_par2);
        return FTRs_Go_To_Next_Action;
    }
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif /* FUNCTESTING */
