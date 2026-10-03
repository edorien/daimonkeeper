# S01 — Callback hygiene

**Status:** done 2026-09-28 (`96d6d3737`..`ee0840766`, docs in the following commit) · **Work items:** W1, W2, W14, W15 · **Depends on:**
— · **Risk:** low · **Layout change:** none · **Estimate:** 3–5 days

## Goal

Make the existing callback mechanism safe and honest *before* anything
starts moving:

- no dead or unnecessary entries;
- no positional wiring that can be silently swapped;
- no call that lands in a no-op without anyone noticing;
- no prototype declared in a library that doesn't own the definition.

There is no behaviour change and no layout change. Every later stage
removes entries, and this stage makes removal mechanical and safe.

## Scope

### A. Delete dead entries (9)

`SimFeedbackCallbacks.is_mouse_pressed_lrbutton`,
`RenderOverlayCallbacks.{set_packet_control, reinit_all_menus}`,
`GameCallbacks.{clear_all_messages, process_all_messages, display_objectives}`,
`DungeonAvailabilityCallbacks.try_set_backup_heart_idx`,
`RendererImGuiCallbacks.{want_capture_mouse, want_capture_keyboard}`.

For each one, remove:
- the struct member;
- the no-op default and its table slot;
- the `main.cpp` wrapper and its table slot;
- the line in the "every default is a no-op" test.

### B. Replace unnecessary entries with direct calls (5), and one duplicate

| Entry | Callers | Replacement |
| --- | --- | --- |
| `ConfigReloadCallbacks.thing_create_thing`, `thing_create_thing_adv` | `kfx_sim/src/lvl_filesdk1.c` (both are kfx_sim) | call `thing_create_thing()` directly; pass `&thing_create_thing_adv` directly |
| `RenderOverlayCallbacks.set_winfont` | 5 sites in kfx_render | direct call: `LbTextSetFont(winfont)` (kfx_platform function, kfx_render global) |
| `NetCallbacks.clear_player_lightning_palette` | `packets.c` | inline the two lines (implementation is kfx_sim + kfx_render) |
| `NetCallbacks.get_default_tag_mode` | `packets.c` | read `keeperfx_ui_config.default_tag_mode` (kfx_config) directly. Found during S01, once the inventory resolved globals read by wrappers. |
| `SimFeedbackCallbacks.play_sound_message_far_from_thing` | kfx_sim | same function as `output_message_far_from_thing` in the same table; keep one |

**Dropped during S01:** `RenderOverlayCallbacks.get_status_panel_width`
was listed here as implemented in kfx_config. Its wrapper also reads
`status_panel_width`, a kfx_frontend global (`frontend.h`), which the
inventory didn't see at the time. The callers below kfx_frontend still
need the entry. Moving `status_panel_width` down is an ownership move for
a later stage.

### C. Header/definition ownership (W14)

The ledger's S01 `function` and `prototype` rows list each item:

- **Move the 8 pure `struct Packet` helpers** (`set_/unset_packet_control`,
  `set_/unset_players_packet_control`, `set_players_packet_position`,
  `get_players_packet_action`, `is_packet_empty`,
  `packet_crtr_control_pressed`) from `kfx_net/src/packets*.c` to
  `kfx_sim/src/packet_data.c`, where their prototypes and `get_packet()`
  already live. This also deletes
  `SimFeedbackCallbacks.packet_crtr_control_pressed`.
- **Move prototypes up to kfx_net** (`packets.h`): `set_packet_pause_toggle`
  and `restore_users_from_packet_save`, whose definitions read
  kfx_net/player state.
- **Other prototypes:**
  - `thing_death_flesh_explosion`: `config_creature.h` → `thing_creature.h`;
  - `do_button_{click,press,release}_actions`: `bflib_guibtns.h` →
    `frontend.h`;
  - `LbBullfrogMain`: `bflib_main.h` → `src/kfxmain.h`. That was
    `bflib_main.h`'s only declaration, and `bflib_main.cpp` was empty, so
    both files are deleted.
- **Delete duplicate prototypes:** `slab_good_for_computer_dig_path` in
  `ariadne_wallhug.h` (the pathfinder never calls it), `is_door_buildable`
  and (found during S01) `is_trap_buildable` in `room_workshop.h`,
  `emulate_integer_overflow` in `game_merge.h`.
- **Optional:** add a `--decl-ownership` mode to `scripts/check_layering.py`
  that flags "prototype in library A, only definition in library B".
  `00-analysis.md` §3.8 has the prototype scan to base it on.

