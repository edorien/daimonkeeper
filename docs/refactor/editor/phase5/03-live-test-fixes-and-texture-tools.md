# Phase 5, round 4 — live-test bug investigation + texture-set tools

Status: **all of it done** — texture-set dropdown, per-slab texture paint, and (after a second,
much deeper live-test round) the two script-editor bugs root-caused and fixed. Ambient light is a
scope finding requiring a decision, not something "done" or still "needs investigation".

## Context

After slice 3 (script editor) shipped, live-testing turned up two reports:

1. "when opening an existing level, is the edit script supposed to be empty currently"
2. "script editor doesn't accept input"

The user also pushed back on this doc's own habit of parking things as "needs investigation" —
specifically calling out three items from slice 2's deferred list (base texture set, ambient
light, per-slab texture paint) as important, not to be silently deferred again. This round
addresses all five items directly: both bugs were root-caused and fixed (§1-2, after an initial
non-interactive investigation pass came up empty and a much longer live, iterative diagnostic
round with the user found the real cause), and the two texture-set items were built rather than
re-deferred. Ambient light turned out not to be a per-level concept at all — see §3.

## 1-2. Script editor showed empty text AND didn't accept input — one root cause, fixed

Both reports turned out to be the same bug: `s_text_editor` (the script editor's `TextEditor`
instance, `editor_script.cpp`) was drawing every single glyph with **alpha 0** — fully
transparent. The widget was working correctly the entire time (real script text loaded, real
keystrokes applied to the real document, correct on-screen positions, real vertices submitted to
the GPU) — nothing was ever visible because the ink itself had zero opacity. Confirmed this
conclusively via a long live-test diagnostic sequence (see below); the "doesn't accept input"
report was a direct consequence of the same bug, not a separate one — you can't see a cursor or
newly typed characters if nothing renders at any alpha above zero.

**Root cause: a static-initialization-order fiasco.** `TextEditor`'s constructor calls
`SetPalette(defaultPalette)`. `defaultPalette` is a *non-local* `static` member, defined in
`deps/ImGuiColorTextEdit/TextEditor.cpp` — a different translation unit than `editor_script.cpp`,
where `s_text_editor` (a global) lives. C++ gives **no guaranteed order** between non-local static
initializers across translation units. If `s_text_editor`'s constructor happens to run before
`TextEditor.cpp`'s own static initializer has set `defaultPalette`, `SetPalette()` copies whatever
`defaultPalette` holds *at that moment* — for a not-yet-constructed non-local static, that's its
zero-initialized state (every byte 0). `SetPalette()` is a plain value copy into `paletteBase`, so
this isn't a transient glitch — the widget is stuck with an all-zero palette for its entire
lifetime, no matter what `defaultPalette` eventually becomes.

**The investigation** (each step ruled something else out first, which is worth keeping as a
record — this bug hides well):
- `read_level_script_text()` (`editor_session.cpp`) — verified live: added a temporary
  diagnostic, ran `ftest_editor_save_reload` (calls `editor_open(1, false)`) against
  `core_files/campgns/keeporig/map00001.txt` (a real 6673-byte script), confirmed all bytes read
  correctly. Not the bug.
- `TextEditor::SetText()`/`GetText()` — a standalone test program (no game code, no display)
  round-tripped the same 6673-byte file byte-for-byte. Not the bug — though this test never
  actually called `Render()`, which mattered later.
- Live diagnostics (WARNLOG, then a temporary file-based diagnostic once the check needed to
  live inside the vendored `.cpp`) progressively confirmed, against the user's own real session:
  session text length correct (10359 bytes) at every checkpoint from open through the first
  render frame; window size/content region/collapsed state/font size all healthy
  (700×500 window, 676×453 content, `font_size=13.0`); the widget's own visible-row range correct
  (rows 0–31 of 274 real lines, right at the top); keystrokes genuinely growing the document
  (`10359→10360→…`) with `want_text_input=1`/`want_capture_kbd=1`; a real, loaded, visible glyph
  for `'R'` at the exact baked font size used (no fallback); the exact screen position of the
  first glyph landing squarely inside the draw list's own clip rectangle; and — most tellingly —
  1082 real glyphs actually appended as 4328 real vertices (exactly 4/glyph) to the draw list.
