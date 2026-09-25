/**
 * @file ftest_packet_capture.h
 * @brief Records the local player's per-turn command packet during a
 * functional test, and asserts against it.
 *
 * KeeperFX multiplayer is lockstep: every client re-simulates the world
 * from the same per-turn input packets (packets_input.c). So the
 * determinism boundary for any GUI change is the `struct Packet` the GUI
 * produces -- if a migrated (ImGui) menu sets the same action/parameters
 * on the same game turn as the legacy sprite menu, every client stays in
 * sync. This harness captures that packet and provides golden-trace and
 * single-action assertions over it.
 *
 * Capture point: gameplay_loop_logic() calls ftest_packet_capture_tick()
 * once per game turn, immediately after exchange_packets() has finalized
 * the local packet and before update()/process_packets() clears it -- the
 * same packet that goes on the wire in a real MP game.
 *
 * A test action sets its packet from ftest_update(), which runs earlier
 * in the same gameplay_loop_logic() call than input()/exchange_packets().
 * front_input.c's per-view input paths gate on
 * "get_players_packet_action(player) != PckA_None" -- an already-queued
 * action is respected, not overwritten -- so the injected action still
 * stands at the capture point. If that ever stops holding, move the tick
 * call to right after ftest_update() instead.
 *
 * See docs/refactor/ingame-gui/06-tab-content-panels.md section 4 and
 * docs/refactor/ingame-gui/01-seam-and-toggle.md.
 */
#pragma once

#include "globals.h"

#ifdef FUNCTESTING

#include "packet_data.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Max distinct turns retained in one capture session. */
#define FTEST_PACKET_CAPTURE_MAX 512

/** "Don't care" sentinel for a parameter field in the assertions below. */
#define FTEST_PKT_ANY ((int64_t)0x80000000)

struct FtestCapturedPacket {
    GameTurn turn;
    struct Packet packet; /* sim_packets[my_player_number], post-input, pre-clear */
};

/**
 * The GUI-relevant projection of a packet, compared by
 * ftest_packet_trace_matches(). turn / checksum / input_lag_turns /
 * pos_x / pos_y and the raw mouse-button control bits are deliberately
 * NOT compared -- they are input-driver / net-layer noise, not GUI logic.
 */
struct FtestPacketExpectation {
    unsigned char action;          /* PckA_* */
    int64_t par1, par2, par3, par4; /* FTEST_PKT_ANY to ignore a field */
    TbBool require_gui_flag;       /* if true, PCtr_Gui must be set on the packet */
};

/** Discard any prior trace and start recording. */
void ftest_packet_capture_begin(void);
/** Stop recording; the captured trace stays readable. */
void ftest_packet_capture_end(void);
/** Discard the trace and stop recording. */
void ftest_packet_capture_reset(void);

/**
 * Per-game-turn hook, called from gameplay_loop_logic() just after
 * exchange_packets(). No-op unless recording. Records the local packet
 * only on turns where it carries a player action (action != PckA_None),
 * so a trace is exactly the sequence of actions the GUI produced and
 * stays comparable regardless of where the cursor rested.
 */
void ftest_packet_capture_tick(void);

int64_t ftest_packet_capture_count(void);
const struct FtestCapturedPacket *ftest_packet_capture_at(int64_t idx);
/** FTESTLOG one line per captured packet (also called automatically on assertion failure). */
void ftest_packet_capture_dump(void);

/* --- assertions: log + FTEST_FAIL_TEST on mismatch, return TbBool (true == ok) --- */

/** Exactly one captured packet carries `action` with matching (non-ANY) parameters. */
TbBool ftest_packet_expect_once(unsigned char action,
                                int64_t par1, int64_t par2, int64_t par3, int64_t par4);

/** No captured packet carries `action`. */
TbBool ftest_packet_expect_absent(unsigned char action);

/**
 * The captured action-bearing packets, in order, equal `expect[0..n)`.
 * Compares action + non-ANY parameters + (optionally) the PCtr_Gui bit.
 * The count of action-bearing packets must equal `n` exactly.
 */
TbBool ftest_packet_trace_matches(const struct FtestPacketExpectation *expect, int64_t n);

#ifdef __cplusplus
}
#endif

#endif /* FUNCTESTING */