### D. Designated initializers everywhere (W2)

- **The 19 global tables:** every default table in
  `kfx_config/src/{sim_feedback,config,game_callbacks,net_callbacks,render_overlay,script_hooks,sprite_lookup,dungeon_availability,pathfinding_world,editor_callbacks,editor_journal_callbacks,content_tools_callbacks,matchmaking_config}.c`,
  the platform ones (`bflib_sndlib`, `bflib_video`, `custom_zip`,
  `bflib_inputctrl`, `RendererManager`), and every `*_impl` table in
  `src/main.cpp`.
  - The default tables are C11, so designated initializers are allowed in
    any order.
  - `main.cpp` is C++20, so designated initializers must follow
    declaration order. That requirement is a free order check.
- **Out of scope:** `ReceiveCallbacks`, which is per-session and passed per
  call, with some entries legitimately optional.
- **Do it with a script, not by hand.** Pair each positional slot with the
  struct member in the same position (`tools/callback_inventory.py` already
  does this pairing). Print any pair whose names don't match *before*
  rewriting: a mismatch would be a live miswire, and must be investigated,
  not frozen into a designated initializer. The 2026-09-27 run found no
  mismatches.

### E. Completeness check and wire-first startup (W2)

- **`kfx_config/include/port_check.h`:**
  ```c
  // every member of a callback table is a function pointer
  #define KFX_TABLE_SLOTS(T) (sizeof(T) / sizeof(void (*)(void)))
  TbBool kfx_table_complete(const void *table, size_t slots, const char *name); // logs each NULL slot
  ```
  Each table header adds
  `_Static_assert(sizeof(struct X) % sizeof(void (*)(void)) == 0, …)`.
- **`src/main.cpp`:**
  - a new `static void wire_ports(void)` holds every shape-A table `set_*`
    and every shape-B provider;
  - it is called as the first statement of `LbBullfrogMain()`, before
    `process_command_line`;
  - the `static const` tables move to file scope;
  - `set_renderer_draw_callbacks` (today wired after `RendererInit`) moves
    in too, since the table is static.
  - Shape-C value pushes (`bf_sprfnt_set_language_lwrstr`, …) stay where
    they are: they need loaded config.
- **`ports_verify_wired()`** runs at the end of `wire_ports()`. It calls
  `kfx_table_complete` on every installed table and `ERRORLOG`s any NULL
  slot. In `FUNCTESTING` builds it also fails `-exitonfailedtest`.
- **Loud defaults.** Each `noop_*` stub gets `KFX_UNWIRED("table.entry")`,
  a macro that logs once per entry at SYNCDBG level 1 (heavy-log build now;
  the "Debug" level after S02). In test binaries the macro is empty, so
  Catch2 suites keep using silent defaults.

**As built (2026-09-28).** Four differences from the plan above:
- `port_check.h`/`.c` live in **kfx_platform**, not kfx_config: five
  tables (`SoundState`, `VideoScale`, `MapZip`, `RendererDraw`,
  `RendererImGui`, plus `InputFocusPredicates`) are kfx_platform's own.
  It also provides `KFX_TABLE_COMPLETE(ptr, T)` and
  `KFX_ASSERT_PORT_TABLE(T)`.
- `wire_ports()` runs right **after** `LbErrorLogSetup()` (so its errors
  reach the log) and before `process_command_line()`. It also installs
  the five shape-B providers (`set_emulate_integer_overflow_provider`,
  `set_get_gameturn_provider`, `bf_sprfnt_set_font_role_resolver`,
  `set_config_network_is_active_check`, `set_power_grant_revoke_callbacks`).
- `ports_verify_wired()` checks main.cpp's own `*_impl` tables. The
  defaults are covered by Catch2 instead (`kfx_config/tests/port_tables_test.cpp`,
  `kfx_platform/tests/port_check_test.cpp`).
- `KFX_UNWIRED(table)` names the table, not the entry: many default
  tables share one stub between several entries (`noop_void`, ...). The
  log line carries the stub's name. Per-entry stubs come with S15's
  generated tables.

**The early-call risk, measured.** Before moving the wiring, a heavy-log
FUNCTESTING binary with `KFX_UNWIRED` in place and the old wiring order
ran a headless `-ftests` startup. Exactly three defaults were reached
before their table was wired: `MatchmakingConfigCallbacks.set_enabled`,
`set_server` and `get_ws_url`, all from `load_configuration()`'s
`MATCHMAKING_SERVER` case. So keeperfx.cfg's `MATCHMAKING_SERVER` was being
silently ignored (the log line read `Matchmaking server: ` with an empty
URL), the same bug class as the INGAME_RES one the old comment described.
Wiring first fixes it. The real setters only store strings, so calling
them that early is safe. After the move: no unwired calls, no incomplete
tables, and the first 400 log lines are identical apart from the
matchmaking line.

