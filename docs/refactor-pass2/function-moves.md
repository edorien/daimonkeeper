# The move ledger

**Short answer to "should pass 2 keep a list of function moves for the
upstream-merge workflow?"** Yes. The ledger is
[`function-moves.tsv`](function-moves.tsv), maintained by every stage and
checked by [`tools/move_ledger.py`](tools/move_ledger.py).

## Why

The upstream-merge workflow
([`upstream-merge-workflow.md`](../Architecture/upstream-merge-workflow.md)
§2 and §5) already re-applies each upstream hunk *by function name*. It
finds the current home by grepping for the symbol. That works for "where is
this function now", but it misses three things pass 2 makes common:

1. **Callback routing that pass 2 removes.** Pass 1 taught the merge
   workflow to keep `sim_feedback->foo()` wherever upstream calls `foo()`.
   Pass 2 deletes hundreds of those entries. Once an entry is gone,
   upstream's direct call is *correct again*, and keeping the callback shape
   would re-add a deleted entry. The ledger records each removal as a
   `callback-entry … (direct call)` row, so the person merging knows which
   rule applies.
2. **State that changed owner.** For example, `play_gameturn` moves from
   `kfx_game_state` to `kfx_sim_state`, and the view-shake fields move out
   of `struct Dungeon`. Grepping for the field name finds it, but not the
   reason, nor the fact that the save layout changed with it. The ledger
   row carries both.
3. **Split files and split structs.** For example, `LightsShadows` splits
   into a registry and caches, and the packet files split three ways. An
   upstream hunk touching the old file can belong to either half. The ledger
   says which symbols went where.

## Format

`function-moves.tsv` is tab-separated, one row per moved or deleted item.
Lines starting with `#` are comments.

| Column | Meaning |
| --- | --- |
| `stage` | `S01` … `S15` |
| `kind` | `function`, `prototype`, `global`, `field`, `type`, `table` (a data table such as a `NamedCommand[]`), `callback-entry`, `file`, `table-type` (a whole callback table) |
| `symbol` | Function or global name; `Struct.field`; `Table.entry` for callback entries; a glob for `file` rows |
| `from` | Current home (file, or header for prototypes and callback entries) |
| `to` | New home; `(deleted)`; or `(direct call)` for a callback entry replaced by a plain call. A callback entry that was renamed on the way into a port (S15) is written `path/to/port.def#new_name` |
| `status` | `planned` → `done`, or `dropped` when a stage finds the move is wrong (the symbol stays at `from`; `notes` says why) |
| `commit` | The commit that did it, filled in when `done` |
| `notes` | Anything a merger needs: "also X, Y" for helpers that move with it, and the layout-change flags |

A row may stand for a small group: "also …" in `notes` lists the helpers
that move with the named symbol. `lookup` and `detect` search the notes
too.

## Commands

```bash
python3 docs/refactor-pass2/tools/move_ledger.py check
python3 docs/refactor-pass2/tools/move_ledger.py lookup draw_power_hand get_gameturn
python3 docs/refactor-pass2/tools/move_ledger.py upstream src/main.cpp --ref origin/master
python3 docs/refactor-pass2/tools/move_ledger.py detect <stage-base>..HEAD
```

- **`check`** confirms every `planned` row is still at `from` and every
  `done` row is at `to`. It catches drift, for example when an upstream
  merge or another stage moved something first. At the time of writing:
  93 rows, 0 problems.
- **`lookup`** reports where a symbol is defined now, plus every ledger
  row about it.
- **`upstream`** lists every function an upstream (flat-layout) file
  defines and where each one lives in this fork. It is tagged `same-file`,
  `moved`, or `MISSING`, and carries the ledger note. This covers pass-1
  moves too, with no backfill needed: for upstream `src/main.cpp` it maps
  53 moved functions automatically.
- **`detect`** lists functions whose defining file changed across a commit
  range, and marks those missing from the ledger. On pass 1's
  `594f4d861` (the `kfx_apploop` extraction) it finds 86 moves. For pass 2,
  running it over each stage's commits keeps the ledger complete.

## How each stage uses it

1. The stage document's move table and the ledger rows are written
   together, before the work starts. The rows already exist for every
   stage, as `planned`.
2. While working: if something moves that wasn't planned, add a row.
3. When the stage is done: set `status=done` and `commit`, then run
   `detect <stage-base>..HEAD` (no "not in ledger" lines allowed) and
   `check` (clean).

## Change to the upstream-merge workflow (made in S01)

Add to `upstream-merge-workflow.md`:

- **§2, after `find src -iname`:** "For each upstream file in the diff,
  run `move_ledger.py upstream <path>`. Hunks in `moved` functions go to
  the listed file. Check the ledger note before applying."
- **§5, the callback bullet:** "Before re-routing an upstream direct call
  through a callback struct, run `move_ledger.py lookup <function>`. If a
  `callback-entry … (direct call)` row is `done`, keep upstream's direct
  call."
