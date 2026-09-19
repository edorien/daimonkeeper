# Phase 2 (companion) — definable keybindings (D6)

Status: **first slice landed (own storage), most of D6 still open.** Companion to
[`09-toolbox-remainder.md`](09-toolbox-remainder.md)'s own "Definable keybindings" backlog entry —
written because that item turned out to need real subsystem plumbing, not a quick binding swap,
and the plumbing itself surfaced a few pre-existing gaps worth recording before they're
forgotten.

The point of doing a slice at all, rather than deferring D6 wholesale: exercising the
keybinding/Define-Keys-menu path end-to-end for the *first* newly-added editor binding was
expected to turn up latent bugs in that path — it did, three of them (§2 below).

**Revised after the first pass**: editor keybindings got their own storage/enum/Define-Keys tab
(`EditorGameKeys`/`editor_key_settings[]`/`settings.editor_kbkeys[]`), not entries folded into the
shared `GameKeys`/`game_key_settings[]`/`settings.kbkeys[]` table the first version of this slice
used. User's call: most editor functions have no gameplay counterpart to collide with, so sharing
one flat ~90-entry table just eats into that shared key space for no benefit — separate storage
means both sides get more room to work with. §1 describes the current (revised) shape; §2's
findings were made against the shared-table version but still apply (the underlying subsystem
gaps are the same either way).

---

## 1. What's implemented

One new definable key, `Gkey_EditorEraseTool` (default `R`, no modifier): while an editor session
is active, pressing it switches straight to the Erase tool, same as clicking its tool-strip
button. Deliberately not a rebind of anything hardcoded — it's a **new** capability (no keyboard
path to the Erase tool existed before this).

