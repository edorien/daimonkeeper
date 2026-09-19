# Phase 5, slice 9 — objective / message helper

Status: **done** (live-test pending). Completes phase 5's planned slices.

## What it adds

**Script > Objective / Message...** opens a small form: kind (Objective / Information), message
number, text, optional location. A live "Will insert:" preview shows the exact line; **Insert**
writes it into the script's user region (§4.4 — never the managed setup region):

```
QUICK_OBJECTIVE(12,"Build a lair and hatch a creature.",PLAYER0)
QUICK_INFORMATION(13,"The Heart is your life; guard it.")
```

- Script editor window **open**: the line goes in as its own line at the editor's cursor (through
  the widget, so its own undo works); press Apply there to keep it.
- Script editor **closed**: the line is appended to the session script text and the session is
  marked dirty (persisted by the next Save, like every other script edit).
- The number defaults to the smallest unused one, is re-picked after each insert, and a caption
  warns when a number is already used.

## Investigation (against `lvl_script_commands.c`, not the doc's guess)

- The doc's `QUICK_OBJECTIVE(n, "text", player)` is wrong about the third argument. The command
  table is `"NAla"`: number, text, optional **location** (a map location such as `PLAYER0`; default
  `ALL_PLAYERS`), optional custom icon. The message is always shown to all players
  (`ALLOCATE_SCRIPT_VALUE(command, ALL_PLAYERS)`), so there is no player selector.
- `QUICK_OBJECTIVE` and `QUICK_INFORMATION` share one table, `quick_messages[256]`, text capped at
  1023 characters. Reusing a number only produces a warning ("overwritten by different text"), so
  the uniqueness caption is advice, not a block.
- The script tokenizer has no escape for `"` inside a string, so quotes become apostrophes and line
  breaks become spaces in the generated line (documented in the window's preview, which shows the
  sanitised result).
- `DISPLAY_OBJECTIVE` / `DISPLAY_INFORMATION` (translated-string-id forms) are not offered: a
  mapmaker writing their own text wants the QUICK forms; the string-id forms need a text table
  the editor has no UI for.

## Design

- `editor_script_message.h/.cpp` — pure string logic (format, numbering, splice, "keep out of the
  managed region"), no session or ImGui, so it is unit-tested.
- `editor_message_helper.h/.cpp` — the window. `editor_script.cpp` gained
  `editor_script_insert_block_at_cursor()`.
- Insert positions inside the managed region are moved to just after its end marker (the region is
  regenerated on Apply, so anything there would be lost). CRLF scripts keep CRLF.

## Known limits

- The optional custom-icon argument and the `_WITH_POS` variants are not generated (existing lines
  using them are recognised when picking a free number).
- Numbers are found by scanning the script text, not by evaluating it, so a command built some
  other way is not seen.
- The location field only accepts a plain identifier (`PLAYER0`, `ALL_PLAYERS`, ...); anything else
  is dropped from the line rather than risking a broken script. It does not check that the name is
  a valid location.

## Tests

`kfx_editor_utest`: 132 assertions / 34 test cases (was 104 / 24) — formatting (quote/newline
sanitising, location, truncation), numbering (both commands, comments, `_WITH_POS`, spacing),
splicing (middle, start, empty, append with and without a trailing newline, CRLF), and the
managed-region guard (inside, boundaries, region as the script tail, unterminated region).
No ftest — UI glue over tested pure logic and the slice 1/3 script-text accessors.

## Verification

Layering clean; `kfx_editor_utest` 132/34, `kfx_sim_utest` 2181/678; `keeperfx` +
`keeperfx_hvlog` build clean. ftest sweep not re-run: nothing here touches the load path (last full
run, 23/23, was on the previous commit). Live-test pending: Script > Objective / Message, insert
with the script editor closed and open, confirm the line, Apply, Save, and check the level's
objective appears in game.
