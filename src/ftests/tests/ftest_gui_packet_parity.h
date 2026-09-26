/**
 * @file ftest_gui_packet_parity.h
 * @brief Golden per-turn command-packet traces for in-game GUI actions.
 *
 * KeeperFX multiplayer is lockstep (every client re-simulates from the
 * same per-turn packets), so the determinism boundary for the in-game
 * GUI ImGui migration (docs/refactor/ingame-gui/) is: does a GUI click
 * produce the same struct Packet on the same turn as before.
 *
 * These tests pin the *current* (legacy sprite) GUI's packet output for a
 * handful of menu actions. When a menu is migrated to ImGui, the same
 * test must still pass -- run it against the ImGui path (flip
 * GUI_POSITION to CLASSIC to switch a test back to the legacy in-game
 * HUD, ingame_gui_use_classic_hud()) and assert the trace is
 * byte-identical.
 */
#pragma once

#include "globals.h"

#ifdef FUNCTESTING

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char TbBool;

TbBool ftest_gui_packet_parity_init();

#ifdef __cplusplus
}
#endif

#endif /* FUNCTESTING */
