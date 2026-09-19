# Phase 5, slice 5 — availability helper grid

Status: **done** (live-test pending).

## What this adds

**Script > Availability...** opens a resizable window with one tab each for Creatures, Rooms,
Spells, Traps and Doors. Each tab is a table: one row per kind, columns **ALL** plus one per
player (sized to the level's Players field). Clicking a cell cycles `-` (unset) → `Avail` →
`Rsrch` → `Off` → `-` (Creatures/Traps/Doors skip `Rsrch`). Apply writes the result into the
managed setup region (§4.2, slice 4) as `CREATURE_AVAILABLE` / `ROOM_AVAILABLE` /
`MAGIC_AVAILABLE` / `TRAP_AVAILABLE` / `DOOR_AVAILABLE` lines; like every other content edit it
commits to session state and marks the session dirty, persisted by the next real Save.

## Investigation (against the engine source, not the design doc's guess)

Parameters are `(player, kind, n1, n2)`; the two numbers mean different things per command
(`lvl_script_value.c`, `dungeon_data.c`, `config_magic.c`):

| Kind | Off | Available | Researchable | Notes |
|---|---|---|---|---|
| Room | `0,0` | `1,1` | `1,0` | `n1` = research value (only tested against 0), `n2` = buildable now; `n1=0` always switches it off |
| Magic | `0,0` | `1,1` | `1,0` | same shape; `n2` adds 1 to `magic_level` |
| Creature | `0,0` | `1,0` | — | `n1` = allowed, `n2` = *count* of forced-in creatures (not a boolean) |
| Trap / Door | `0,0` | `1,0` | — | `n1` = manufacturable, `n2` = starting stock, **added** (`+=`) not assigned; research is done via `RESEARCH`/`RESEARCH_ORDER`, not these commands |

- `ALL_PLAYERS` is valid for all five (271 of 355 CREATURE/MAGIC lines in shipped keeporig
  scripts use it); later lines overwrite earlier ones for research/allowed flags, but trap/door
  stock and magic level accumulate — so the generator emits **exactly one line per
  (kind, player, item)**, `ALL_PLAYERS` lines first and per-player lines after, so a `PLAYERn`
  line overrides the baseline exactly as it would in execution order.
- Name tables are the dynamically-filled `creature_desc`/`room_desc`/`power_desc`/`trap_desc`/
  `door_desc` (NULL-terminated `NamedCommand[]`); the grid iterates them directly (skipping
  `num <= 0` and alias names that repeat an id already listed) rather than going through model
  counts.

## Design

- **Cell model**: an `AvailabilityEntry {kind, player (-1 = ALL), item, a, b}` keeps the raw
  `(a, b)` pair. The grid derives a display state from it but only rewrites a pair when the user
  clicks *that* cell — so values the grid has no UI for (a creature force count, a trap stock
  amount, a research value of 3 or 4 as shipped maps use) round-trip losslessly instead of being
  collapsed to a canonical pair. **Unset** (no entry, no line) is a real fourth state: it means
  "leave the script/engine default", which is what you want for any kind you haven't touched.
- **`ManagedSetupValues` grew an `availability` vector** (`editor_script_managed.*`), parsed and
  generated alongside the slice-4 fields; last-write-wins on parse matches execution order.
- **Both editors preserve each other's lines.** Level Settings' Apply used to regenerate the
  block from only its own four fields — it now parses the existing block first and overwrites just
  those, so availability lines survive it; the availability window's Apply does the reverse.
- **Fix to slice 4 found while doing this:** the generator emitted `SET_GENERATE_SPEED(0)` /
  `START_MONEY(PLAYERn,0)` / `MAX_CREATURES(PLAYERn,0)` for a level with no managed block yet
  (Level Settings opens with all zeros), which would silently override the engine's own defaults
  for any level whose script relied on them. Zero now means "not set" and those lines are simply
  omitted. The (small) cost: you can no longer explicitly set gold or max-creatures to exactly 0
  through this dialog.

## Known limits

- The grid doesn't filter kinds that make no sense to grant (dungeon heart/portal rooms, hero-only
  creatures) — the engine doesn't either; not verified which powers can't be granted.
- Trap/door **stock amounts** and creature **force counts** have no UI (preserved if present, never
  created). Hand-edit the script for those.
- If a hand-written `*_AVAILABLE` line elsewhere in the script targets the same kind, it executes
  after the block (which sits at the top) and wins — by design, but worth knowing when the grid
  seems to have "no effect".

## Tests

- **Catch2** (`editor_script_managed_test.cpp`): parsing all five kinds with raw pairs preserved
  (trap amount 3 survives), generate→parse round-trip with `ALL_PLAYERS`-before-per-player
  ordering, last-line-wins plus unknown-name and `PLAYER_GOOD` lines skipped, and the
  zero-omission generator fix. A fixture temporarily fills the first entries of the five name
  tables (bare test binaries never load config) and restores them. `kfx_editor_utest`: 104
  assertions / 24 test cases (was 90 / 20).
- No new ftest — the window is UI glue over the tested parser/generator and the slice 1/4
  script-text accessors.

## Verification

See the commit message for the final numbers; manual live-test pending: open Script >
Availability on a real level, set a few cells, Apply, open the script editor and confirm the
managed block's lines, then reopen Level Settings, Apply, and confirm the availability lines
survived.
