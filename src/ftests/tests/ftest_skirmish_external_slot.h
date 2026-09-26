#pragma once

#include "globals.h"

#ifdef FUNCTESTING

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char TbBool;

/**
 * Phase M5 of docs/refactor/AI/LLM/01-integration-plan.md: the Skirmish "Slots & AI" page's External controller. Before
 * the level loads, the setup state marks slot 1 External and slot 2 a pinned built-in AI, and installs for Play exactly
 * as the page and Play do -- no net_add_external_seat() call from the test. The actions then check the live game: slot 1
 * is a packet-driven External seat (its packets dispatch, get_seats/view see it), slot 2 is still the built-in AI, the
 * human is untouched, and the pending list was consumed. Runs on original-pack multiplayer map 60 (human + two rivals).
 */
TbBool ftest_skirmish_external_slot_init();
void ftest_skirmish_external_slot_pre_start();

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
