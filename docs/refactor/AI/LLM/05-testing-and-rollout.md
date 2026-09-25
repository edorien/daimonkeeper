# Live LLM player-seat — testing, phasing, and open questions

Status: **investigation and plan, no code written.** Date: 2026-09-23. Final stage of
[`01-integration-plan.md`](01-integration-plan.md)'s sub-documents; read `02-transport-and-protocol.md`,
`03-observation-api.md`, `04-seat-and-action-api.md`, and `06-lifecycle-and-robustness.md` first — this
document sequences all four into a build order and closes with the questions only the owner can answer.

## 1. Testing strategy

### 1.1 The gap this starts from

`01-integration-plan.md` §2.8 (from a dedicated investigation) found something worth restating plainly:
**zero existing tests exercise any continuous gameplay gesture** (build room, dig-mark, sell, slap,
trap/door placement, power cast, hand pick/drop) through the real `PCtr_LBtnClick/Held/Release` +
`pos_x/pos_y` path across turns. `ftest_packet_capture.h` compares discrete `PckA_SetPlyrState` selections
and deliberately excludes position/button-bit fields from its comparisons; the editor's
`PckA_EditorPlaceTerrainRect`-style tests use a different, simpler one-shot-rectangle protocol built
specifically to avoid the ambient-drag mechanism. `04-seat-and-action-api.md` §2's catalog is therefore a
reading of production code, not a validated trace.

This means the very first piece of new test infrastructure this feature needs isn't a test *of the new
code* — it's a test that **the existing catalog reading is correct**, before any gesture-sequencing code
is built on top of it. Get this backwards (build the sequencing layer first, assume the catalog is right,
discover mismatches later) and a wrong catalog entry becomes a silent agent-side failure mode instead of a
caught test failure.

### 1.2 Phase-by-phase test plan

1. **Ground-truth ftests for the gesture catalog** (before `04-seat-and-action-api.md` §3's sequencing
   layer is built): for each verb in that document's §2, an `src/ftests/` test that drives a **real human-
   shaped packet sequence** via `set_players_packet_control()`/`set_players_packet_position()`/
   `set_players_packet_action()` across the turns the catalog describes, and asserts the expected in-sim
   effect (a room gets built, slabs get tagged and dug, a trap appears, gold changes hands). This is
   genuinely new ftest coverage, not a duplicate of anything existing (§1.1) — treat it as validating
   `04-seat-and-action-api.md` §2 itself, not as testing a feature that exists yet.
2. **`get_net_user_player_number()` fix regression test** (`04-seat-and-action-api.md` §1): a focused
   Catch2/ftest asserting a second local `NetUserId` now correctly maps to a non-local `PlayerNumber`,
   that `process_packets()` actually dispatches that slot's packet, and that nothing in the networked path
   (`net_resync.cpp`, `net_checksums.c`, `packets_misc.c` — all consumers of this function, per that
   document's §1.2) regresses. This is the single highest-risk code change in the whole feature (existing
   dispatch logic, several dependents) and deserves isolated verification before anything else is built
   on top of it.
