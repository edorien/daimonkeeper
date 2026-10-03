#include "ftest_local_view_transitions.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include "../ftest.h"
#include "../ftest_util.h"

#include "player_data.h"
#include "player_instances.h"
#include "thing_creature.h"
#include "thing_data.h"
#include "dungeon_data.h"
#include "packet_data.h"
#include "kfx_sim_state.h"
#include "config_settings.h"
#include "frontend.h"
#include "gui_parchment.h"
#include "config_keeperfx.h"
#if __has_include("local_state.h")
#include "local_state.h" // kfx_render since refactor pass 2's S15 (was player_data.h)
#endif

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

// Refactor pass 2, S15 moved the local player's view-transition side effects
// (palette fades, menus, the map's UI hold) out of kfx_sim's player
// instances into kfx_frontend's local_view.c. No other ftest possesses a
// creature or opens the parchment map, so this one drives each transition
// and logs the local presentation state every turn ("LVT:" lines) for a
// before/after comparison, then checks the map hold restores the UI.

struct ftest_local_view_transitions__variables
{
    struct Thing *creature;
    GameTurn phase_start;
    TbBool tooltips_before;
    int64_t status_menu_before;
};
struct ftest_local_view_transitions__variables ftest_local_view_transitions__vars = {
    .creature = NULL,
    .phase_start = 0,
    .tooltips_before = false,
    .status_menu_before = 0,
};

/** Give up on a phase that doesn't finish: the view never reached its target. */
#define LVT_PHASE_TIMEOUT 600
/** Turns to stay in a view before leaving it. */
#define LVT_DWELL 24

static void log_local_view(const char *phase)
{
    struct PlayerInfo *player = get_player(PLAYER0);
    JUSTLOG("LVT: t=%lld %s view=%d inst=%d fade_poss=%lld fade_map=%lld sm_hidden=%d tt_hidden=%d tooltips=%d show_panel=%d status_menu=%lld",
        (long long)get_gameturn(), phase, (int)player->view_type, (int)player->instance_num,
        (long long)local_state.palette_fade_step_possession, (long long)local_state.palette_fade_step_map,
        (int)local_state.status_menu_hidden_for_map, (int)local_state.tooltips_hidden_for_map, (int)settings.tooltips_on,
        (int)((kfx_sim_state.operation_flags & GOF_ShowPanel) != 0), (long long)menu_is_active(GMnu_MAIN));
}

/** True once the player shows `view` with no instance running; fails the test on a timeout. */
static TbBool reached_view(struct ftest_local_view_transitions__variables *vars, int64_t view, const char *phase)
{
    struct PlayerInfo *player = get_player(PLAYER0);
    log_local_view(phase);
    if ((player->view_type == view) && (player->instance_num == PI_Unset))
        return true;
    if (get_gameturn() > vars->phase_start + LVT_PHASE_TIMEOUT)
    {
        FTEST_FAIL_TEST("%s: view %d not reached (view %d, instance %d)", phase, (int)view, (int)player->view_type, (int)player->instance_num);
        return true;
    }
    return false;
}

static TbBool dwelt(struct ftest_local_view_transitions__variables *vars)
{
    return get_gameturn() >= vars->phase_start + LVT_DWELL;
}

static void start_phase(struct ftest_local_view_transitions__variables *vars)
{
    vars->phase_start = get_gameturn();
}

/** What magic_use_power_possess() does before its instance: point the player at the creature. */
static void influence_creature(struct PlayerInfo *player, struct Thing *creatng, int64_t instance)
{
    player->influenced_thing_idx = creatng->index;
    player->influenced_thing_creation = creatng->creation_turn;
    set_player_instance(player, instance, false);
}

static FTestActionResult spawn_and_possess(struct FTestActionArgs* const args)
{
    struct ftest_local_view_transitions__variables* const vars = args->data;
    struct Thing *heart = get_player_soul_container(PLAYER0);
    if (thing_is_invalid(heart))
    {
        FTEST_FAIL_TEST("Player 0 has no dungeon heart");
        return FTRs_Go_To_Next_Action;
    }
    vars->creature = create_owned_special_digger(heart->mappos.x.val + 3 * COORD_PER_STL, heart->mappos.y.val, PLAYER0);
    if (thing_is_invalid(vars->creature))
    {
        FTEST_FAIL_TEST("Failed to create a creature");
        return FTRs_Go_To_Next_Action;
    }
    ftest_util_move_camera_to_thing(vars->creature, PLAYER0);
    vars->tooltips_before = settings.tooltips_on;
    vars->status_menu_before = menu_is_active(GMnu_MAIN);
    log_local_view("start");
    start_phase(vars);
    influence_creature(get_player(PLAYER0), vars->creature, PI_DirctCtrl);
    return FTRs_Go_To_Next_Action;
}

static FTestActionResult leave_possession(struct FTestActionArgs* const args)
{
    struct ftest_local_view_transitions__variables* const vars = args->data;
    if (!reached_view(vars, PVT_CreatureContrl, "possessed") || !dwelt(vars))
        return FTRs_Repeat_Current_Action;
    start_phase(vars);
    set_player_instance(get_player(PLAYER0), PI_DirctCtLeave, false);
    return FTRs_Go_To_Next_Action;
}

