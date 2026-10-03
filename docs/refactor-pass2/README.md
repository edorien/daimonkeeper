# Refactor pass 2

Pass 1 (`docs/refactor/`, summarised in
[`architecture.md`](../Architecture/architecture.md)) built a strict,
CI-enforced library ladder. Pass 2 puts code and state on the right side of
those lines, shrinks the callback surface that crosses them, and makes what
remains safe and cheap to maintain.

| Document | Contents |
| --- | --- |
| [`00-analysis.md`](00-analysis.md) | The analysis: what the callback mechanism costs, why most entries exist, the target design, evaluated alternatives, investigated design questions |
| `stage-NN-*.md` (below) | One plan per stage: scope, move list, steps, verification, risks |
| [`function-moves.md`](function-moves.md) + [`function-moves.tsv`](function-moves.tsv) | The move ledger: every function, global, type and callback entry a stage moves or deletes, for the upstream-merge workflow |
| [`tools/callback_inventory.py`](tools/callback_inventory.py) | Counts callback entries, dead ones, duplicates, unnecessary ones |
| [`tools/move_ledger.py`](tools/move_ledger.py) | `check` / `lookup` / `upstream` / `detect` for the move ledger |

## Baseline

- Pass 2 starts from the tree **after the spectator hand-off work landed**
  (`2906dce70`; the plan itself is `576e0e35d`). Stage 7 builds on it.
- The stage documents refer to code by **symbol**, not line number. Line
  numbers are given only as a hint, as of `216be51d5`.
- At the start of each stage:
  - rerun `python3 docs/refactor-pass2/tools/callback_inventory.py`;
  - rerun `python3 docs/refactor-pass2/tools/move_ledger.py check`;
  - update the stage document if the tree has moved on.

## Stages

| # | Stage | Work items | Depends on | Risk | Save/resync layout change? |
| --- | --- | --- | --- | --- | --- |
| S01 | [Callback hygiene](stage-01-callback-hygiene.md) — **done** 2026-09-28 | W1, W2, W14, W15 | — | low | no |
| S02 | [Logging option](stage-02-logging-option.md) — **done** 2026-09-28 | W18 | — | low–medium | no |
| S03 | [Ownership quick moves](stage-03-ownership-quick-moves.md) — **done** 2026-09-28 | W20, W7, W8, input primitives | S01 | low | no |
| S04 | [`kfx_content` library](stage-04-content-library.md) — **done** 2026-09-28 | W16 | — | very low | no |
| S05 | [Config gameplay → sim](stage-05-config-gameplay-to-sim.md) — **done** 2026-09-28 | W5, W6 | S01 | low | no |
| S06 | [Presentation out of sim](stage-06-presentation-out-of-sim.md) — **done** 2026-09-28 | W4 | S03 | low–medium | no |
| S07 | [Synced camera → sim](stage-07-camera-to-sim.md) — **done** 2026-09-28 | W19 | S03, S06 | medium | no (the view-shake field move is deferred to S10) |
| S08 | [Header-only `kfx_model`](stage-08-kfx-model-headers.md) — **done** 2026-09-28 | W10 | S03; Ariadne playtest | medium | no |
| S09 | [Versioned state chunks](stage-09-versioned-state-chunks.md) — **done** 2026-09-28 | (§9) | — | low–medium | introduces the mechanism (clean refusal, no migrations) |
| S10 | [Session state down](stage-10-session-state-down.md) — **done** 2026-09-28 | W3 | S09 | medium | **yes** |
| S11 | [Lighting split](stage-11-lighting-split.md) — **done** 2026-09-28 | W9 | S09 | medium–high | **yes** |
| S12 | [kfx_net split](stage-12-net-split.md) — **done** 2026-09-28 | W11 | S07 | medium–high | no |
| S13 | [Frame composition up](stage-13-frame-composition.md) — **done** 2026-09-28 | W12 | S06 | medium | no |
| S14 | [`kfx_ai` spike](stage-14-ai-library-spike.md) — **done** 2026-09-28 | W17 | S03 | medium (spike first) | maybe |
| S15 | [Ports and events](stage-15-ports-and-events.md) — **done** 2026-09-28 | W13 | everything above that is done | medium | no |

```
S01 ──► S03 ──► S06 ──► S07 ──► S12
  │       │       │
  │       │       └──► S13
  │       ├──► S08  (+ Ariadne playtest)
  │       └──► S14 (spike)
  └──► S05
S02, S04        independent, any time
S09 ──► S10, S11
all ──► S15
```

**Suggested order.**

1. **S01**, then S02 and S04 in parallel. These are cheap and give an
   immediate safety net: every table checked for completeness, loud unwired
   calls.
2. **S03 → S05 → S06 → S07.** This is the bulk of the "move code to its
   owner" work, about 180 callback entries.
