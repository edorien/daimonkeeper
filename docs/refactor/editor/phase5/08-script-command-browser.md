# Phase 5 — Script > Commands (command browser)

Status: **done** (live-test pending). First of the two "nice-to-have" script features; the second
(syntax colouring) is separate.

## What it does

**Script > Commands...** opens a resizable, **non-modal** window (it stays open beside the script
editor so several things can be inserted in a row).

- **Commands tab** — every script command the engine accepts (175, read from `command_desc[]` so
  it cannot drift from the parser), grouped by purpose, with a filter box and a *Classic commands
  only* checkbox. Each entry shows its signature (`START_MONEY(player, number)`), a
  `[KFX]` tag if it is a KeeperFX addition, and — for the classic commands — a one-line
  description written from the 1993 Editor manual. **Insert** (or double-click) puts a template
  line (`START_MONEY(PLAYER0,0)`) into the script.
- **Values tab** — the Editor's "variable groups" from the QuickCard / manual §5.2.1, as
  click-to-insert tokens read from the live name tables (so custom creatures, rooms and spells
  show up): Players, Variables, Flags & timers, Comparisons, Hero creatures, Evil creatures, Rooms,
  Doors, Traps, Spells & powers, and the action point numbers currently on the level. Values insert
  at the script editor's cursor, replacing its selection; they are disabled while the script editor
  is closed (no cursor to insert at).

### Groups (manual chapter 5.2, with the manual's "Miscellaneous" split in two)

Flow control & conditions · Flags, timers & variables · Level setup · Computer players ·
Creatures, spells, traps & doors (availability) · Research · Manipulating creatures · Adding
creatures, parties & objects · Objectives & messages · Map, slabs & powers · KeeperFX
configuration · Other. Classic commands come first in each group.

## Insertion rules (`editor_script_insert_command`)

- Cursor line blank → the command goes on that line; otherwise on a new line below it.
- Indentation follows the neighbouring structure: one tab deeper directly under an `IF…` line, one
  level back for `ENDIF` after a body.
- A cursor inside the managed setup region is moved to just after its end marker.
- CRLF scripts stay CRLF.
- Script editor open: edited through the widget as one undoable step, cursor left at the end of the
  new line, press Apply to keep it. Script editor closed: appended after the last non-blank line
  of the session script and the session marked dirty.

## Design notes

- `editor_script_commands.h/.cpp` — pure logic (grouping table, summaries, signature/template from
  the engine's argument letters, insertion), unit-tested without the engine.
  `editor_command_browser.cpp` — the window and the engine-table reads.
- Templates use required arguments only, each as a recognisable placeholder (`PLAYER0`, `0`,
  `CREATURE`, `ROOM`, `NAME`): the user fills the blanks. They deliberately are not valid until
  edited where no sane default exists.
- A command missing from the grouping table falls into **Other** rather than disappearing, and the
  `editor_script_commands` ftest fails if any exist, naming them, so a newly added engine command
  is prompted to be classified.

## Known limits

- Only the ~35 classic commands have descriptions (they are the ones the original manual
  documents). KeeperFX commands show their signature only; there is no bundled KeeperFX script
  documentation to draw from, and inventing descriptions for ~140 commands unverified would be
  worse than none.
- Argument letters give types, not meaning, so signatures say `text`, `number`, `player`… rather
  than the manual's `[action point]`, `[can be available]`. Text arguments are also used for names
  (spell, trap, door, flag), so placeholders for those read `NAME`.
- The legacy `dk1_command_desc[]` table (version-0 script syntax) is not listed; the current table
  is the superset scripts are written against.
- No argument validation or fill-in-the-blanks prompt yet — that belongs with the Validate
  button (design §4.1), still not built.

## Tests

- `kfx_editor_utest` 188 assertions / 46 test cases (was 132 / 34): grouping, unknown → Other,
  classic/summary flags, signatures (optional, `!`, `+`), templates, insertion (blank line, below,
  end of script, IF/ENDIF indentation, CRLF, managed-region guard, empty script).
- ftest `editor_script_commands` (new): builds the catalogue from the real tables (175 commands),
  asserts none is unclassified, and inserts a command through the closed-editor path
  (`IF(...)` context → indented `WIN_GAME`). ftest sweep 24/24.

## Verification

Layering clean; `kfx_sim_utest` 2181/678; `keeperfx` + `keeperfx_hvlog` build clean; ftest sweep
24/24. Live-test pending: Script > Commands; filter; Classic only; insert with the script editor
open (cursor on blank line, under an IF, inside the managed block) and closed; Values tab inserting
`PLAYER1`, a creature name, a comparison; confirm Apply keeps it.
