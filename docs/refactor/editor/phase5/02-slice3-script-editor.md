# Phase 5, slice 3 — Script editor (ImGuiColorTextEdit)

Status: **done.**

## What this adds

A "Script" menu > "Edit Script..." window that lets a level's `map%05lu.txt` script text be viewed
and edited in-session, on top of slice 1's script-preservation fix (which made the text survive a
Save at all, but gave no way to change it in-editor). Built on
[ImGuiColorTextEdit](https://github.com/goossens/ImGuiColorTextEdit) (MIT licensed) rather than a
raw `ImGui::InputTextMultiline()` — no existing precedent anywhere in this codebase for line
numbers, syntax highlighting or find/replace, all of which the library gives for free (though only
syntax highlighting and find/replace are actually deferred this slice, see below).

- **`editor_script.cpp`/`.h`** (new) — `editor_script_frame()` draws the window every frame while
  open (called from `editor_frame()`, same as every other per-frame editor draw call);
  `editor_dialogs_open_script()` seeds the widget from `editor_current_level_script_text()`
  (slice 1's accessor) and opens it. A single file-scope `TextEditor` instance, same
  single-instance-per-dialog shape as this codebase's other `s_show_*`-gated windows.
- **Apply commits to session state, not disk** — `Apply` calls
  `editor_set_current_level_script_text()` (slice 1's setter) and `editor_mark_dirty()`. This
  matches how every other content edit already works here (placing things, painting terrain: edit
  now, persist on the next real Save) rather than Level Settings' own "write straight to disk"
  shape, which only fits because that dialog's fields map onto small, independent `.lof`/`.lif`
  writes. A script edit is exactly the kind of "part of the map's own content" change the
  deferred-persistence model exists for.
- **Plain `ImGui::Begin()`/`ImGui::End()`, not `FeBeginModal()`** — the user's own requirement was
  "embedded in resizable message box". `FeBeginModal()` (every other dialog in this codebase) sets
  `ImGuiWindowFlags_AlwaysAutoResize` (`frontgui_widgets.cpp:494-501`), which can't be manually
  resized. A plain `Begin()`/`End()` pair is resizable by default as long as neither `NoResize` nor
  `AlwaysAutoResize` is set — `ImGui::SetNextWindowSize(ImVec2(700, 500), ImGuiCond_FirstUseEver)`
  just gives it a sensible starting size.
- **Menu wiring** — `editor_menubar.cpp`'s Script menu (a disabled stub since phase 3's own
  slice 3) gets its first real item, `Edit Script...`.

## Vendoring

`deps/ImGuiColorTextEdit/` — exactly the 2 files the library needs (`TextEditor.cpp`,
`TextEditor.h`) plus its `LICENSE`, fetched from the upstream repo. No runtime dependency beyond
ImGui + the STL, matching every other vendored dep here.

- **`build/cmake/modules/Dependencies.cmake`** — new `imgui_color_text_edit` `OBJECT` library
  (right after the existing `imgui` block), `PUBLIC` include dir, linked against `imgui`. Not
  linked against `kfx_common_opts` — same choice `imgui`'s own `OBJECT` library already makes, and
  the reason this slice's build issue (below) took investigation to actually place correctly.
- **`src/kfx_editor/CMakeLists.txt`** — both `kfx_editor` and `kfx_editor_hvlog` targets'
  `target_link_libraries` gained `imgui_color_text_edit`. Scoped to just this library (not added to
  `kfx_common_opts`'s own `INTERFACE` linkage like `imgui`/`tinyfiledialogs` are) since nothing else
  in the codebase needs it.

## The `-Wshadow` build issue

Building the full `keeperfx`/`keeperfx_hvlog` targets after wiring the new dependency in produced
dozens of `error: declaration of 'X' shadows a member of 'TextEditor::Y' [-Werror=shadow]` from
`TextEditor.h` — its constructors idiomatically use parameter names that shadow member names, a
common, harmless C++ style this codebase's own strict `WARNFLAGS` (`CMakeLists.txt:115`,
`-Wshadow -Werror` among others) rejects outright.

This looked contradictory at first: `WARNFLAGS` is only ever applied via
`target_compile_options(kfx_common_opts INTERFACE ...)` (`CMakeLists.txt:133`), and
`imgui_color_text_edit` deliberately never links `kfx_common_opts` — confirmed directly by
inspecting the generated `build.ninja` for `TextEditor.cpp.o`, whose own `FLAGS` are just
`-g -std=gnu++20 -fPIC`, no `-Wshadow` anywhere. An isolated single-target build of just
`imgui_color_text_edit` in a scratch tree also compiled cleanly, with no warnings at all.

The actual mechanism: `TextEditor.h`'s inline constructors aren't only compiled once, as part of
`TextEditor.cpp`'s own object file — they're **header code**, so they're also compiled inline
wherever else the header is `#include`d. `editor_script.cpp` does exactly that, and
`editor_script.cpp.o` is built as part of `kfx_editor`, which *does* link `kfx_common_opts` and so
*does* inherit `-Wshadow -Werror`. The vendored library's own build target was never the problem;
the consuming file was recompiling the header's shadow-prone code under stricter flags than the
library's own build ever uses.

**Fix** — `editor_script.cpp` wraps just the one `#include <TextEditor.h>` line:

```cpp
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#include <TextEditor.h>
#pragma GCC diagnostic pop
```

Same push/ignore/pop-around-the-include shape `src/kfx_frontend/src/frontend.cpp` already uses for
its own `-Wmissing-field-initializers` case around a designated-initializer table — narrowest
possible scope, no change to `WARNFLAGS` itself, no modification of vendored code.

## Deferred, explicitly, not silently dropped

- **Syntax highlighting for the classic DK-script command format** (`IF`/`ENDIF`/
  `SET_GENERATE_SPEED`/`REM` comments, ...) — the library ships `TextEditor::Language` definitions
  for several real languages (C/C++/Lua/Python/...) but none for this one. `Language` is documented
  as extensible for a custom definition; not attempted this slice. Plain text is still a fully
  working editor, just without colour.
- **Find/replace** beyond whatever the library's own default keybindings provide out of the box —
  not independently wired to anything in this codebase's UI (no search-bar chrome added).

## Tests

- No new Catch2 coverage this slice specifically — this is UI/widget glue over slice 1's own
  already-tested `editor_current_level_script_text()`/`editor_set_current_level_script_text()`
  accessors (covered by slice 1's `map_content_roundtrip_test.cpp` additions), same "UI-only glue
  doesn't need its own unit test" precedent slice 2 already established for its own dialog fields.
- No new ftest — same reasoning; nothing here changes save/load behavior itself, only how the
  in-memory script text gets edited before a save.

## Verification

- `python3 scripts/check_layering.py --strict` — 0 violations (`editor_script.cpp`/`.h` stay
  internal to `kfx_editor`; the new vendored dep introduces no cross-library edge).
- `keeperfx`/`keeperfx_hvlog` (native Linux, fresh scratch configure) — clean build, no warnings
  from the vendored code or `editor_script.cpp` after the `-Wshadow` fix.
- `kfx_editor_utest` — full suite passes, no regressions (53 assertions, same count as slice 2 —
  expected, since this slice adds no new test cases).
- **`kfx_sim_utest` still can't be linked** — same pre-existing, unrelated gap carried forward from
  slices 1 and 2: a concurrent session sharing this worktree has an incomplete
  `src/kfx_sim/tests/roomspace_extra_test.cpp` calling `roomspace_liquid_path_is_blocked()`, which
  is declared in `roomspace.h` but still has no definition anywhere in `kfx_sim`. Not this slice's
  file, not touched.
- Full ftest sweep (`-ftests -exitonfailedtest -headless`) — clean, exit code 0, 22/22 tests passed.
  Two false starts along the way, worth recording so a future session doesn't waste time
  rediscovering them: (1) `dist/linux` (the `build-cmake.sh`/`cmake --install --component runtime`
  output) ships only the binary and its runtime libs, not the proprietary original-game data files
  — running an ftest sweep from a copy of it gets partway through `initial_setup` and then fails to
  load `data/*.pal`/`data/*.dat`; use a tree that actually has the original game data staged (an
  existing `KFX_FTEST_DATA_DIR`-populated build tree, or a `build-package.sh` output) instead, and
  just drop the freshly built ftest-enabled `keeperfx` binary into it. (2) that data tree's own
  `keeperfx.cfg` had `INGAME_RES=DESKTOP`, which resolves to a `0x0` "desktop" size under
  `-headless`'s dummy SDL video driver and crashes with `SIGFPE` (a divide against that 0 somewhere
  in screen-mode setup) — fix by setting a real fixed resolution
  (e.g. `INGAME_RES=1920x1080x32`) before running headless.
- Manual live-test — pending; ask the user to open Script > Edit Script..., confirm the existing
  script text loads, edit it, Apply, Save, and confirm the change is actually written to
  `map%05lu.txt` on reload (exercising slice 1's own preservation fix end-to-end through this
  slice's new editing path).
