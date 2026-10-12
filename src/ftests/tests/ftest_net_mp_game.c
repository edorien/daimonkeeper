// Refactor pass 5, S03 target 2: a real two-player multiplayer game, two processes over ENet on 127.0.0.1. The host
// (net_mp_game_host) serves and waits in the lobby; the client (net_mp_game_join) connects and logs in; the lobby
// starts original 50 as a multiplayer game (ftest.h's net_role), and from then on the two exchange their packets every
// turn (kfx_net's gameplay exchange, input lag, checksums).
//
// Each keeper plays a short script through its own packets, injected before the exchange so the other machine runs
// them too (ftest_packet_inject.h), once the game takes input: tag a block for digging next to its heart, build a
// treasury on its claimed floor, make an imp. The host then pauses and unpauses the game, and forces a desync (intentional_desync(), the console's
// `desync`): the checksums must find it and the resync bring the client back. Every NET_HASH_EVERY turns, outside the
// desync's window, each side logs a hash of what replays check (compute_replay_integrity()) as "NETHASH <turn> <hash>":
// scripts/run_ftest_coverage.sh, which runs the two together, fails the pair when the two sides' lines differ. At
// NET_LAST_TURN the client leaves and the host goes on until it has seen it go (the drop path).
#include "ftest_net_mp_game.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include "../ftest.h"
#include "../ftest_packet_inject.h"

#include "config_keeperfx.h"
#include "config_magic.h"
#include "config_players.h"
#include "dungeon_data.h"
#include "game_replay.h"
#include "kfx_sim_state.h"
#include "net_main.h"
#include "kfx_net_state.h"
#include "net_resync.h"
#include "packets.h"
#include "packet_data.h"
#include "player_data.h"
#include "room_data.h"
#include "slab_data.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NET_HASH_EVERY 100
#define NET_SCRIPT_TURN 80
#define NET_CHECK_TURN 500
#define NET_DESYNC_TURN 650
#define NET_RESYNC_BUDGET 300
#define NET_LAST_TURN 1500
#define NET_DROP_BUDGET 2000

#define CLICK (PCtr_LBtnClick | PCtr_LBtnHeld)
#define HELD  (PCtr_LBtnHeld)
#define REL   (PCtr_LBtnRelease)

struct Step { unsigned char action; int64_t p1, p2; TbBool pos; MapSlabCoord sx, sy; unsigned char ctx; uint64_t ctrl; };
static struct Step s_script[64];
static int64_t s_n, s_i;
static GameTurn s_next_hash;
static int64_t s_rooms_at_start, s_imps_at_start;
static int64_t s_paused_calls;
static GameTurn s_pause_requested_turn;

static void S_act(unsigned char a, int64_t p1, int64_t p2) { struct Step s = {0}; s.action = a; s.p1 = p1; s.p2 = p2; s_script[s_n++] = s; }
static void S_pos(MapSlabCoord sx, MapSlabCoord sy, unsigned char ctx, uint64_t ctrl) { struct Step s = {0}; s.pos = true; s.sx = sx; s.sy = sy; s.ctx = ctx; s.ctrl = ctrl; s_script[s_n++] = s; }
static void S_idle(int64_t n) { while (n-- > 0) { struct Step s = {0}; s_script[s_n++] = s; } }

static TbBool my_claimed(MapSlabCoord x, MapSlabCoord y)
{
    if ((x < 1) || (y < 1) || (x >= kfx_sim_state.map_tiles_x - 1) || (y >= kfx_sim_state.map_tiles_y - 1))
        return false;
    const struct SlabMap *slb = get_slabmap_block(x, y);
    return (slb->kind == SlbT_CLAIMED) && (slabmap_owner(slb) == my_player_number);
}

static MapSlabCoord s_hx, s_hy, s_dig_y;
static int s_dir;

/** The keeper's script, from where its heart is (original 50: a 3x3 pedestal, a ring of claimed floor, the walls, then
 *  earth): tag the wall towards the middle of the map and the two rows of earth behind it for digging, build a
 *  treasury on the ring west of the heart, make an imp on the ring east of it. */
