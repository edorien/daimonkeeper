# The two remaining `check_layering.py --strict` violations are both a wrong-header choice, not a real architectural need

Status: **fixed and verified, 2026-08-29.** Both header swaps applied,
plus the bonus dead-include cleanup below. `scripts/check_layering.py
--strict` now reports **zero un-accepted violations** (down from 2), and
the accepted-residual count drops from 5 to 4 as predicted. Both
existing test suites (`ariadne_regions_test.cpp`,
`power_specials_test.cpp`) pass unchanged against a clean two-variant
build (`keeperfx` + `keeperfx_hvlog`). See "Why not fixed here" below for
the reasoning this document originally gave for deferring the edit —
superseded now that it's been picked up and applied.

`scripts/check_layering.py --strict` currently reports exactly two
violations that are *not* in `ACCEPTED_VIOLATIONS` (the permanent,
by-design residuals documented in
[`../../Architecture/architecture.md`](../../Architecture/architecture.md)
§8.2):

```
kfx_pathfinding -> kfx_sim:
  src/kfx_pathfinding/src/ariadne_regions.c  includes  player_data.h

kfx_sim -> kfx_script:
  src/kfx_sim/src/power_specials.c  includes  api.h
```

Both were already known (`docs/refactor/testing/stage-04b-kfx-pathfinding.md`
found and reverted a naive fix for the first one back in stage 4b), but
neither had been root-caused past "a keyword grep found nothing, so it
looked like dead weight." This document does that, for both, and records
the unit tests now protecting each one's actual behavior.

## `ariadne_regions.c` → `player_data.h`

`player_data.h` (`kfx_sim`) is used for exactly two symbols in this
file: `PlayerNumber` (a function parameter type) and `PLAYERS_COUNT` (a
bounds check). Neither actually requires this header:

- `PlayerNumber` is `typedef`'d in `kfx_platform`'s `globals.h`
  (`kfx_pathfinding` already depends on `kfx_platform`) — `player_data.h`
  doesn't define it, just transitively pulls in the same `globals.h`
  typedef.
- `PLAYERS_COUNT` **is independently `#define`'d a second time**, to the
  same value (`9`), in `kfx_config`'s `kfx_config_state.h` — already a
  legitimate `kfx_pathfinding` dependency (`kfx_platform → kfx_config →
  kfx_pathfinding`). `kfx_config_state.h`'s own comment already explains
  why the duplicate `#define` exists: "kfx_config can't [depend on
  kfx_sim to reuse player_data.h's copy]... must stay in sync manually if
  PLAYERS_COUNT ever changes."

The one and only place `PLAYERS_COUNT` is read in this file:
`navigation_regions_connected()`'s owner-range bounds check
(`ariadne_regions.c`, `if (owner < 0 || owner >= PLAYERS_COUNT) return
true;`).

**The fix**: swap `#include "player_data.h"` for `#include
"kfx_config_state.h"` (or wherever the header that actually defines
`kfx_config`'s copy of `PLAYERS_COUNT` should canonically live — that's a
naming/placement call, not a behavior one). Zero behavioral risk: same
integer value, same bounds check, just resolved through the
already-legitimate lower-ranked copy instead of reaching upward for a
`#define` that happens to also exist one layer up.

**Test coverage now in place**
(`src/kfx_pathfinding/tests/ariadne_regions_test.cpp`, 10 tests):
`navigation_regions_connected()`'s full behavior is pinned down —
including a from-scratch, hand-verified minimal two-triangle fixture
(mutual tag-link at corner 0, everything else a border edge) that makes
`regions_connected()`'s real `region_connect()`/`region_lnk()` BFS
actually succeed, not just the early-return bounds checks — so a future
header swap has a real regression net, not just "it still compiles."
Also covers `regions_connected()`'s bounds/floor-height-blocking checks
directly, and `region_store_init`/`region_get`/`region_put` (the
region-allocation circular queue, unrelated to this specific violation
but in the same file, cheap to cover alongside it).

## `power_specials.c` → `api.h`

`api.h` (`kfx_script`) is used for exactly one symbol in this file:
`script_hooks` (the `ScriptHookCallbacks` global, called at
`activate_dungeon_special()`'s first line:
`script_hooks->lua_on_special_box_activate(...)`).

`api.h` itself doesn't define `script_hooks` — it just `#include`s
`kfx_config`'s `script_hooks.h`, with a comment on that exact line
explaining why: "`enum ApiEventDataType`/`struct ApiEventData` live in
kfx_config's `script_hooks.h` — kfx_sim's `actionpt.c` is the
lowest-ranked real producer of event data payloads and can't reach up to
this (kfx_script) header, so the type is defined there and just used
here." `script_hooks` (the extern pointer) and `struct
ScriptHookCallbacks` (with `lua_on_special_box_activate`) are both
declared directly in `kfx_config/include/script_hooks.h` — already a
legitimate `kfx_sim` dependency.

**The fix, as applied**: `power_specials.c` already had a separate
`#include "script_hooks.h"` a few lines below `#include "api.h"` — so the
fix turned out to be simpler than a swap: just delete the `#include
"api.h"` line outright. Same symbol, same physical declaration, same
type — `api.h` was never anything but a redundant extra hop through
`kfx_script` to reach a header this file already included directly, one
layer below `kfx_sim`.