3. **Observation API tests** (`03-observation-api.md`): an in-process fixture (no real transport needed,
   following the `ftest_net_fake`-style precedent `01-integration-plan.md` §2.6 already established as
   this codebase's pattern for "test the machinery without a real socket") that reveals part of a map for
   one player, builds `get_player_view` snapshots for two different `plyr_idx` values, and asserts the
   difference matches the filtering rule in `03-observation-api.md` §3 exactly — including the trap/door
   per-thing `revealed` bit case, not just tile reveal.
4. **Gesture-sequencing layer tests**: once step 1's ground truth exists and step 2's identity fix lands,
   an ftest that submits each `04-seat-and-action-api.md` §2 gesture request through the new sequencing
   layer and asserts (via `ftest_packet_capture`, already built for exactly this kind of assertion) that
   the resulting per-turn packet trace matches step 1's validated ground truth trace for the same gesture.
   This is where a wrong catalog reading (had step 1 not caught it) or a sequencing bug would surface.
5. **End-to-end smoke test**: once a transport exists (`02-transport-and-protocol.md`), a minimal external
   script (outside the `keeperfx` binary — Python or similar) that connects, calls `get_player_view`,
   calls `submit_action` for one simple verb (e.g. `slap` or `place_trap` — pick a single-turn verb first,
   not a multi-turn drag, to keep the first end-to-end test simple), and asserts the sim state changed as
   expected. This is the first test that actually exercises the "external process" property the whole
   feature is about; don't assume the in-process tests above substitute for it.
6. **Save/load regression for the seat-identity fix** (`06-lifecycle-and-robustness.md` §1.3, explicitly
   called out there as not deferrable): extend step 2's `get_net_user_player_number()` regression test
   with a save → load → assert-still-dispatchable cycle, not just a fresh-game case. This is the test that
   would have caught `01-integration-plan.md` §2.9's finding if it had existed before that finding was made
   by manual investigation instead.
7. **Disconnect/fallback test** (`06-lifecycle-and-robustness.md` §2): once a fallback policy is chosen
   (§3, open question 8 below), a test that closes the API client connection while the seat's turn is
   paused-and-waiting, and asserts the chosen fallback behaviour (timeout → no-op-and-unpause, or
   timeout → AI conversion) actually happens rather than the game staying paused forever. If Option B (AI
   conversion) is chosen, this also needs `06-lifecycle-and-robustness.md` §2.3's own targeted test
   (does `script_support_setup_player_as_computer_keeper()` alone, without
   `init_creature_states_for_player()`, leave in-progress creature jobs intact on a live dungeon) as a
   prerequisite, independent of the disconnect scenario itself.

## 2. Phasing

| Phase | Work | Depends on | Doc |
|---|---|---|---|
| T0 | Ground-truth ftests for the gesture catalog (§1.2 step 1) | — (validates existing-code reading only) | this doc §1 |
| S0 | Decide `02-transport-and-protocol.md` §1's transport question and §4's sandboxing requirement | — | 02 |
| E1 | `get_net_user_player_number()` fix + its regression test (§1.2 step 2) | T0 not required, but should land before E2/E3 rely on the seat existing | 04 §1 |
| E2 | Save/load identity fixup (`06-lifecycle-and-robustness.md` §1) + save/load regression test (§1.2 step 6) | E1 — treat as the same unit of work with two call sites, per `06` §1.3 | 06 §1 |
| O1 | Observation API: snapshot builder, fixed/bypassed `revealed` binding, filtering layer | S0 (response envelope shape) | 03 |
| O2 | Observation API tests (§1.2 step 3) | O1 | this doc §1 |
| O3 | Victory-state exposure (`06-lifecycle-and-robustness.md` §3) — new `SVar_*` or equivalent, surfaced in `get_player_view` | O1 | 06 §3 |
| A1 | Gesture-sequencing layer, built against T0's validated catalog | T0, E1, S0 | 04 §2–§3 |
| A2 | Gesture-sequencing tests (§1.2 step 4) | A1, T0 | this doc §1 |
| R1 | Disconnect/stuck-pause fallback (`06-lifecycle-and-robustness.md` §2) — timeout policy, chosen fallback option, its own safety test if Option B | E1, S0 | 06 §2 |
| R2 | Disconnect/fallback test (§1.2 step 7) | R1 | this doc §1 |
| P1 | Wire O1/A1 into the chosen transport; pause-on-think submission flow | O1, A1, S0 | 02 §3–§4 |
| P2 | End-to-end smoke test (§1.2 step 5) | P1, E2, O3, R1 | this doc §1 |
| F1 (future) | Revisit networked agent seats and the queued-actions latency model if v1 proves the concept | P2 | 01 §5.2–§5.3 |

Notable reordering versus the original single-document plan: **T0 and E1 now come before the observation
and action work they support**, rather than being folded into "testing" as a final phase — both follow
directly from the two investigations that produced this split (§2.7 and §2.8 of `01-integration-plan.md`).
**E2, O3, and R1/R2 are a second wave of the same pattern**: two more follow-up investigations
(§2.9–§2.10 of `01-integration-plan.md`) found that identity, victory-state, and disconnect handling are
also prerequisites rather than final-phase polish — each is now a numbered phase with its own dependency
edges into the end-to-end smoke test (P2), rather than an afterthought discovered post-launch.

## 3. Open questions for the owner

1. **Transport** (`02-transport-and-protocol.md` §1): extend `api.c`, or a dedicated channel? The
   recommendation there is to extend `api.c`; confirm before S0/P1 start.
2. **`get_net_user_player_number()` fix scope** (`04-seat-and-action-api.md` §1.2, new question raised by
   the seat-identity investigation): should the fix be scoped narrowly (only ever populated for local
   External seats, v1's actual need) or generally (also correctly usable for a future networked seat,
   `01-integration-plan.md` §1's stated non-goal for now)? The fix itself is nearly the same size either
   way (§1.2's sketch already generalizes it), but a narrow framing might justify skipping the "does the
   networked path still work" half of E1's regression test — not recommended, since this function is a
   shared dependency, but worth an explicit decision rather than assuming.
3. **How is an `External` seat configured for a skirmish game?** The Slots & AI page
   (`docs/refactor/skirmish/01-scope-expansion-research-winlose-ai.md` §5.2/§6) already reserves the
   `External{ agent_id, params }` variant in the controller model but it's unimplemented. Recommend
   landing the engine-side seat (E1, O1, A1) first, testable via ftest/the transport's own client without
   any UI, and wiring the Slots & AI page's `External` option to it afterward as a separate, smaller task.
4. **Pause-on-think acceptability** (`01-integration-plan.md` §5.3): is a visibly-pausing game acceptable
   for v1, including in a session with a human opponent watching it happen? If not, the queued-actions
   alternative needs to move earlier than F1.
5. **How much of the Lua API's existing surface should the action set mirror vs. deliberately restrict?**
   `01-integration-plan.md` §5.4 argues for a scoped action set distinct from raw Lua; where exactly that
   line sits (e.g. is a `ChangeSlabOwner`-equivalent power ever appropriate for an agent seat, or only
   build/dig/sell/cast/creature-management actions a human could take through normal play) is a product
   decision, not an engineering one.
6. **Turn-advance push notification** (`02-transport-and-protocol.md` §5): worth building
   `subscribe_event`-based "your turn" push so a bridge doesn't need to poll `get_current_game_info` in a
   loop while paused-waiting? Not required for v1 (the bridge already controls pause/unpause itself), but
   cheap to add alongside the rest of the transport work if the owner wants it from the start.
7. **Observation API scoping** (`03-observation-api.md` §6, item 1): fix `lua_api_slabs.c`'s `"revealed"`
   binding for everyone, or add a separate non-Lua-exposed reveal query used only by the snapshot builder?
   Recommend the latter (lower risk to existing scripts); confirm before O1 starts.
8. **Disconnect/stuck-pause fallback policy** (`06-lifecycle-and-robustness.md` §2.2): Option A (timeout →
   no-op → unpause), Option B (timeout → convert to built-in AI, pending §2.3's safety check), or Option C
   (timeout → surface a notification, no automatic state change)? Recommend A as the v1 default, with B
   available for unattended/automated sessions once §2.3 is resolved. Also needs a concrete timeout value
   (§2.4) — not `FINAL_RESORT_RESYNC_RECOVERY`'s 10 seconds, which was sized for network jitter, not
   thinking time.
9. **Victory-state exposure mechanism** (`06-lifecycle-and-robustness.md` §3.2): a new `SVar_*` reachable
   through the existing `read_var`/Lua fallthrough (recommended, consistent with how everything else in
   this family works), or a dedicated field bypassing that machinery? Confirm before O3 starts.
