#ifndef INPUT_LAG_H
#define INPUT_LAG_H

#include "globals.h"
#include "bflib_basics.h"

#define MAXIMUM_INPUT_LAG_TURNS 12
// The minimum amount of time needed to achieve a stable reading
#define INPUT_LAG_INCREASE_SAMPLE_MS 150
#define INPUT_LAG_INCREASE_WAIT_MS 75
// Host relay and 1v1 times are treated differently because the value in 4-player takes much longer to shift (requires all players to have the same value for a decrease).
#define INPUT_LAG_ADJUSTMENT_TIME_1V1_MS 60000
#define INPUT_LAG_ADJUSTMENT_TIME_HOST_RELAY_MS 15000
// Percentage of misses allowed before a decrease, to take noise into consideration
#define INPUT_LAG_DECREASE_WAIT_PERCENT 1

#ifdef __cplusplus
extern "C" {
#endif

struct Packet;

TbBool input_lag_skips_processing(void);
int64_t calculate_skip_input(void);
void input_lag_update(struct Packet *packet);
void input_lag_reset(void);
void input_lag_reset_request(int64_t input_lag_turns);
void input_lag_get_stats(int64_t *increase_wait_time, int64_t *increase_turn_time, int64_t *decrease_wait_time, int64_t *decrease_sample_time);
void input_lag_note_packet_wait(int64_t wait_time);
void input_lag_observe_host_packet(const struct Packet *packet);
TbBool input_lag_needs_lookahead(void);
/** Pins this process's input lag to `turns` (the startup lag, a re-negotiated one, and no adjustment by how long it
 *  waited for packets: those depend on the machine's load and the network's timing), or lets it adapt again (-1).
 *  Every machine of a game must pin the same value. A multiplayer functional test does, so an input takes effect at
 *  the same turn every run and the game is the same every run. */
void input_lag_set_fixed(int64_t turns);
/** The pinned input lag, or -1 when it adapts. */
int64_t input_lag_fixed(void);

#ifdef __cplusplus
}
#endif

#endif