**Test coverage now in place**
(`src/kfx_sim/tests/power_specials_test.cpp`, 6 tests): the one
`script_hooks`-calling function, `activate_dungeon_special()`, is a
large, heavily dungeon/thing/config-state-coupled orchestration function
— not attempted here, the same "needs a fuller context" call this whole
testing plan has made for similarly-shaped functions elsewhere (e.g.
`creature_states_barck.c`, `docs/refactor/testing/comprehensive/stage-08b-kfx-sim-clusters.md`).
Instead: `box_thing_to_special()` (self-contained, pattern A, called
from several other files besides this one) and `activate_bonus_level()`
— **`kfx_sim`'s first `SimFeedbackCallbacks` pattern-B test in this
whole plan**, despite `sim_feedback` being called through pervasively
across `kfx_sim`'s source. Neither test exercises `script_hooks`
directly (no cheap call site does), but both add real coverage to the
same file the violation lives in, and the `SimFeedbackCallbacks` pattern
proven here is now a template for testing other `sim_feedback` call
sites later.

## Bonus finding: one of the five *accepted* residuals is now fully dead code

While investigating the two `--strict` violations above, the same
"verify against a real build, don't trust a grep" discipline applied to
`ACCEPTED_VIOLATIONS`'s five permanent entries too. One of them,
`("src/kfx_game/src/console_cmd.c", "game_session_loop.h")`, is
**currently dead**: its own comment in `scripts/check_layering.py` says
it's "the one legitimate direct call to the real per-frame `update()`
dispatcher, from a debug console command that manually advances a turn"
— but grepping `console_cmd.c` for every symbol `game_session_loop.h`
declares (`update`, `find_frame_rate`,
`packet_load_find_frame_rate`, `display_should_be_updated_this_turn`,
`keeper_screen_swap`, `keeper_wait_for_next_turn`,
`keeper_gameplay_loop`, `game_loop`, `network_yield_poll_gameplay`,
`network_yield_waiting_gameplay_packets`, `network_yield_draw_frontend`,
`host_packet_received`, `interpolate_time`) finds **zero** matches. The
call this comment describes has apparently been removed or moved
elsewhere since the comment was written, and the `#include` was left
behind.

Verified, not just grepped: removed the `#include` line from
`console_cmd.c` and rebuilt both `kfx_game` (the OBJECT library alone)
and the full `keeperfx` executable — both compile clean.
`check_layering.py --strict`'s accepted-residual count drops from 5 to
4 with the include gone, no new violations introduced. Reverted before
committing anything (this document's job is to record verified findings,
not to make the edit — same reasoning as the two violations above, and
this one is lower-risk than either of them: not even a header swap,
just deleting a confirmed-unused `#include`).

**Applied**: deleted the `#include "game_session_loop.h"` line from
`console_cmd.c`, and the now-permanently-unmatched
`("src/kfx_game/src/console_cmd.c", "game_session_loop.h")` tuple from
`ACCEPTED_VIOLATIONS` in `scripts/check_layering.py`.

## Why not fixed immediately (historical)

Both header-swap fixes were one-liners with no behavioral change — the
symbol each file needs already exists, verbatim, at a legitimate lower
layer. Deliberately not applied in the pass that wrote this document,
for the same reason stage-04b reverted its own near-miss attempt rather
than "chasing a bigger fix" mid-testing-effort: that pass's job was to
hand a future change a root cause, a verified fix path, and a regression
net, not to make the edit itself outside the scope it was asked for.

## Result of applying the fixes (2026-08-29)

All three edits applied in a single pass:

- `ariadne_regions.c`: swapped `#include "player_data.h"` for `#include
  "kfx_config_state.h"`.
- `power_specials.c`: deleted the redundant `#include "api.h"` (see
  above — `script_hooks.h` was already included separately).
- `console_cmd.c`: deleted the dead `#include "game_session_loop.h"`,
  plus its matching `ACCEPTED_VIOLATIONS` tuple in
  `scripts/check_layering.py`.

Verified: clean two-variant build (`keeperfx` + `keeperfx_hvlog`);
`ariadne_regions_test.cpp` (10 test cases, 17 assertions) and
`power_specials_test.cpp` (6 test cases, 8 assertions) both pass
unchanged; `check_layering.py --strict` now reports **zero un-accepted
violations**, with the accepted-residual count at 4 (down from 5);
`scripts/check_layering_symbols.py` (the post-build symbol-level audit,
see `check-layering-symbol-level-blind-spot.md`) also reports zero new
violations, confirming none of these three edits introduced a new
symbol-level issue either.