static FTestActionResult ride_as_passenger(struct FTestActionArgs* const args)
{
    struct ftest_local_view_transitions__variables* const vars = args->data;
    if (!reached_view(vars, PVT_DungeonTop, "left possession") || !dwelt(vars))
        return FTRs_Repeat_Current_Action;
    start_phase(vars);
    influence_creature(get_player(PLAYER0), vars->creature, PI_PsngrCtrl);
    return FTRs_Go_To_Next_Action;
}

static FTestActionResult leave_passenger(struct FTestActionArgs* const args)
{
    struct ftest_local_view_transitions__variables* const vars = args->data;
    if (!reached_view(vars, PVT_CreaturePasngr, "passenger") || !dwelt(vars))
        return FTRs_Repeat_Current_Action;
    start_phase(vars);
    set_player_instance(get_player(PLAYER0), PI_PsngrCtLeave, false);
    return FTRs_Go_To_Next_Action;
}

static FTestActionResult possess_again(struct FTestActionArgs* const args)
{
    struct ftest_local_view_transitions__variables* const vars = args->data;
    if (!reached_view(vars, PVT_DungeonTop, "left passenger") || !dwelt(vars))
        return FTRs_Repeat_Current_Action;
    start_phase(vars);
    influence_creature(get_player(PLAYER0), vars->creature, PI_DirctCtrl);
    return FTRs_Go_To_Next_Action;
}

static FTestActionResult kill_possessed(struct FTestActionArgs* const args)
{
    struct ftest_local_view_transitions__variables* const vars = args->data;
    if (!reached_view(vars, PVT_CreatureContrl, "possessed again") || !dwelt(vars))
        return FTRs_Repeat_Current_Action;
    start_phase(vars);
    kill_creature(vars->creature, INVALID_THING, -1, CrDed_NoEffects);
    vars->creature = NULL;
    return FTRs_Go_To_Next_Action;
}

static FTestActionResult open_map(struct FTestActionArgs* const args)
{
    struct ftest_local_view_transitions__variables* const vars = args->data;
    if (!reached_view(vars, PVT_DungeonTop, "possessed died") || !dwelt(vars))
        return FTRs_Repeat_Current_Action;
    start_phase(vars);
    zoom_to_parchment_map(); // the live path: no fade, predicted locally
    return FTRs_Go_To_Next_Action;
}

static FTestActionResult close_map(struct FTestActionArgs* const args)
{
    struct ftest_local_view_transitions__variables* const vars = args->data;
    if (!reached_view(vars, PVT_MapScreen, "map") || !dwelt(vars))
        return FTRs_Repeat_Current_Action;
    start_phase(vars);
    zoom_from_parchment_map();
    return FTRs_Go_To_Next_Action;
}

static FTestActionResult fade_to_map(struct FTestActionArgs* const args)
{
    struct ftest_local_view_transitions__variables* const vars = args->data;
    if (!reached_view(vars, PVT_DungeonTop, "map closed") || !dwelt(vars))
        return FTRs_Repeat_Current_Action;
    start_phase(vars);
    set_players_packet_action(get_player(PLAYER0), PckA_SetViewType, PVT_MapFadeIn, 0, 0, 0);
    return FTRs_Go_To_Next_Action;
}

static FTestActionResult fade_from_map(struct FTestActionArgs* const args)
{
    struct ftest_local_view_transitions__variables* const vars = args->data;
    if (!reached_view(vars, PVT_MapScreen, "faded to map") || !dwelt(vars))
        return FTRs_Repeat_Current_Action;
    start_phase(vars);
    set_players_packet_action(get_player(PLAYER0), PckA_SetViewType, PVT_MapFadeOut, 0, 0, 0);
    return FTRs_Go_To_Next_Action;
}

static FTestActionResult check_ui_restored(struct FTestActionArgs* const args)
{
    struct ftest_local_view_transitions__variables* const vars = args->data;
    if (!reached_view(vars, PVT_DungeonTop, "faded from map") || !dwelt(vars))
        return FTRs_Repeat_Current_Action;
    if (local_state.status_menu_hidden_for_map || local_state.tooltips_hidden_for_map)
    {
        FTEST_FAIL_TEST("The map still holds the UI (status menu %d, tooltips %d)",
            (int)local_state.status_menu_hidden_for_map, (int)local_state.tooltips_hidden_for_map);
    }
    if (settings.tooltips_on != vars->tooltips_before)
    {
        FTEST_FAIL_TEST("Tooltips %d after the map, %d before", (int)settings.tooltips_on, (int)vars->tooltips_before);
    }
    if (menu_is_active(GMnu_MAIN) != vars->status_menu_before)
    {
        FTEST_FAIL_TEST("Status menu %lld after the map, %lld before", (long long)menu_is_active(GMnu_MAIN), (long long)vars->status_menu_before);
    }
    return FTRs_Go_To_Next_Action;
}

TbBool ftest_local_view_transitions_init()
{
    struct ftest_local_view_transitions__variables *vars = &ftest_local_view_transitions__vars;
    ftest_append_action(spawn_and_possess, 20, vars);
    ftest_append_action(leave_possession, 0, vars);
    ftest_append_action(ride_as_passenger, 0, vars);
    ftest_append_action(leave_passenger, 0, vars);
    ftest_append_action(possess_again, 0, vars);
    ftest_append_action(kill_possessed, 0, vars);
    ftest_append_action(open_map, 0, vars);
    ftest_append_action(close_map, 0, vars);
    ftest_append_action(fade_to_map, 0, vars);
    ftest_append_action(fade_from_map, 0, vars);
    ftest_append_action(check_ui_restored, 0, vars);
    return true;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