### F. Test helper

Add `ScopedPortOverride<T>` (RAII: copy the current table, let the test
override entries, restore on scope exit) in a shared test header next to
the existing fixtures (`src/kfx_config/tests/fixtures/`). Convert the
existing copy-override-restore sites (for example
`kfx_sim/tests/map_events_test.cpp`) opportunistically, not as a sweep.

### G. Documentation (W15)

- Refresh `architecture.md`:
  - §5.1: list all tables, with entry counts from the inventory;
  - §5.2: point to `wire_ports()`;
  - §7 and §13 rule 2: CMake is the only build, `linux.mk` is gone;
  - §2 file counts.
- Add the two move-ledger steps to `upstream-merge-workflow.md`
  ([`function-moves.md`](function-moves.md), last section).

## Steps (one commit each)

1. A and B: deletions and direct calls. The inventory goes from 571 to
   556 (done: 9 dead + 5 direct + 1 duplicate).
2. C: packet helpers and prototypes. `check_layering.py --strict` and
   `check_layering_symbols.py` stay clean.
3. D: the designated-initializer rewrite (script plus diff review).
4. E: `port_check.h`, `wire_ports()`, `ports_verify_wired()`,
   `KFX_UNWIRED`.
5. F: the test helper.
6. G: docs.

## Verification

- Build the Linux and mingw cross-builds, in both variants.
- Both layering checks pass.
- Catch2: all libraries.
- Full ftest list. Also run the game once and confirm `ports_verify_wired()`
  logs nothing and no `unwired port call` line appears in a normal session
  (menu → level → save → load → quit).
- Inventory: 0 dead entries, 0 "caller ≥ implementer" entries.
- `move_ledger.py detect <base>..HEAD` is clean, and the S01 rows are
  `done`.

## Result (2026-09-28)

- **Commits:** `96d6d3737` (D), `d49260efc` (A/B), `5c92ed3dd` (C),
  `a4f821314` (E), `ee0840766` (F), and the docs commit after them. D ran
  first: with designated initializers, the A/B/C deletions are one-line
  removals instead of position-sensitive edits.
- **Inventory:** 571 → 555 entries. There are 0 "caller ≥ implementer"
  entries. The only dead entries left are `ReceiveCallbacks.hostMsg`/`sysMsg`,
  and ReceiveCallbacks is out of scope.
- **Builds:** Linux and the mingw cross-build, both variants. Both
  layering checks pass. Catch2: 2026 tests pass (6 new).
- **Ftests** (headless, `out/run-editor` data):
  - Every short test passes: 56 in one sweep, the 14 after
    `harness_setup_failure` run one at a time, and `harness_setup_failure`
    through its own script.
  - `harness_setup_failure` sits in the short list and fails on purpose, so
    an `-exitonfailedtest` sweep always stops there.
  - The long-running two-process tests (`ai_bridge_*`, `net_enet_*`) were
    not run.
  - `out/coverage-ftest`'s staged data is missing the multiplayer map
    packs, so `ai_gesture_order_creature` hangs there. That's a data-staging
    problem, investigated separately.
- **Startup:** with a heavy-log build, no `unwired port call` line and no
  NULL-slot error appeared in a headless ftest run.
- **Not done:** the manual menu → level → save → load → quit session
  needs a real display. It's left to the maintainer.

## Risks

- **A hidden miswire found in step D.** That would be a real bug. Fix it in
  its own commit, with a test.
- **An early call through a table that depended on being a no-op.** For
  example, something called during `process_command_line` that used to hit
  a default and now reaches a real implementation before its subsystem is
  initialised. `wire_ports()` moving earlier makes this possible. The loud
  defaults can't catch it, since the call now goes through. So run
  `-ftests` and a manual startup with heavy logging, and inspect the first
  second of `keeperfx.log`. If one turns up, keep that single table's
  wiring where it was and document why.

## Upstream-merge notes

The callback mechanism and `main.cpp` wiring are fork-only, so there is no
upstream conflict surface. The packet helpers (C) move to `packet_data.c`,
while upstream edits them in `packets.c`/`packets_misc.c`. They are one-line
functions and rarely touched.