3. **S09**, then S10 and S11 (the save-layout changes, done together if
   possible).
4. **S08, S12, S13, S14** as capacity allows.
5. **S15** last. It regroups whatever is left, so doing it earlier would
   regroup entries that are about to disappear.

## Decisions

| Date | Decision | Affects |
| --- | --- | --- |
| 2026-09-27 | Pass 2 starts after the spectator hand-off work lands. | baseline, S07 |
| 2026-09-27 | Logging level **Off** writes nothing except crash reports. With logging off, the fatal-error dialog says that logging is off instead of pointing at `keeperfx.log`. | S02 |
| 2026-09-27 | The default logging level is named **Normal** (not "Errors"). It is today's standard log. | S02 |
| 2026-09-27 | **Breaking old saves is acceptable.** Layout changes bump the chunk version, and older saves are refused cleanly. No migrations. | S09, S10, S11 |
| 2026-09-28 | **No event dispatcher (S15).** `config_changed` is not built: kfx_config's per-field assign hooks already pick each reaction, so the push calls stay named port entries. The local player's view transitions go through one port entry, `UiPort.local_view_transition`, because they have a single subscriber (kfx_frontend). A dispatcher is worth adding only when a second subscriber needs the same event. | S15 |
| 2026-09-28 | **Save layout version 3 for sim and game state (S11).** The lights moved from kfx_render's unsaved `lish` into `kfx_sim_state` (so saves now carry them, fixing loads that kept the previous level's lights), and `kfx_game_state` lost `lightst`. Saves and continue-replays from before S11 **can't be loaded**, and resync no longer sends the 4.6 MB shading blob. Release note: "Saved games from earlier versions can't be loaded." | S11 |
| 2026-09-28 | **Save layout version 2 for sim, net and game state (S10).** Session values moved between the state structs (`play_gameturn`, `local_plyr_idx`, `human_players_count`, `packet_load_enable`) and the camera shake left `struct Dungeon`, so saves and continue-replays from before S10 **can't be loaded**. Release note: "Saved games from earlier versions can't be loaded." | S10 |
| 2026-09-28 | **Save layout version 1 (S09).** Every state chunk is now versioned (`state_versions.h`); saves and continue-replays from before S09 carry version 0 and **can't be loaded**. Release note: "Saved games from earlier versions can't be loaded." | S09 |
| 2026-09-28 | **No `keeperfx_hvlog` copy.** The external launcher is mostly obsolete (its options moved into the in-game options menu), so the transition copy and the name check that ran it at Debug were dropped straight away. | S02 |

**Save-compatibility breaks** (one line per version bump, for release
notes; the decisions above have the detail):

- S09: every state chunk versioned; older saves can't be loaded.
- S10: sim, net and game state version 2; older saves can't be loaded.
- S11: sim and game state version 3; older saves can't be loaded.

## The standard procedure for every stage

1. **Timing against upstream merges.** Stages that move many functions in
   files upstream edits often (S05, S06, S07, S11, S12) should start **right
   after** an upstream merge
   ([`upstream-merge-workflow.md`](../Architecture/upstream-merge-workflow.md))
   and land before the next one.
2. **Before:**
   - run `callback_inventory.py` and `move_ledger.py check`;
   - confirm the stage's ftests pass on the baseline;
   - add any missing ftest the stage asks for, as its own commit on the
     baseline, the same way the merge workflow's step 4 does.
3. **Do the stage** in small commits: one commit per move group, each one
   building.
4. **After each commit:**
   - the build passes (`KFX_OS=linux ./build-cmake.sh`, and the Windows
     cross-build for anything touching platform code);
   - `python3 scripts/check_layering.py --strict` passes;
   - `python3 scripts/check_layering_symbols.py --strict` passes (needs a
     build tree).
5. **Before merging the stage:**
   - Catch2 suites (`KFX_BUILD_TESTS=ON`);
   - the full ftest list (`-ftests -exitonfailedtest`);
   - the stage's own manual checks.
6. **Ledger:**
   - flip the stage's rows in `function-moves.tsv` to `done`, with the
     commit;
   - add rows for anything moved that wasn't planned;
   - `move_ledger.py detect <stage-base>..HEAD` must show no
     "not in ledger" lines;
   - `move_ledger.py check` must be clean.
7. **Docs:**
   - update `architecture.md` (callback tables in §5.1, library contents in
     §2);
   - update this README's stage table;
   - update the stage document's status line.

## Definition of done (every stage)

- No new entries in `ACCEPTED_VIOLATIONS` / `ACCEPTED_SYMBOL_VIOLATIONS`.
- The callback-entry count went down by roughly the stage's estimate, or the
  stage document explains why not.
- The build passes (one variant since S02). All tests pass.
- The ledger is current, and `detect` over the stage's commits is clean.