**Camera/console keys duplicated in, not shared with `GameKeys`.** Found live, right after the
separate-storage revision above: the editor session still read camera movement/rotation/zoom/tilt
and the console toggle straight from `settings.kbkeys[]`/`GameKeys` via the normal gameplay
input path (`get_isometric_view_nonaction_inputs()`, `front_input.c` — the same function that
runs during normal play, since an editor session reuses the identical isometric dungeon view).
Having `Gkey_EditorEraseTool` in its own table bought nothing if every *other* key an editor
session actually uses still came from the shared one — "two independent paths" only pays off if
editor code actually reads from the editor path. Fixed by adding 14 more `EditorGameKeys` entries
(`Gkey_EditorMoveUp/Down/Left/Right`, `Gkey_EditorRotateMod`, `Gkey_EditorSpeedMod`,
`Gkey_EditorRotateCW/CCW`, `Gkey_EditorZoomIn/Out`, `Gkey_EditorTiltUp/Down/Reset`,
`Gkey_EditorToggleConsole` — 15 total with the erase tool) with defaults copied verbatim from
their `game_key_settings[]` counterparts (duplicating a default key across two independent tables
causes no collision — that's the point), and reusing the *same* `GUIStr_*` label as the gameplay
counterpart for each (unlike the erase tool, "Move Up"/"Rotate CW"/etc. mean the same thing in
both tables, so there's a real localized string already, not a label-less new concept).

New downward callback needed for this: `EditorCallbacks::is_active` (`editor_callbacks.h`,
kfx_config) — `front_input.c` (kfx_frontend) needs "is an editor session currently active" to
decide which table to read, but can't call `editor_is_active()` (`kfx_editor.h`) directly since
kfx_editor is ranked above kfx_frontend too. Mirrors `editor_callbacks.h`'s existing
`request_open` field/no-op-until-wired convention exactly.

`get_isometric_view_nonaction_inputs()`'s dozen-odd `is_game_key_pressed(Gkey_X, ...)` calls now
go through a new small dispatch pair instead of branching inline at every call site:
`is_dual_context_key_pressed(game_key_id, editor_key_id, clear_pressed, ignore_mods)`/
`get_dual_context_key_axis_value(game_key_id, editor_key_id, ignore_mods)`
(both `static`, `front_input.c`) check `editor_callbacks->is_active()` once and route to
`is_editor_key_pressed()`/`get_editor_key_axis_value()` (new, mirroring the gameplay
`is_game_key_pressed()`/`get_game_key_axis_value()`) or the gameplay pair otherwise — one
definition of the dispatch rule, not a dozen easy-to-miss copies. When not in an editor session,
every one of these calls resolves to the exact same `is_game_key_pressed()`/
`get_game_key_axis_value()` call as before this change, with the same arguments — normal gameplay
camera behaviour is untouched.

**Separate storage, parallel to (not sharing) the classic gameplay tables:**

- `enum EditorGameKeys` (`globals.h`) — `Gkey_EditorEraseTool = 0`, its own `EDITOR_GAME_KEYS_COUNT`
  sentinel. A completely separate enum from `GameKeys`/`GAME_KEYS_COUNT`, starting at its own 0 —
  not appended to or inserted into the gameplay enum.
- `editor_key_settings[EDITOR_GAME_KEYS_COUNT]` (`config_settings.c`) — same `GamekeySettings`
  struct shape as `game_key_settings[]`, but its own array: `KC_R`/`KMod_NONE`/`BMV_Visible`, with
  a `label_literal` (see §2.1) instead of a `GUIStr_*` string id.
- `settings.editor_kbkeys[EDITOR_GAME_KEYS_COUNT]` (`struct GameSettings`, `config_settings.h`) —
  the actual bound-key storage, parallel to `settings.kbkeys[]`.
- `struct GamekeySettings` (`config_settings.h`) — new `label_literal` field (shared by both
  tables), mirroring `struct SettingOption`'s own pre-existing escape hatch of the same name and
  purpose.
- `setup_default_settings()`/`load_settings()`/`save_settings()` (`config_settings.c`) — each got a
  parallel loop/TOML section (`[editor_keys]`, alongside the existing `[keys]`) for
  `editor_kbkeys[]`, independent of the gameplay one.
- `is_editor_key_pressed()` (`front_input.c`/`.h`) — a deliberately simpler sibling of
  `is_game_key_pressed()`: no `GuiLayer_OneClickBridgeBuild` interaction, no modifier-only
  "hold as a mod key" bindings (`Gkey_RotateMod`-style), no hardcoded alternate-key fallbacks —
  none of that applies to editor keys. Just "is the bound key/button down."
- `set_editor_game_key()` (`kjm_input.c`) — sibling of `set_game_key()`, sharing its
  swap-on-conflict logic via a generalized, table-parameterized
  `check_and_assign_normal_keys_in()` (see §1's "True separation follow-up" below) rather than
  keeping a simplified copy with no collision handling. No modifier-key-group machinery
  (`check_and_assign_mod_keys*_in()`) wired up for it, though the same generalized helpers support
  it — none of the current editor keys need a "bind to a bare Shift/Ctrl/Alt" style binding.
- `frontend_format_key_binding()`/new `frontend_format_editor_key_binding()`
  (`frontmenu_options.c`/`.h`) — both now call a shared `static format_key_binding(const struct
  GameKey *kbk, ...)` taking the raw binding rather than an index, so the two public functions
  differ only in which array (`settings.kbkeys[]` vs `settings.editor_kbkeys[]`) they read from —
  no duplicated key-name/mouse-button formatting logic.
- `defining_editor_key` (new global, `kjm_input.c`/`.h`) — `defining_a_key_id` alone can't tell
  which table it indexes into once there are two (both start at index 0); `define_key_input()`
  branches on this to call `set_editor_game_key()` vs `set_game_key()`.
- `frontgui_definekeys_frame()` (`frontgui_screens.cpp`) — now a `FeBeginTabBar`/`FeTab` pair
  ("Game" / "Editor"), same wrapper the settings screen's own Game/Graphics/Sound/Mouse tabs and
  the editor toolbox's own tool-strip tabs already use. The "Game" tab is the original list,
  unchanged in content; "Editor" is a new list over `editor_key_settings[]`/
  `settings.editor_kbkeys[]`, looping `EDITOR_GAME_KEYS_COUNT` directly (no `num_definable_keys()`
  equivalent needed yet — with one entry there's nothing to filter).
- `handle_editor_erase_tool_shortcut()` (`editor_toolbox.cpp`) — reads the binding via
  `is_editor_key_pressed(Gkey_EditorEraseTool, true, false)`, guarded on `!io.WantCaptureKeyboard`
  (see §2.3).

Ctrl+Z/Ctrl+Y stay hardcoded, per explicit user direction earlier in this project: they're
standard everywhere, so D6's "should be reassignable" concern doesn't really apply to them.

**"True separation" follow-up: gameplay shortcuts with no editor purpose, and shared
swap-on-conflict logic.** Found live, right after the camera/console duplication above: unrelated
gameplay hotkeys were *still* reaching the editor session — "H" (`Gkey_ZoomRoomHeart`, one of
`zoom_shortcuts()`'s room-kind keys) zoomed the camera to "my next room of that kind" while
editing, same for `Gkey_ZoomToFight`/`Gkey_ZoomCrAnnoyed`. Root cause:
`get_dungeon_control_action_inputs()` (`front_input.c`) runs unconditionally for the
`PVT_DungeonTop` view — the same view an editor session uses — and none of its zoom-shortcut
checks were ever part of the camera/console duplication (they're a different function from
`get_isometric_view_nonaction_inputs()`). Unlike the camera/console keys, these have no editor
equivalent to duplicate *to* — they all key off the current player's own dungeon ownership, a
gameplay concept an editor session doesn't have a meaningful answer for — so they're wrapped in
`!editor_callbacks->is_active()` and skipped outright while editing, not routed through
`EditorGameKeys`. (`get_dungeon_control_action_inputs()`'s minimap-click navigation and its
`PSt_CtrlDungeon`/`PSt_BuildRoom`/`PSt_Sell` roomspace branches were left alone — the minimap is
still useful for an editor session, and those three work-states are never active during one, so
they're already inert. A second, cheat-menu-only `Gkey_ZoomRoomTreasure`-range check exists
further down the same file, inside a teleport-destination-selection submenu that isn't reachable
during a normal editor session — not touched, out of scope for what was actually reported.)

Alongside that fix: `set_editor_game_key()`'s swap-on-conflict gap (§1's "revisit once a second
editor key exists" note) stopped being hypothetical the moment the camera/console keys above
landed (`EDITOR_GAME_KEYS_COUNT` went from 1 to 15 in the same pass) — user's ask, ported at the
same time rather than after. `swap_assigned_keys()`/`assign_key()`/`check_and_assign_mod_keys*()`/
`check_and_assign_normal_keys()` (`kjm_input.c`) were all hardcoded to `settings.kbkeys[]`/
`GAME_KEYS_COUNT`/`defined_keys_that_have_been_swapped[]`; each now takes a `const struct
KeyBindingTable *` (`{keys, swapped, count}`) instead, with `_in` added to each name
(`check_and_assign_normal_keys_in()` etc.) and two small getters
(`get_game_key_table()`/`get_editor_key_table()`) supplying the right one. `set_game_key()` keeps
its exact original behaviour (same special-case modifier-group handling for
`Gkey_RotateMod`/`Gkey_SpeedMod` etc., just routed through the parameterized helpers with the game
table); `set_editor_game_key()` now gets real swap-on-conflict via the same
`check_and_assign_normal_keys_in()` call instead of silently allowing two editor keys to end up
bound to the same key. A parallel `defined_editor_keys_that_have_been_swapped[]` array was added
alongside the existing one for this (not yet surfaced in the ImGui Define-Keys screen's "Editor"
tab as a font-color hint — the "Game" tab doesn't show that hint either in the ImGui version, only
the classic non-ImGui menu does, so this isn't a new gap). Deliberately shaped as a reusable table
abstraction, not just inlined two ways, per the user's own explicit forward-looking note: a
hypothetical future Possession-mode key set (flagged as scope creep, **not attempted here**) could
plug in as a third `KeyBindingTable` without the swap/collision logic needing to change again.

## 2. What this slice found

(Found against the first version of this slice, before the separate-storage revision — the
subsystem gaps below are properties of the shared `GameKeys` table/Define-Keys code as it already
existed, not of the editor-specific addition, so they're still worth recording even though
`Gkey_EditorEraseTool` itself no longer lives in that table.)

### 2.1 `GamekeySettings` had no English-first label path

`struct SettingOption` (`config_settingschema.h`) already had a `label_literal` field precisely
for "this option is new, hasn't got a slot in the classic localized `GUIStr_*` table yet" —
`frontgui_screens.cpp` renders `opt->label_literal ? opt->label_literal : get_string(opt->label_stridx)`.
`GamekeySettings` (a much older struct) had no equivalent, because every entry in it up to now
was either an original-game binding (a real `GUIStr_*` id already exists) or a prior KFX addition
whose maintainers *did* add a proper `GUIStr_*` entry (e.g. `GUIStr_ScreenRecord`,
`GUIStr_ToggleConsole`) — appended to the enum "English-first," per that enum's own precedent
comment, but still going through `get_string()`. This slice is the first key binding added
*without* a matching localized string at all — copying the `SettingOption` pattern was the path
of least resistance rather than inventing a new mechanism, but it's the first time this
particular struct needed it. Now used by both `game_key_settings[]` (all `NULL`, unused) and
`editor_key_settings[]` (its one entry actually uses it).

Confirmed safe to append `GUIStr_*`-adjacent labels without regenerating any `.dat` file:
`gui_strings[]` (`config_strings.c`) is `reset_strings()`'d to `""` for every slot *before*
`fill_strings_list()` populates it from the loaded language file, so any enum value added past
what a given `.dat` actually contains resolves to an empty string, not garbage or a crash — this
is what makes the "English-first, translations land incrementally" precedent workable at all. Not
relevant to `Gkey_EditorEraseTool` specifically (it uses `label_literal`, never touches
`get_string()`), but worth recording since the next person extending either table will want to
know this holds.

### 2.2 The Define-Keys menu assumed visible keys were one contiguous prefix

`num_definable_keys()` (`frontmenu_options.c`) counts `game_key_settings[]` entries with
`binding_menu_visibility == BMV_Visible`; `frontgui_definekeys_frame()`'s "Game" tab then loops
`key_id` from `0` to `count-1` and indexes `game_key_settings[key_id]`/`settings.kbkeys[key_id]`
**directly** — correct only if every `BMV_Visible` entry sits in one unbroken block starting at
index 0, with every `BMV_Hidden`/`BMV_ControllerOnly` entry after it. That's exactly how the array
is laid out today (`Gkey_TeleportDefault` is the last visible entry; everything from
`Gkey_CheatMenu1` onward is hidden or controller-only) — but it's an *invariant nobody enforces*,
just a byproduct of every past addition happening to respect it.

This is what originally forced `Gkey_EditorEraseTool` to be **inserted** into `GameKeys` right
before `Gkey_CheatMenu1` rather than appended after `Gkey_MouseRight`, in the first version of
this slice — appending would have put a visible entry outside the block `num_definable_keys()`'s
count actually covers. The separate-storage revision (§1) sidesteps this specific problem for
*future* editor keys (`editor_key_settings[]` is its own small array, looped directly, no
visibility-prefix assumption needed since every entry today is visible) — but the underlying gap
in the **shared** `GameKeys`/`game_key_settings[]` table is unchanged and would still bite the
next person adding a *gameplay* key there. Confirmed at the time that inserting into the middle of
that enum is safe despite shifting every ordinal value below it: keybindings persist by
`toml_name` string lookup, never by raw array index, and nothing else in the codebase serializes a
`Gkey_*` value raw (local machine preference, never network-synced or save-game blobbed).

**Not fixed here**: the contiguity requirement on `GameKeys`/`game_key_settings[]` is still just an
unenforced convention. A `static_assert` or a runtime check partitioning the array by visibility,
rather than requiring careful manual placement, would close this properly — small, self-contained,
but out of scope for this slice.

### 2.3 Nothing gates classic key polling on ImGui keyboard focus

`is_game_key_pressed()`/`is_editor_key_pressed()` (`front_input.c`) both read `lbKeyOn[]`
(`bflib_keybrd.c`, kfx_platform) — a listener on the raw SDL key-event stream that's entirely
independent of ImGui's own input handling, not something ImGui "consumes" first.
`ImGuiContextWantCaptureKeyboard()` (`kfx_platform/src/gui/ImGuiContext.cpp`) exists and correctly
wraps `ImGui::GetIO().WantCaptureKeyboard` — but grepping the whole tree turns up **zero callers**.
Nothing in this codebase currently gates a classic key check on ImGui keyboard focus at all.

For most existing bindings this has never mattered (they're read from contexts with no competing
ImGui text input in play). It would matter here: without an explicit guard,
`Gkey_EditorEraseTool`'s default (`R`) firing while a mapmaker is typing into one of this
session's own new ImGui fields (the object edit panel's X/Y/Z inputs, the gold Value field) would
switch tools out from under them mid-edit. Worked around locally
(`handle_editor_erase_tool_shortcut()` checks `!io.WantCaptureKeyboard` itself, same idiom the
mouse-click handlers elsewhere in `editor_toolbox.cpp` already use for `WantCaptureMouse`) rather
than fixing it at the `is_editor_key_pressed()`/`is_game_key_pressed()` level, since the gameplay
one is shared by every `Gkey_*` consumer across the whole game, in-editor and not — wiring
`ImGuiContextWantCaptureKeyboard()` into it globally is a real, separate change with its own
blast radius (every existing gameplay hotkey would newly stop firing while *any* ImGui widget has
keyboard focus, which might be correct or might not be, depending on context) and deserves its
own investigation, not a drive-by inside an editor keybindings slice. `is_editor_key_pressed()`
itself has no such shared-blast-radius concern (only editor code calls it) — a future editor key
could reasonably gate itself the same way `handle_editor_erase_tool_shortcut()` does, or this
could move into `is_editor_key_pressed()` itself once there's more than one caller to confirm the
guard belongs at that level rather than per-caller.

## 3. What's still open

- ~~**No hotkey scheme for the other 11 tools.**~~ **4 of 12 tools now have one.** The 1998
  manual's own D6 defaults (`02-editing-toolbox.md`'s own citation: `F1`–`F9` tiles, `0`–`5`
  players, `f`/`b`/`z` fill/brush/paint, `t` texture, `l` lights, `Tab` mode, `p`/`i`/`o` views)
  never mapped cleanly onto this editor's actual tool set/naming, so rather than force a 1:1
  translation, the four **highest-value** tools got hotkeys instead: `Gkey_EditorEraseTool` (`R`,
  first slice), plus `Gkey_EditorTerrainTool` (`T`), `Gkey_EditorQueryTool` (`Q`),
  `Gkey_EditorEyedropperTool` (`E`) — chosen because they're toggled back to constantly *and*
  usable standalone, unlike Creature/Hero/Digger/Object/Trap/Door/Fill/Stamp, which all need a
  follow-up model-picker click regardless of how the tool itself was selected (a hotkey saves
  less of the total action for those). `handle_editor_erase_tool_shortcut()`
  (`editor_toolbox.cpp`) generalized into `handle_editor_tool_shortcuts()` to check all four in
  one place rather than four near-identical standalone functions. **Remaining 8 tools**: still no
  hotkey, still an open product-scoping question if wanted later — not attempted, since the
  "which 4" decision was already the point of narrowing scope here.
- **RMB-drag for Brush capture: resolved, staying hardcoded.** Investigated (which mouse button,
  i.e. `KC_UNASSIGNED` default with a mouse-button `code`) and found `bflib_keybrd.h` already
  carries an explicit warning on `KC_MOUSE1`/`KC_MOUSE2`: *"don't use for binding, there will
  likely be conflicts"* — Left/Right click are the primary interaction buttons used everywhere in
  the game for clicking things, so a `Gkey_*` bound to either creates exactly the ambiguity
  ("is this click a world/UI interaction or a hotkey press?") that warning exists to avoid. User's
  call, given that pre-existing caution: RMB-drag stays exactly as it is, not routed through the
  keybinding system. Not a gap — a deliberate exception, consistent with why the base game never
  bound `Gkey_LeftClick`/`Gkey_RightClick` (both `BMV_ControllerOnly`, `CBtn_R2`/`CBtn_L2` — gamepad
  triggers, never a keyboard/mouse `code`) to an actual mouse button either.
- ~~**The contiguity invariant on the shared `GameKeys`/`game_key_settings[]` table (§2.2)**~~
  **Fixed for the ImGui Define-Keys screen.** `frontgui_definekeys_frame()`'s "Game" tab now loops
  every `key_id` in `[0, GAME_KEYS_COUNT)` and filters on `binding_menu_visibility == BMV_Visible`
  directly (same shape the "Editor" tab already used), instead of trusting
  `num_definable_keys()`'s count-of-visible-entries plus an unenforced "they're a contiguous
  prefix" assumption. **Still open for the classic (non-ImGui) Define Keys menu**
  (`frontmenu_options.c`'s `frontend_draw_define_key()`/scroll-tab code, `key_id >=
  num_definable_keys()` bounds checks) — that menu's fixed-size paginated button grid would need
  its own scroll/skip logic reworked to filter per-entry the same way, a bigger and riskier change
  to a system that may already be superseded by the ImGui screen; not touched here.
- **No real localization for `Gkey_EditorEraseTool`'s label** — it's `label_literal`-only,
  English-only, by design for this slice (see §2.1). A proper `GUIStr_*` entry (translatable via
  the normal `.po`/`.pot`/`make pkg-languages` pipeline) is the eventual right answer once there's
  a full editor hotkey set worth translating, not for one binding in isolation.
