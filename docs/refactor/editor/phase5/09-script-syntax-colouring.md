# Phase 5 — script editor syntax colouring

Status: **done** (live-test pending). Second and last of the two "nice-to-have" script features.

## What it does

The script editor (Script > Edit Script...) now colours Dungeon Keeper scripts. A **Colour syntax**
checkbox next to Apply / Close turns it off (kept across opens).

| Colour slot | What |
|---|---|
| comment | `REM` and the rest of that line |
| keyword | flow control: `IF`, `IF_*`, `ENDIF`, `WIN_GAME`, `LOSE_GAME`, `NEXT_COMMAND_REUSABLE` |
| declaration | every other script command (`START_MONEY`, `ADD_CREATURE_TO_LEVEL`, ...) |
| known identifier | values the engine knows: players, variables, flags, timers, comparison words, creature / room / door / trap / spell names |
| string | `"quoted text"` |
| number / punctuation | numbers; `( ) , = ! < >` |
| plain identifier | anything else — party names, typos |

An unknown command name therefore shows in the plain identifier colour, not the command colour, which
makes a mistyped command visible at a glance.

## Design

- The language is a `TextEditor::Language` built in `editor_script_syntax.cpp` **from the engine's
  own tables** — `command_desc[]` for commands, and the shared value tables
  (`editor_script_names.cpp`, extracted from the Script > Commands window) for values — and
  rebuilt every time the editor opens, so custom creatures, rooms and spells from the level's
  config are coloured and a newly added engine command needs no keyword-list edit.
- **Case-insensitive**, like the engine (`get_id()` uses `strcasecmp`); the widget lower-cases
  identifiers before lookup and the sets are stored lower-case.
- **Strings are handled by a custom tokenizer, not the widget's built-in double-quote state**: a
  string runs to its closing quote or the end of the *line*, never further, so one stray quote can't
  colour the rest of the script. Strings are consumed before `REM` is looked for, so `"REM ..."`
  inside an objective's text is not a comment.
- **`REM`** is recognised as a whole word (so `REMOVE_SACRIFICE_RECIPE` is an ordinary command) and
  colours to end of line. The widget's tokenizer can't look back to the start of the line, so `REM`
  is a comment wherever an identifier could start; that is right for every valid script.
- Flow-control classification is a pure function (`editor_script_command_is_flow`) shared with the
  command browser's grouping.

## Known limits

- Colouring is lexical: it does not check argument counts or types, and does not colour a known
  creature name differently in a creature slot than in a room slot.
- Names in tables the config fills at load time (creatures, rooms, spells, traps, doors) are read
  when the editor opens; a config reload while the editor stays open needs a close/reopen (or the
  checkbox toggled twice) to refresh.
- The dark palette's slot colours were used as they are; only the slot assignment is ours.
- Legacy `dk1_command_desc[]`-only syntax isn't specially treated.

## Tests

- `kfx_editor_utest` 273 assertions / 53 test cases (was 188 / 46). `editor_script_syntax_test.cpp`
  attaches the real language to a `TextEditor` and walks lines the way the widget's tokenizer loop
  does: keyword vs declaration vs known value vs number vs punctuation, case-insensitivity, `REM`
  to end of line, `REMOVE_*` not a comment, `"REM"` inside a string, unterminated string stays on
  its line, unknown names stay plain. (Colourising normally happens inside a render pass, which a
  unit test does not have, hence the mirrored loop; the real widget is covered by the live-test.)
  The test target gained the vendored TextEditor include path.
- ftest `editor_script_commands` now also opens the script editor headlessly, which attaches the
  language from the live config tables (must not crash), and inserts through the widget path.
  Sweep 24/24.

## Verification

Layering clean; `kfx_sim_utest` 2181/678; `keeperfx` + `keeperfx_hvlog` build clean; ftest sweep
24/24. Live-test pending: open Script > Edit Script on a real level and check commands, flow
control, values, numbers, strings and `REM` lines each get their own colour; type a misspelt
command and confirm it stays plain; toggle Colour syntax; add a line with an unmatched quote and
confirm the next line is unaffected.