static void build_script(void)
{
    s_n = 0;
    s_i = 0;
    const struct Thing *heart = get_player_soul_container(my_player_number);
    s_hx = subtile_slab(heart->mappos.x.stl.num);
    s_hy = subtile_slab(heart->mappos.y.stl.num);
    s_dir = (s_hy < kfx_sim_state.map_tiles_y / 2) ? 1 : -1;
    s_dig_y = s_hy + 3 * s_dir;
    // the pickaxe's one-slab brush dragged over the 3x3 block row by row (ftest_ai_gesture_drag_verbs.c's serpentine),
    // starting at the wall next to the claimed floor, so the imps reach the rest
    S_act(PckA_SetPlyrState, PSt_CtrlDungeon, 0);
    S_act(PckA_SetRoomspaceMan, 1, 0);
    S_idle(1);
    S_pos(s_hx - 1, s_dig_y, CSt_PickAxe, CLICK);
    MapSlabCoord prev_x = s_hx - 1;
    for (int r = 0; r < 3; r++)
    {
        const MapSlabCoord y = s_dig_y + r * s_dir;
        if (r > 0)
            S_pos(prev_x, y, CSt_PickAxe, HELD);
        const MapSlabCoord x = (r % 2 == 0) ? (s_hx + 1) : (s_hx - 1);
        S_pos(x, y, CSt_PickAxe, HELD);
        prev_x = x;
    }
    S_pos(prev_x, s_dig_y + 2 * s_dir, CSt_PickAxe, REL);
    if (my_claimed(s_hx - 2, s_hy))
    {
        S_act(PckA_SetPlyrState, PSt_BuildRoom, RoK_TREASURE);
        S_act(PckA_SetRoomspaceDrag, 0, 0);
        S_idle(1);
        S_pos(s_hx - 2, s_hy, CSt_DefaultArrow, CLICK);
        S_pos(s_hx - 2, s_hy, CSt_DefaultArrow, REL);
    }
    if (my_claimed(s_hx + 2, s_hy))
    {
        S_act(PckA_SetPlyrState, PSt_CreateDigger, PwrK_MKDIGGER);
        S_idle(1);
        S_pos(s_hx + 2, s_hy, CSt_DefaultArrow, CLICK);
        S_pos(s_hx + 2, s_hy, CSt_DefaultArrow, REL);
    }
    S_act(PckA_SetPlyrState, PSt_CtrlDungeon, 0);
    FTESTLOG("player %d: heart at slab (%ld,%ld); digging rows %ld..%ld; %" PRId64 " steps", (int)my_player_number,
        (long)s_hx, (long)s_hy, (long)s_dig_y, (long)(s_dig_y + 2 * s_dir), s_n);
}

/** How many of the tagged block's slabs aren't earth or wall any more (dug). */
static int64_t dug_slabs(void)
{
    int64_t n = 0;
    for (MapSlabCoord y = s_dig_y, k = 0; k < 3; k++, y += s_dir)
        for (MapSlabCoord x = s_hx - 1; x <= s_hx + 1; x++)
        {
            const struct SlabMap *slb = get_slabmap_block(x, y);
            if ((slb->kind == SlbT_CLAIMED) || (slb->kind == SlbT_PATH))
                n++;
        }
    return n;
}

static int64_t count_rooms(void)
{
    int64_t n = 0;
    for (int64_t i = 1; i < ROOMS_COUNT; i++)
    {
        const struct Room *room = &kfx_sim_state.rooms[i];
        if (room_exists(room) && (room->owner == my_player_number))
            n++;
    }
    return n;
}

static void log_hash(void)
{
    const GameTurn turn = get_gameturn();
    if (turn < s_next_hash)
        return;
    // the desync's window (the host's state is changed, then sent to the client) is left out
    if ((turn < NET_DESYNC_TURN) || (turn >= NET_DESYNC_TURN + NET_RESYNC_BUDGET))
        FTESTLOG("NETHASH %" PRId64 " %016" PRIx64, (int64_t)s_next_hash, (uint64_t)compute_replay_integrity());
    s_next_hash += NET_HASH_EVERY;
}

