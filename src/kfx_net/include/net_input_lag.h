#ifndef INPUT_LAG_H
#define INPUT_LAG_H

#include "globals.h"
#include "bflib_basics.h"

#define MAXIMUM_INPUT_LAG_TURNS 12
// Each turn records the worst packet delay; the largest few delays in a window are ignored so isolated hiccups don't change the input lag.
#define INPUT_LAG_IGNORED_SPIKES 2
#define INPUT_LAG_INCREASE_WINDOW_TURNS 20
#define INPUT_LAG_DECREASE_WINDOW_TURNS 300
// Waiting this long for a packet each turn is tolerated, in exchange for lower input lag.
#define INPUT_LAG_TOLERATED_WAIT_MS 35
// Extra headroom required before a decrease, so the input lag can't oscillate between two values.
#define INPUT_LAG_DECREASE_MARGIN_MS 10

#ifdef __cplusplus
extern "C" {
#endif

struct Packet;

/** For the network stats display: the synchronized target, the recent packet delay (ms, spikes ignored), the lag
 *  that delay raises the request to, the lag the long window would lower it to, and how many turns that window has. */
struct InputLagStats {
    int64_t target;
    int64_t packet_delay;
    int64_t increase_level;
    int64_t decrease_level;
    int64_t sample_count;
};

TbBool input_lag_skips_processing(void);
int64_t calculate_skip_input(void);
void input_lag_update(struct Packet *packet);
void input_lag_reset(void);
void input_lag_reset_request(int64_t input_lag_turns);
void input_lag_get_stats(struct InputLagStats *stats);
/** A peer's packet for `turn` arrived (the first copy of it): times it against when our own packet for that turn
 *  was sent (input_lag_update()), the per-turn packet delay the input lag adapts to. */
void input_lag_note_packet_arrival(GameTurn turn);
void input_lag_observe_host_packet(const struct Packet *packet);
TbBool input_lag_needs_lookahead(void);
/** Pins this process's input lag to `turns` (the startup lag, a re-negotiated one, and no adjustment by how late the
 *  peers' packets arrive: those depend on the machine's load and the network's timing), or lets it adapt again (-1).
 *  Every machine of a game must pin the same value. A multiplayer functional test does, so an input takes effect at
 *  the same turn every run and the game is the same every run. */
void input_lag_set_fixed(int64_t turns);
/** The pinned input lag, or -1 when it adapts. */
int64_t input_lag_fixed(void);

#ifdef __cplusplus
}
#endif

#endif
