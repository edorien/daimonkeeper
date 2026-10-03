// kfx_net: net_input_lag.c. Almost everything in this file mixes
// module-private static state (input_lag_target/input_lag_increase_turns/
// etc, none of it exposed through any accessor) with real wall-clock
// reads (LbTimerClock()) and live network-session state (netstate.*,
// GetRemoteUserCount()), so most of it isn't attempted here. The one
// safe, deterministic slice: input_lag_skips_processing() and
// input_lag_needs_lookahead() both short-circuit on network_is_active()
// (a kfx_sim static inline reading local_system_flags'
// GSF_NetworkActive bit, fully controllable) before touching any of
// that state, so their "network session not active" early-return path
// is pure pattern A.
#include <catch2/catch_test_macros.hpp>

#include "net_input_lag.h"
#include "kfx_sim_state.h"

#include <cstring>

namespace {
struct ResetSimState {
    ResetSimState() { std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state)); }
};
}

TEST_CASE_METHOD(ResetSimState, "input_lag_skips_processing is false when no network session is active", "[kfx_net][net_input_lag]") {
    // local_system_flags left at 0: GSF_NetworkActive unset.
    CHECK_FALSE(input_lag_skips_processing());
}

TEST_CASE_METHOD(ResetSimState, "input_lag_needs_lookahead is false when no network session is active", "[kfx_net][net_input_lag]") {
    CHECK_FALSE(input_lag_needs_lookahead());
}