static FTestActionResult net_mp_game_start(struct FTestActionArgs* const args)
{
    FTESTLOG("multiplayer game: I am player %d (net user %d), game kind %d, %d human players", (int)my_player_number,
        (int)netstate.my_id, (int)kfx_sim_state.game_kind, (int)kfx_sim_state.human_players_count);
    if (kfx_sim_state.game_kind != GKind_MultiGame)
        FTEST_FAIL_TEST("the level didn't start as a multiplayer game");
    s_next_hash = NET_HASH_EVERY;
    s_rooms_at_start = count_rooms();
    s_imps_at_start = get_players_num_dungeon(my_player_number)->num_active_diggers;
    build_script();
    return FTRs_Go_To_Next_Action;
}

/** One step of the keeper's script a turn, from turn 20. */
static FTestActionResult net_mp_game_script(struct FTestActionArgs* const args)
{
    log_hash();
    if (s_i >= s_n)
        return FTRs_Go_To_Next_Action;
    const struct Step *s = &s_script[s_i++];
    struct FtestPacketInject r = {0};
    r.action = s->action;
    r.par1 = s->p1;
    r.par2 = s->p2;
    if (s->pos)
    {
        r.has_position = true;
        r.pos_x = subtile_coord_center(slab_subtile_center(s->sx));
        r.pos_y = subtile_coord_center(slab_subtile_center(s->sy));
        r.context = s->ctx;
        r.control_flags = s->ctrl;
    }
    ftest_packet_inject_queue_local(&r);
    return FTRs_Repeat_Current_Action;
}

/** What the script did, a while later. */
static FTestActionResult net_mp_game_check_script(struct FTestActionArgs* const args)
{
    log_hash();
    const struct Dungeon *dungeon = get_players_num_dungeon(my_player_number);
    FTESTLOG("player %d: rooms %" PRId64 " -> %" PRId64 ", imps %" PRId64 " -> %" PRId64 ", %" PRId64 " of the 9 tagged slabs dug",
        (int)my_player_number, s_rooms_at_start, count_rooms(), s_imps_at_start, (int64_t)dungeon->num_active_diggers,
        dug_slabs());
    if (dug_slabs() == 0)
        FTEST_FAIL_TEST("player %d's tagged slabs weren't dug", (int)my_player_number);
    if (count_rooms() <= s_rooms_at_start)
        FTEST_FAIL_TEST("player %d's treasury wasn't built", (int)my_player_number);
    if (dungeon->num_active_diggers <= s_imps_at_start)
        FTEST_FAIL_TEST("player %d's imp wasn't made", (int)my_player_number);
    return FTRs_Go_To_Next_Action;
}

/** Waits until a turn (args->data), logging the hashes (an action's delay doesn't). */
static FTestActionResult net_mp_game_wait(struct FTestActionArgs* const args)
{
    log_hash();
    return (get_gameturn() < (GameTurn)(intptr_t)args->data) ? FTRs_Repeat_Current_Action : FTRs_Go_To_Next_Action;
}

/** The host pauses the game, then unpauses it, as the pause key does (set_packet_pause_toggle(): a pause packet, which
 *  takes effect input_lag_turns later on every machine; the unpause, once the toggle's cooldown has passed, a
 *  NETMSG_UNPAUSE to the client). Turns don't pass while the game is paused: this counts calls. */
static FTestActionResult net_mp_game_host_pause(struct FTestActionArgs* const args)
{
    log_hash();
    const TbBool paused = (kfx_sim_state.operation_flags & GOF_Paused) != 0;
    if (s_paused_calls == 0)
    {
        set_packet_pause_toggle();
        s_pause_requested_turn = get_gameturn();
        s_paused_calls = 1;
        return FTRs_Repeat_Current_Action;
    }
    if (paused)
    {
        s_paused_calls++;
        set_packet_pause_toggle(); // ignored until the cooldown has passed
        if (s_paused_calls > 2000000)
        {
            FTEST_FAIL_TEST("the game stayed paused");
            return FTRs_Go_To_Next_Action;
        }
        return FTRs_Repeat_Current_Action;
    }
    if (s_paused_calls > 1)
    {
        FTESTLOG("paused and unpaused at turn %" PRId64 " (%" PRId64 " calls paused)", (int64_t)get_gameturn(), s_paused_calls - 1);
        return FTRs_Go_To_Next_Action;
    }
    // the pause packet hasn't taken effect yet: it does input_lag_turns after the turn it was sent in
    if (get_gameturn() > s_pause_requested_turn + kfx_net_state.input_lag_turns + 2)
    {
        FTEST_FAIL_TEST("the pause wasn't taken");
        return FTRs_Go_To_Next_Action;
    }
    return FTRs_Repeat_Current_Action;
}

