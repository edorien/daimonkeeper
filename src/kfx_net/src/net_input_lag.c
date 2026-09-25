#include "pre_inc.h"
#include "net_input_lag.h"

#include "globals.h"
#include "packets.h"
#include "player_data.h"
#include "net_game.h"
#include "net_exchange_gameplay.h"
#include "net_main.h"
#include "bflib_datetm.h"
#include "kfx_net_state.h"
#include "kfx_sim_state.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
#define INPUT_LAG_SAMPLE_START_TURN 50

static int64_t input_lag_decrease_sample_time;
static int64_t input_lag_decrease_wait_time;
static uint64_t input_lag_decrease_last_update;
static int64_t input_lag_time_spent_waiting;
static unsigned char input_lag_increase_wait_history[INPUT_LAG_INCREASE_SAMPLE_MS];
static uint64_t input_lag_increase_last_update;
static int64_t local_input_lag_request;
static int64_t input_lag_target;
static int64_t input_lag_increase_turns;
static GameTurn input_lag_next_increase_turn;
static int64_t input_lag_adjustment_time = INPUT_LAG_ADJUSTMENT_TIME_1V1_MS;

static void input_lag_update_increase_sample(uint64_t current_time)
{
    uint64_t elapsed = current_time - input_lag_increase_last_update;
    if (elapsed >= INPUT_LAG_INCREASE_SAMPLE_MS) {
        memset(input_lag_increase_wait_history, 0, sizeof(input_lag_increase_wait_history));
        input_lag_time_spent_waiting = 0;
    } else {
        for (uint64_t offset = 1; offset <= elapsed; offset += 1) {
            uint64_t index = (input_lag_increase_last_update + offset) % INPUT_LAG_INCREASE_SAMPLE_MS;
            input_lag_time_spent_waiting -= input_lag_increase_wait_history[index];
            input_lag_increase_wait_history[index] = 0;
        }
    }
    input_lag_increase_last_update = current_time;
}

static void input_lag_reset_samples(void)
{
    input_lag_decrease_sample_time = 0;
    input_lag_decrease_wait_time = 0;
    input_lag_decrease_last_update = LbTimerClock();
    input_lag_time_spent_waiting = 0;
    memset(input_lag_increase_wait_history, 0, sizeof(input_lag_increase_wait_history));
    input_lag_increase_last_update = input_lag_decrease_last_update;
}

void input_lag_reset_request(int64_t input_lag_turns)
{
    input_lag_reset_samples();
    local_input_lag_request = input_lag_turns;
    input_lag_next_increase_turn = 0;
}

void input_lag_reset(void)
{
    input_lag_reset_samples();
    local_input_lag_request = kfx_net_state.input_lag_turns;
    input_lag_target = kfx_net_state.input_lag_turns;
    input_lag_increase_turns = 0;
    input_lag_next_increase_turn = 0;
}

void input_lag_get_stats(int64_t *increase_wait_time, int64_t *increase_turn_time, int64_t *decrease_wait_time, int64_t *decrease_sample_time)
{
    *increase_wait_time = input_lag_time_spent_waiting;
    *increase_turn_time = INPUT_LAG_INCREASE_WAIT_MS;
    *decrease_wait_time = input_lag_decrease_wait_time;
    *decrease_sample_time = input_lag_decrease_sample_time;
}

TbBool input_lag_skips_processing(void)
{
    if (!network_is_active()) {
        return false;
    }
    if ((kfx_sim_state.operation_flags & GOF_Paused) != 0) {
        return true;
    }

    if (kfx_net_state.skip_initial_input_turns > 0) {
        kfx_net_state.skip_initial_input_turns--;
        MULTIPLAYER_LOG("process_packets: Input lag skip turns remaining: %" PRId64 ", skipping packet processing", (int64_t)(kfx_net_state.skip_initial_input_turns));
        return true;
    }
    if (input_lag_increase_turns > 0) {
        input_lag_increase_turns -= 1;
        MULTIPLAYER_LOG("Input lag increase: skipping input turn, remaining=%" PRId64, (int64_t)(input_lag_increase_turns));
        return true;
    }
    if (input_lag_target < kfx_net_state.input_lag_turns) {
        JUSTLOG("Input lag decreased from %" PRId64 " to %" PRId64 "; discarded_turn=%" PRIu64 " target=%" PRId64, (int64_t)(kfx_net_state.input_lag_turns), (int64_t)(kfx_net_state.input_lag_turns - 1), (uint64_t)(get_gameturn() - kfx_net_state.input_lag_turns), (int64_t)(input_lag_target));
        kfx_net_state.input_lag_turns -= 1;
    }
    return false;
}

const int64_t heartZoomTime = 35; //30 isn't enough, it causes palette issues if it desyncs during the heart zoom
int64_t calculate_skip_input(void) {
    if (get_gameturn() <= heartZoomTime) {
        return kfx_net_state.input_lag_turns + heartZoomTime;
    }
    return kfx_net_state.input_lag_turns + (kfx_sim_state.turns_per_second * 0.25);
}