- A side-by-side test (plain `ImGui::Text()` planted directly above the widget, then wrapped in
  its own temporary child window matching `TextEditor::Render()`'s own shape) rendered correctly
  both times — ruling out "text is broken inside child windows" as a category.
- First (wrong) theory: the vendored library predates Dear ImGui's newer dynamic multi-texture
  font atlas, and its raw `font->RenderChar()` calls (bypassing `ImGui::Text()`'s own internal
  texture setup) might be sampling the wrong texture. Patched `TextEditor.cpp` locally with
  `drawList->PushTexture(io.Fonts->TexRef)`/`PopTexture()` around the glyph loop — **no visible
  change**. A follow-up diagnostic proved why: the draw list's texture reference already exactly
  matched `io.Fonts->TexRef` *before* the patch ran. Texture binding was never the problem; the
  patch was reverted (the vendored file is untouched — no local patch needed for this bug).
- That contradiction prompted re-deriving why the "plain Text() inside a child" comparison had
  worked in the first place (`BeginChild()` draws its own background fill automatically for any
  child with one, so both scenarios had a preceding draw command — undermining the specific
  "wrong texture inherited from an earlier command" story) — which pointed at *color*, not
  geometry or texture, as the one dimension not yet directly measured.
- A diagnostic logging `updatePalettes()`'s actual resolved values found it immediately:
  `style.Alpha=1.000` (globally fine) but `paletteBase[text]=0x00000000` — completely zeroed, not
  the dark theme at all. From there, the static-init-order explanation was immediate given
  `s_text_editor` and `TextEditor::defaultPalette` live in separate translation units.

**The fix** — entirely within `editor_script.cpp`, no vendored-code patch: `TextEditor::
GetDarkPalette()` is safe (a *function-local* static, correctly initialized on first call
regardless of translation-unit order — C++11's "magic statics" guarantee). `editor_dialogs_open_
script()` now calls `s_text_editor.SetPalette(TextEditor::GetDarkPalette())` once per open, right
before `SetText()`, sidestepping the non-local `defaultPalette` this constructor was implicitly
(and unreliably) depending on. Confirmed live: script text now visible on open, typed characters
now visible immediately.

## 3. Ambient light — scope finding, not a fix

Investigated where ambient light actually lives, since the deferred note in
`01-slice2-level-settings-expansion.md` only said "needs investigation into where/how it's
stored." Found it: **`GLOBALAMBIENTLIGHT`** (`config_rules.c:119`, field
`gameplay.global_ambient_light`) is a **`rules.cfg` value — per campaign, not per level.**
`light_data.c:2389-2396` syncs the live `lish.global_ambient_light` from this config value; nothing
in `MapContent`/`.inf`/`.lof` carries a per-level override, and no script command sets it either.

This means a "Level Settings" field for ambient light would be actively misleading — changing it
would silently affect every other level in the same campaign, not just the one currently open, the
same category of problem `phase3/06-slice7-verify-map.md`'s plan already ruled out room/portal
checks for ("would be actively wrong to enforce"). **Not built.** This needs a decision, not more
investigation:
- Expose it read-only in Level Settings (informational, "this campaign's ambient light: N," no
  edit control) — cheap, honest, doesn't imply per-level control that doesn't exist.
- Build actual campaign-rules editing (a genuinely different, bigger feature — editing
  `rules.cfg`, which affects every level in the campaign, not a Level Settings concern at all).
- Drop it entirely — it was never a real per-level authoring need, just a listed field in the
  original design doc that didn't survive contact with how this engine actually stores it.

## 4. Base texture set (done)

Turned out much smaller than `01-slice2-level-settings-expansion.md`'s deferred note feared (it
cited needing to investigate `texture_pack_desc`/custom `tmap?%03d.dat` discovery):

- **Persistence already existed** — `MapContent::texture_id` (`map_content.h:128`) has round-tripped
  through `.inf` since before this round (`map_content_writer.cpp`/`map_content_reader.cpp`), and
  `snapshot_map()` already reads the live `kfx_config_state.texture_id` at save time. No new
  session-state accessor pair was needed (unlike name/players/multiplayer/description, which don't
  exist independently of anything the live engine already tracks).
- **`texture_pack_desc[]`** (`kfx_game`'s `lvl_script_commands.c:428`) is a small, fixed, 15-entry
  named table (ids 0-14; `BIG_BREASTS` is a later-added second alias for id 6, omitted from the
  dropdown since a combo can only show one label per id) — no filesystem scanning for custom
  `tmap?%03d.dat` packs turned out to be necessary for this to be useful; that's a separate,
  smaller concern that can be added later if a real map ever needs it.
- **New shared header** `editor_texture_packs.h` — a small local copy of the 15 names (not
  `#include`-ing `lvl_script_lib.h` from `kfx_game` just for one fixed table), shared by both the
  Level Settings dropdown and the New Map dialog (whose own "Texture set" field was a bare
  `ImGui::InputInt` before this — upgraded to the same `FeCombo` for consistency) and the new Paint
  Texture toolbox tool below.
- **Live preview** — Apply calls `load_texture_map_file()` (`kfx_render`), the same function the
  `SET_MAP_TEXTURE` script command and its Lua equivalent already call mid-session
  (`lua_api_map.c`), so this is a proven-safe call outside of level load, not a new risk.
- Independent of the `.lof` write in the same Apply click — a failed `.lof` write no longer
  silently skips the texture-set change too.

## 5. Per-slab texture paint (done, v1)

Also smaller than feared once the real mechanism was found:

- **`kfx_config_state.slab_ext_data[]`** (`kfx_config_state.h:139`, one byte per slab) is already
  fully wired into rendering (`engine_render.c:4080` adds it as a texture-block offset) and already
  settable live via the classic script command / Lua API (`lua_api_slabs.c`) — the only gap was an
  editor tool to paint it, and editor-side persistence.
- **New tool**: `EdTool_PaintTexture`/`PSt_EditorPaintTexture` (`editor_toolbox.cpp`,
  `config_players.h`), Utility tab, "Paint Tex". Same **D2 single-player-local-exception** shape
  Stamp already established: continuous LMB-drag writes directly into
  `kfx_config_state.slab_ext_data[]` client-side, no packet round-trip, no undo/redo journal entry
  — the same accepted "no undo for this tool, repaint to fix" scope Stamp already ships with.
  `PSt_EditorPaintTexture` exists purely for click routing (so a stray click doesn't fall through
  to whatever tool was active before), same as `PSt_EditorStamp`/`PSt_EditorEyedropper` — confirmed
  by reading `set_player_state()` (`player_data.c`) directly: an unrecognized `PSt_` value gets no
  special setup, which is exactly what every other purely-client-side editor tool already relies
  on.
- **New persistence** — `.slx` ("ExtSlab," `load_ext_slabs()`/`lvl_filesdk1.c`) is a real,
  established format: confirmed **99 `.slx` files across real shipped classic-format campaigns**
  under `core_files/campgns/`. `MapContent` gained `slab_texture` (a flat per-slab byte vector,
  same shape as `slab_kind`/`slab_owner`); `MapContentReader`/`MapContentWriter` gained
  `read_slab_texture()`/`write_slab_texture()`, both format-independent (shared base-class methods,
  like `.inf`, not per-format overrides) since `.slx` isn't tied to the classic/KFX-native
  distinction. `write_slab_texture()` is always called (like `.slb`/`.own`), not skipped when every
  override is 0, so a KFX-editor save is self-consistently complete. `snapshot_map()`
  (`editor_mapsave.cpp`) reads `kfx_config_state.slab_ext_data[]` live into the snapshot, the same
  shape as `slab_kind`/`slab_owner`; loading an existing level already populates
  `slab_ext_data[]` before `editor_open()` ever runs (`load_ext_slabs()` is part of the normal
  level-load path), so no new read-on-open step was needed either.
- **Found and fixed a real bug while adding test coverage**: `map_content_roundtrip_classic_test.cpp`'s
  own sample-content builder predates `slab_texture` and never populated it, so the new
  `write_slab_texture()` crashed (`SIGABRT`, out-of-bounds vector access) against that fixture the
  first time the Catch2 suite ran with this change. Fixed two ways — `write_slab_texture()` now
  tolerates a `slab_texture` shorter than `map_tiles_x*map_tiles_y` (treats missing entries as 0,
  the same convention the reader already used for a missing/short `.slx` file) as a general safety
  net for future callers, *and* the classic test's own fixture was updated to populate
  `slab_texture` for genuine round-trip coverage on that path too.
- **Deferred, explicitly**: no rectangle/fill mode (continuous drag-paint only, which already
  covers large areas reasonably well); no undo/redo (matches Stamp's own precedent, not a new gap).

## Tests

- **Catch2** (`map_content_roundtrip_test.cpp`, `map_content_roundtrip_classic_test.cpp`) — both
  extended with non-trivial `slab_texture` values (not just an all-zero grid) and a round-trip
  assertion, plus two new dedicated cases: reader leaves `slab_texture` all-zero (not a failure)
  when `.slx` is missing, writer always produces a `.slx` even for an all-zero grid (unlike
  `write_script()`'s skip-when-empty convention). `kfx_sim_utest`: 2176 assertions / 677 test cases,
  all passing. `kfx_editor_utest`: 53 assertions / 10 test cases (no new editor-side Catch2 tests —
  the toolbox click handler is UI/input glue over already-tested `kfx_config_state` primitives,
  same "UI-only glue doesn't need its own unit test" precedent established in prior slices).
- No new ftest for the paint tool — same reasoning (UI glue over an already-live-tested engine
  primitive), and the tool is genuinely not scriptable via the existing ftest input-simulation
  surface without new plumbing.
- No new Catch2/ftest for the §1-2 palette fix — a one-line runtime call fixing a static-init-order
  bug isn't meaningfully unit-testable (the underlying non-determinism is a link-order property,
  not something a test can force either way); the real regression net here is the live confirmation
  already obtained (see below) plus not regressing the full build/test/ftest sweep.
- Full ftest sweep (`-ftests -exitonfailedtest -headless`) — clean, exit code 0, 22/22, re-run
  after the §1-2 fix landed too.

## Verification

- `python3 scripts/check_layering.py --strict` — 0 violations (all new includes stay within or
  point to already-lower layers: `kfx_editor` reaching `kfx_config`/`kfx_render`/`kfx_sim` headers,
  `kfx_sim`'s own `map_content_*` files staying in `kfx_sim`).
- `keeperfx`/`keeperfx_hvlog` (native Linux, fresh scratch configure) — clean build, no warnings.
- `kfx_sim_utest`/`kfx_editor_utest` — both pass in full (see Tests above); the concurrent
  session's previously-incomplete `roomspace_liquid_path_is_blocked` is now resolved (both suites
  link and run cleanly this round, unlike the gap tracked in slices 1/2's own docs). Re-run clean
  after the §1-2 fix landed too (53/10, 2176/677 — no change in count, as expected for a fix with
  no new testable logic).
- Full ftest sweep — clean, 22/22, exit code 0, both before and after the §1-2 fix.
- Manual live-test — **done for everything in this round.** The texture-set dropdown and Paint
  Texture tool were confirmed live (screenshots showing the dropdown and the new toolbox button
  working as expected). §1-2's fix was confirmed live twice over: the script text is now visible
  on open (screenshot showing a real, syntax-plain script with line numbers and the cursor), and
  keyboard input now visibly works (explicit user confirmation after testing).
