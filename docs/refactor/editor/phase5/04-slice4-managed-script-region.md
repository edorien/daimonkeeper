# Phase 5, slice 4 — managed setup region + Level Settings' script-backed fields

Status: **done.**

## What this adds

The Level Settings dialog gains four script-backed fields — **Generation Speed**, **Start Gold**
(per player), **Max Creatures** (per player), and a **Creature Pool** list — which read from and
write into a delimited block inside the level's own script text (§4.2):

```
REM --- editor-managed setup: do not hand-edit between these markers ---
SET_GENERATE_SPEED(300)
START_MONEY(PLAYER0,20000)
START_MONEY(PLAYER1,2460000)
MAX_CREATURES(PLAYER0,25)
MAX_CREATURES(PLAYER1,30)
ADD_CREATURE_TO_POOL(TROLL,30)
REM --- end editor-managed setup ---
```

Everything else in the script — the mapmaker's own hand-written `IF`/`ENDIF` logic, comments,
whatever else — is untouched; only the text strictly between the two marker lines is ever
regenerated.

## Investigation

Before writing any code, confirmed the exact real-world syntax/semantics of all four commands
directly against `src/kfx_game/src/lvl_script_commands.c` and its processing functions (not
assumed from the design doc's own guess):

- **`SET_GENERATE_SPEED(speed)`** — global, no player param needed for this dialog's purposes
  (the command does support an optional second `PLAYERn` argument, but nothing in Level Settings
  needs a per-player generation speed).
- **`START_MONEY(PLAYERn,amount)`** and **`MAX_CREATURES(PLAYERn,amount)`** — genuinely one line
  per player; `get_players_range()` (`lvl_script.c`) only ever resolves a `P` token to a single
  player or `ALL_PLAYERS` (uniform value for every player), never an arbitrary sub-range. Real
  shipped scripts (confirmed against `core_files/campgns/keeporig/map00020.txt`, Skybird Trill)
  do exactly this — one `START_MONEY`/`MAX_CREATURES` line per player.
- **`ADD_CREATURE_TO_POOL(NAME,amount)`** — confirmed genuinely **global**, not per-player:
  `command_add_creature_to_pool()` hardcodes `ALL_PLAYERS` as its nominal range argument, but the
  actual sink, `add_creature_to_pool()` (`kfx_sim/src/room_entrance.c`), writes a single flat
  `kfx_sim_state.pool.crtr_kind[kind]` array with no player index at all — one shared bucket per
  creature kind for the whole level, drawn from by whichever player's dungeon gate pulls next.
- Creature names resolve via **`creature_desc[]`** (`config_creature.h`, dynamically populated at
  config-load time from `kfx_config_state.conf.crtr_conf`) through `get_rid()`/`creature_code_name()`
  — the same underlying model data the editor's own existing creature picker
  (`editor_toolbox.cpp`'s `draw_creature_picker()`) already iterates, just via numeric `ThingModel`
  ids there instead of name lookups.
- **No reusable generic script-line parser exists.** `script_scan_line()`/`get_next_token()`
  (`lvl_script.c`) are the real tokenizer, but calling them requires a live script-execution
  context and has immediate side effects for some commands (`START_MONEY`'s own processing calls
  `player_add_offmap_gold()` the instant the line is scanned — not something safe to trigger just
  to parse a static block of text). This slice hand-rolls a small parser/generator for just these
  four commands instead, working purely on plain strings.
- **`PLAYERS_COUNT` is 9** (`player_data.h`/`kfx_config_state.h`), with 7 real "keeper" player
  slots named `PLAYER0`-`PLAYER6` in sequence (the two non-keeper slots, `PLAYER_GOOD`/
  `PLAYER_NEUTRAL`, sit between `PLAYER3` and `PLAYER4` in the underlying enum's numeric *values*,
  but not in the per-player script *keyword* sequence) — so generating `"PLAYER" + index` for
  `index` in `[0, players)` is correct and needs no special-casing.

## Architecture

- **New module**: `src/kfx_editor/include/editor_script_managed.h` + `.cpp` — four functions,
  entirely plain-string logic, no live session state touched:
  - `editor_script_extract_managed_region()` / `editor_script_replace_managed_region()` — find the
    two marker lines and get/replace just the text between them; if the markers don't exist yet
    (a level that's never had this dialog's Apply pressed), replace inserts a fresh block at the
    very start of the script, ahead of everything else.
  - `editor_script_parse_managed_setup()` / `editor_script_generate_managed_setup()` — the
    four-command parser/generator, operating on a `ManagedSetupValues` struct (generation speed,
    per-player gold/max-creatures vectors, a flat creature-pool vector of `(ThingModel, amount)`
    pairs). Parsing is line-by-line prefix matching (`SET_GENERATE_SPEED(`, `START_MONEY(`, ...) —
    any other line (blank, a stray comment, something malformed) is silently skipped, not an
    error, matching the deliberately-lenient convention every other part of this editor's own
    save/load pipeline already uses for "content the editor doesn't recognize."
- **`editor_dialogs.cpp`**: Level Settings dialog gained the four fields, all buffered the same
  way every other field in this dialog already is (typing doesn't touch the real script text
  until Apply). `start_money`/`max_creatures` are **reactively resized every frame the dialog
  draws**, not just on open — raising or lowering the existing "Players" field immediately
  grows/shrinks the per-player rows below it, before Apply, preserving already-entered values for
  indices that still exist (`std::vector::resize`'s own behavior already does this). The creature
  pool is a scrollable list (each row: name, an editable count, a Remove button) plus a combo +
  "Add to Pool" button below it, using the same `[1, model_count)` + `creature_code_name()`
  iteration `draw_creature_picker()` already established.
- **Apply-time commit**: unlike the `.lof`/`.inf` fields in the same dialog (which write straight
  to disk), the managed-region write commits to **session state and marks the session dirty** —
  the same deferred-persist-until-the-next-real-Save shape the script text editor's own Apply
  button and every other content edit (painting terrain, placing things) already use. There's no
  separate "script settings file" to write independently; this is part of the same
  `map%05lu.txt` a plain Save already carries. Regenerated unconditionally on every Apply, even if
  nothing changed — cheap, and it also self-heals a managed region a stray hand-edit might have
  mangled, the same "the editor is the source of truth, don't skip regenerating just because
  nothing looked different" reasoning `write_slab_texture()` already established for `.slx`.

## Tests

- **Catch2** (`editor_script_managed_test.cpp`, new) — 10 test cases / 37 assertions, pure
  text-logic coverage requiring no live session: marker extraction/replacement (present, absent,
  and the "insert a fresh block" vs. "replace just the body, preserve everything else" cases),
  full generate→parse round-trips for generation speed/gold/max-creatures, missing-per-player-line
  defaults to 0 (a level whose player count grew since Level Settings was last Applied),
  blank-line/comment tolerance, the exact real-shipped-script line format, and — via a small
  `creature_desc[]`-snapshot-and-restore fixture (mirroring `editor_session_test.cpp`'s own
  `CampaignLevelsLocationFixture` pattern for a different global, since a bare Catch2 binary never
  loads real creature config) — a full creature-pool round-trip plus an unrecognized-creature-name
  line being silently skipped rather than corrupting the rest of the parse.
- No new ftest — the UI wiring itself is glue over the already-tested parser/generator plus
  already-tested session-state accessors (`editor_current_level_script_text()`/
  `editor_set_current_level_script_text()`, slice 1), matching the "UI-only glue doesn't need its
  own ftest" precedent established since phase 3.

## Verification

- `python3 scripts/check_layering.py --strict` — 0 violations (`editor_script_managed.cpp` stays
  in `kfx_editor`, its own includes — `config_creature.h`, `bflib_basics.h` — both already
  well below it in the ladder).
- `keeperfx`/`keeperfx_hvlog` (native Linux) — clean build.
- `kfx_editor_utest` — 90 assertions / 20 test cases, all passing (53 baseline + 37 new).
- Full ftest sweep (`-ftests -exitonfailedtest -headless`) — clean, 22/22.
- Manual live-test — pending; ask the user to open Level Settings on a real level with an existing
  script, confirm the parsed gold/max-creatures/pool values match what's actually in the script,
  change a couple of values plus add/remove a pool entry, Apply, Save, and confirm the script's
  managed region reflects the change while everything else in the script (the mapmaker's own
  hand-written logic) is untouched.