void input_lag_update(struct Packet *packet)
{
    packet->input_lag_turns = 0;
    if (!network_is_active()) { return; }
    int64_t remote_player_count = GetRemoteUserCount();
    input_lag_adjustment_time = INPUT_LAG_ADJUSTMENT_TIME_1V1_MS;
    if (remote_player_count > 1) {
        input_lag_adjustment_time = INPUT_LAG_ADJUSTMENT_TIME_HOST_RELAY_MS;
    }
    if ((kfx_sim_state.operation_flags & GOF_Paused) == 0 && input_lag_increase_turns == 0 && input_lag_target > kfx_net_state.input_lag_turns) {
        JUSTLOG("Input lag increased from %" PRId64 " to %" PRId64, (int64_t)(kfx_net_state.input_lag_turns), (int64_t)(input_lag_target));
        kfx_net_state.input_lag_turns = input_lag_target;
    }
    uint64_t current_time = LbTimerClock();
    input_lag_update_increase_sample(current_time);
    if ((kfx_sim_state.operation_flags & GOF_Paused) == 0 && kfx_net_state.skip_initial_input_turns == 0 && get_gameturn() >= INPUT_LAG_SAMPLE_START_TURN) {
        uint64_t sample_time = current_time - input_lag_decrease_last_update;
        if (sample_time > input_lag_adjustment_time) {
            sample_time = input_lag_adjustment_time;
        }
        input_lag_decrease_sample_time += sample_time;
        if (input_lag_decrease_sample_time >= input_lag_adjustment_time) {
            if (input_lag_decrease_wait_time * 100 <= INPUT_LAG_DECREASE_WAIT_PERCENT * input_lag_decrease_sample_time && local_input_lag_request > 0) {
                local_input_lag_request -= 1;
                input_lag_next_increase_turn = 0;
                MULTIPLAYER_LOG("Input lag request decreased after %" PRId64 "ms spent waiting in %" PRId64 "ms: request=%" PRId64, (int64_t)(input_lag_decrease_wait_time), (int64_t)(input_lag_decrease_sample_time), (int64_t)(local_input_lag_request));
                input_lag_reset_samples();
            } else {
                input_lag_decrease_sample_time = 0;
                input_lag_decrease_wait_time = 0;
            }
        }
    }
    input_lag_decrease_last_update = current_time;
    packet->input_lag_turns = local_input_lag_request;
    if (!network_is_host() || !netstate.sp) { return; }
    for (NetUserId id = 0; id < netstate.max_users; id += 1) {
        if (id == netstate.my_id || netstate.users[id].progress != USER_LOGGEDIN) { continue; }
        const struct Packet *peer_packet = get_latest_history_packet(id);
        if (peer_packet != NULL && (uint8_t)peer_packet->input_lag_turns <= MAXIMUM_INPUT_LAG_TURNS && peer_packet->input_lag_turns > packet->input_lag_turns) {
            packet->input_lag_turns = peer_packet->input_lag_turns;
        }
    }
    if (remote_player_count <= 0) {
        input_lag_reset_samples();
        local_input_lag_request = 0;
        packet->input_lag_turns = 0;
    }
}

void input_lag_note_packet_wait(int64_t wait_time)
{
    if (!network_is_active() || (kfx_sim_state.operation_flags & GOF_Paused) != 0 || get_gameturn() < INPUT_LAG_SAMPLE_START_TURN) { return; }
    uint64_t current_time = LbTimerClock();
    input_lag_update_increase_sample(current_time);
    input_lag_decrease_wait_time += wait_time;
    if (wait_time > INPUT_LAG_INCREASE_SAMPLE_MS) {
        wait_time = INPUT_LAG_INCREASE_SAMPLE_MS;
    }
    for (int64_t offset = 0; offset < wait_time; offset += 1) {
        uint64_t index = (current_time % INPUT_LAG_INCREASE_SAMPLE_MS + INPUT_LAG_INCREASE_SAMPLE_MS - offset) % INPUT_LAG_INCREASE_SAMPLE_MS;
        if (input_lag_increase_wait_history[index] == 0) {
            input_lag_increase_wait_history[index] = 1;
            input_lag_time_spent_waiting += 1;
        }
    }
    if (input_lag_time_spent_waiting >= INPUT_LAG_INCREASE_WAIT_MS && local_input_lag_request < MAXIMUM_INPUT_LAG_TURNS && get_gameturn() >= input_lag_next_increase_turn) {
        local_input_lag_request += 1;
        input_lag_next_increase_turn = get_gameturn() + (int64_t)input_lag_adjustment_time * kfx_sim_state.turns_per_second / 1000;
        MULTIPLAYER_LOG("Input lag request increased after %" PRId64 "ms spent waiting: request=%" PRId64, (int64_t)(input_lag_time_spent_waiting), (int64_t)(local_input_lag_request));
        input_lag_reset_samples();
    }
}

void input_lag_observe_host_packet(const struct Packet *packet)
{
    int64_t target = packet->input_lag_turns;
    if ((uint64_t)target > MAXIMUM_INPUT_LAG_TURNS) {
        WARNLOG("Ignoring invalid input lag target %" PRId64, (int64_t)(target));
        return;
    }
    if (target == input_lag_target) {
        return;
    }
    input_lag_target = target;
    if (target > kfx_net_state.input_lag_turns) {
        input_lag_increase_turns = target - kfx_net_state.input_lag_turns;
    }
    MULTIPLAYER_LOG("Input lag target synchronized: current=%" PRId64 " target=%" PRId64, (int64_t)(kfx_net_state.input_lag_turns), (int64_t)(input_lag_target));
}

TbBool input_lag_needs_lookahead(void)
{
    return network_is_active() && (kfx_sim_state.operation_flags & GOF_Paused) == 0 && kfx_net_state.skip_initial_input_turns == 0 && input_lag_increase_turns == 0 && input_lag_target < kfx_net_state.input_lag_turns;
}

#ifdef __cplusplus
}
#endif