/** The host changes its own state at NET_DESYNC_TURN (intentional_desync()); the checksums must notice and the host
 *  resync the client. Found and resynced within a turn, so what's checked is the resync's mark: it starts the input
 *  skip of a game's start again (kfx_net_state.skip_initial_input_turns, 0 long before this turn). */
static FTestActionResult net_mp_game_host_desync(struct FTestActionArgs* const args)
{
    log_hash();
    const GameTurn turn = get_gameturn();
    if (turn < NET_DESYNC_TURN)
        return FTRs_Repeat_Current_Action;
    if (turn == NET_DESYNC_TURN)
    {
        FTESTLOG("forcing a desync at turn %" PRId64, (int64_t)turn);
        intentional_desync();
        return FTRs_Repeat_Current_Action;
    }
    if (kfx_net_state.skip_initial_input_turns > 0)
    {
        FTESTLOG("the desync was found and resynced by turn %" PRId64, (int64_t)turn);
        return FTRs_Go_To_Next_Action;
    }
    if (turn > NET_DESYNC_TURN + NET_RESYNC_BUDGET)
    {
        FTEST_FAIL_TEST("the desync wasn't found and resynced within %d turns", NET_RESYNC_BUDGET);
        return FTRs_Go_To_Next_Action;
    }
    return FTRs_Repeat_Current_Action;
}

static FTestActionResult net_mp_game_play(struct FTestActionArgs* const args)
{
    log_hash();
    return (get_gameturn() < NET_LAST_TURN) ? FTRs_Repeat_Current_Action : FTRs_Go_To_Next_Action;
}

static FTestActionResult net_mp_game_host_wait_for_drop(struct FTestActionArgs* const args)
{
    int64_t others = 0;
    for (int64_t i = 0; i < MAX_NET_USERS; i++)
        if ((i != netstate.my_id) && (netstate.users[i].progress != USER_UNUSED))
            others++;
    if (others == 0)
    {
        FTESTLOG("the client has left; the host goes on alone at turn %" PRId64, (int64_t)get_gameturn());
        return FTRs_Go_To_Next_Action;
    }
    if (get_gameturn() > NET_LAST_TURN + NET_DROP_BUDGET)
    {
        FTEST_FAIL_TEST("the client's leaving wasn't seen within %d turns", NET_DROP_BUDGET);
        return FTRs_Go_To_Next_Action;
    }
    return FTRs_Repeat_Current_Action;
}

TbBool ftest_net_mp_game_host_init()
{
    s_paused_calls = 0;
    ftest_append_action(net_mp_game_start, 0, NULL);
    // a multiplayer game takes no input for its first turns (kfx_net_state.skip_initial_input_turns)
    ftest_append_action(net_mp_game_wait, 0, (void *)(intptr_t)NET_SCRIPT_TURN);
    ftest_append_action(net_mp_game_script, 0, NULL);
    ftest_append_action(net_mp_game_wait, 0, (void *)(intptr_t)NET_CHECK_TURN);
    ftest_append_action(net_mp_game_check_script, 0, NULL);
    ftest_append_action(net_mp_game_host_pause, 0, NULL);
    ftest_append_action(net_mp_game_host_desync, 0, NULL);
    ftest_append_action(net_mp_game_play, 0, NULL);
    ftest_append_action(net_mp_game_host_wait_for_drop, 0, NULL);
    return true;
}

TbBool ftest_net_mp_game_join_init()
{
    ftest_append_action(net_mp_game_start, 0, NULL);
    // a multiplayer game takes no input for its first turns (kfx_net_state.skip_initial_input_turns)
    ftest_append_action(net_mp_game_wait, 0, (void *)(intptr_t)NET_SCRIPT_TURN);
    ftest_append_action(net_mp_game_script, 0, NULL);
    ftest_append_action(net_mp_game_wait, 0, (void *)(intptr_t)NET_CHECK_TURN);
    ftest_append_action(net_mp_game_check_script, 0, NULL);
    ftest_append_action(net_mp_game_play, 0, NULL);
    return true;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
