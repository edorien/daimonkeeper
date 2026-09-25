# Campaign progress overhaul: multi-campaign save, richer Land View

Status: **All five phases (A-E) landed.** `game_campaign_progress.c`/`.h` in `src/kfx_game/`; the
`update_ensigns_visibility()` change and the Campaign Select slider in `src/kfx_frontend/src/`;
Continue Game's `use_classic_menu()` split across `game_saves.c`/`frontend.cpp`; the new
`SOptT_Action` schema type and Reset Progress row across `config_settingschema.{h,c}`/
`frontgui_screens.cpp`. Written at the user's request ("write a design doc first") after live
feedback on the Phase G ImGui settings build; revised three times before implementation began
(round 2: file format, slider location, and the Continue button's fate; round 3: the file format
extended to cover `IntralevelData`, the slider restricted to unlocked levels, `-classicmenu` given
its own independent, unchanged code path, and Reset Progress confirmed as all-campaigns; round 4:
`ensign_overrides` given its own key too, per the instruction to preserve every field
`IntralevelData` held rather than drop any). See §7 for Phase A's own implementation notes
(including one real design correction found only while building it — the `BONUS_AVAILABLE` key
ended up shaped differently than §3.1 first specified), §8 for Phase B's (a second, related
correction to Phase A's own `campaign_progress_record_level_completed()`, plus a note on why
`update_ensigns_visibility()` itself isn't unit-tested), §9 for Phase C's, §10 for Phase D's (the
"pre-select a specific campaign" idea in §3.4 turned out unnecessary once actually built), §11 for
Phase E's, **§12 for a first round of live-testing fixes** (four real bugs the user found actually
running the build — the slider not appearing, its width, ensigns disappearing while using it, and
the Continue Game button needing outright removal rather than just repointing), **§13 for a
second round** (including the next level in the slider, moving its title beside it, and hiding the
detail panel when a level has no description), **§14 for a third** (removing the slider's title
entirely rather than relocating it, per the user's own second thoughts on §13, and preserving the
preview's pan/zoom across a slider move instead of resetting it), and **§15 for a fourth** (§13's
own "hide the panel when there's no description" fix turned out to also hide the level's *name* —
the one thing §14 made this panel the sole place showing — once there was no description to gate
on; the panel now gates on the name being present instead).
See [00-overview.md](../00-overview.md) and
[04-phase2-landview-panel-investigation.md](04-phase2-landview-panel-investigation.md) for related
Land View investigation.

## 1. The ask, verbatim (as refined across three rounds of feedback)

1. Store campaign progress for **all** campaigns, not just the currently active one, as a plain
   text file consistent with the project's other `.cfg` files — `save/progress.cfg`, one
   `[campaign.cfg]`-keyed section per campaign, extended (round 3) to also cover everything
   `fx1contn.sav`'s `IntralevelData` used to: `unlocked_levels`, `transfer_creature`,
   `bonus_available`, and other campaign flags. See §3.1.
2. Land View should show unlocked levels' ensigns, selectable — not just the single current one.
   Once unlocked, a level stays unlocked permanently, unless the player uses Reset Progress (#5).
3. A slider on the **Campaign Select screen** (`FeSt_CAMPAIGN_SELECT`, the already-ImGui-migrated
   master-detail screen), placed between the land preview panel and the detail textbox, to scroll
   through the different `LAND_VIEW` graphics each **unlocked** level in the campaign defines —
   later, not-yet-reached levels' art is deliberately excluded (it depicts world state and would
   spoil what's ahead).
4. Retire the "Continue Game" flow as a direct-to-gameplay shortcut **for the new menu only**: the
   Main Menu's Continue button routes to Campaign Select instead of loading a save and jumping
   straight into gameplay. Since this is tied to the new menu, `-classicmenu` keeps today's
   `fx1contn.sav`-based Continue flow completely unchanged, as its own independent path — not
   merely "unaffected" but deliberately gated so the two never interfere with each other.
5. A "reset all campaign progress" option in the Game options menu — resets every campaign's
   progress at once, not one at a time.

## 2. Current behaviour (traced, not assumed)

**One campaign, one binary save.** `fx1contn.sav` (`continue_game_filename`, `game_saves.c:63`) is
a single fixed-size binary record — `char cmpgn_fname[64]` (`CAMPAIGN_FNAME_LEN`,
`config_campaigns.h:43`), one `LevelNumber continue_level_number`, one `struct IntralevelData`
(transferred creatures, bonus flags, campaign flags — `game_merge.h:98`) — written wholesale with
`Lb_FILE_MODE_NEW` (truncate) by `save_continue_game()` (`game_saves.c:748`) every time a
singleplayer level is won or quit (`frontend_save_continue_game`, `frontend.cpp:1833`). Starting or
continuing a *different* campaign overwrites this file outright.

**Land View shows only the next level, not everything you've unlocked.**
`update_ensigns_visibility()` (`front_landview.c:175`) sets every level's ensign to `LvSt_Hidden`,
then makes **only** `get_continue_level_number()`'s own level `LvSt_Visible` — except once
`lvnum == SINGLEPLAYER_FINISHED` (the whole campaign is done), when every level becomes visible
(`show_all_sp`) for free replay. This matches original DK's own linear-progression design; the ask
is a deliberate, KeeperFX-only departure from it, not a bug fix.

**Two separate "land view" code paths exist, not one** (this was under-distinguished in this doc's
first draft):

- **Post-mission Land View** (`FeSt_LAND_VIEW`, `frontmap_load()`, `front_landview.c:~1090`) —
  loads `get_continue_level_number()`'s own art via `load_map_and_window()`. This is the
  full-screen legacy screen the player lands on after finishing/quitting a mission; it's what "the
  current continue landview just picks the latest one" (the user's phrasing) refers to.
- **Campaign Select's preview panel** (`land_preview_load()`, `frontmenu_landpreview.c:393`,
  called from `frontend_campaign_select_by_index()`, `frontmenu_select.c:354`) — currently always
  loads `SINGLEPLAYER_NOTSTARTED`, which `load_map_and_window()` (`front_landview.c:905`) resolves
  to the *campaign's own* `land_view_start`/`land_window_start` bookend image, regardless of
  progress. This is the screen the new slider (§3.3) lives on — it's already ImGui
  (`frontgui_campaignselect_frame()`, `frontgui_screens.cpp:686`), so the slider needs no legacy
  UI work at all.

**`LAND_VIEW` is a per-level, two-file directive.** `cmpgn_map_commands[]`'s `LAND_VIEW` (value 8,
`config_campaigns.c:993`) reads two parameters straight into `struct LevelInformation`
(`config_campaigns.h:142`): `lvinfo->land_view` (the background art) and `lvinfo->land_window`
(a window/frame graphic paired with it) — one such pair per level that defines one. The campaign
itself separately carries bookend-only `land_view_start`/`_end` pairs
(`cmpgn_common_commands[]`, values 7/8) shown before the first level and after the last.

**Campaigns are enumerable today.** `campaigns_list` (`frontmenu_select.c`, populated by scanning
`campgns/*.cfg` for `FeSt_CAMPAIGN_SELECT`) already gives every known campaign's `fname` (unique
id, the same string `.sav`/`.cfg` files use to identify a campaign) and `display_name`.

**No "reset progress" affordance anywhere**, and the settings schema (`config_settingschema.h`,
Phase G) has no row type for a fire-and-forget action — `SOptT_Bool`/`Int`/`Enum` all read and
write a value; none represent "do a thing when clicked."

## 3. Design

### 3.1 `save/progress.cfg` — `[section]`-per-campaign, in the project's own `.cfg` idiom

**Revised twice**: the first draft proposed a binary keyed blob; round 2 corrected that to a plain
line-per-campaign text format; round 3 corrected the shape again, to an actual `[section]`/
`KEY = value` layout (round 2's misgiving about that shape not fitting was wrong — it fits fine,
and is a better match for "consistent with the project's other `.cfg` files" than a bespoke line
grammar). One `[campaign.cfg]` section per campaign, keyed by `campaign.fname` exactly as
`campaigns_list` and the old `fx1contn.sav` already do:

```ini
[keeporig.cfg]
unlocked_levels = 1 2 3 4 5 6 7 8 9 10
transfer_creature = HORNY 10
bonus_available = 105
next_level = 11
campaign_flag = 0 3
ensign_override = 7 2
```

Parsed with the same generic block/command machinery `config_campaigns.c` already uses for
`campgns/*.cfg`'s own `[map00001]`-style per-level blocks: `find_conf_block()` locates
`[<campaign.fname>]`, then a new `NamedCommand` table + `case N:` switch (mirroring
`cmpgn_map_commands[]`'s own shape) reads each key inside it. This is a smaller, purpose-built
table, not a reuse of `cmpgn_map_commands[]` itself — the keys don't overlap.

**Key-by-key mapping from the old binary `struct IntralevelData`** (`game_merge.h:98`), so nothing
it tracked is silently dropped:

| New key | Old field | Notes |
|---|---|---|
| `unlocked_levels` | *(new — derived from `continue_level_number` during migration)* | Space-separated `LevelNumber` list, replaces "highest reached" with an explicit set (§3.2) — not necessarily sorted, so a bonus level cleared out of sequence needs no special-casing. |
| `transfer_creature` | `transferred_creatures[PLAYERS_COUNT][TRANSFER_CREATURE_STORAGE_COUNT]` | One line per creature kind carried over (repeated key, same idiom `campgns/*.cfg` already allows for repeatable directives) — creature kind name + count. Only the human player's slot matters for a singleplayer campaign's own progress file, so the `PLAYERS_COUNT` dimension is dropped rather than encoded. |
| `bonus_available` | `bonuses_found[BONUS_LEVEL_STORAGE_COUNT]` | One line per unlocked-but-unplayed bonus level's `LevelNumber` (repeated key). **Corrected during implementation (§7)**: stored as a plain `LevelNumber` array on `struct CampaignProgressEntry` itself, not synced into `IntralevelData.bonuses_found`'s bitset -- that bitset's bit index is only meaningful relative to whichever campaign is the *currently active* global `campaign` (`storage_index_for_bonus_level()`, config.h), which isn't guaranteed to be this entry's own campaign while reading/writing progress.cfg for one that isn't active right now. |
| `next_level` | `next_level` | Kept as its own key rather than folded into `unlocked_levels`, since the old field's exact semantics (script-driven branching vs. plain linear progression) need a closer read during implementation, not assumed here. |
| `campaign_flag` | `campaign_flags[PLAYERS_FOR_CAMPAIGN_FLAGS][CAMPAIGN_FLAGS_PER_PLAYER]` | One line per non-zero flag (repeated key): `<flag index> <value>`. Same human-player-only simplification as `transfer_creature`. |
| `ensign_override` | `ensign_overrides[ENSIGN_OVERRIDES_COUNT]` (`struct LevelEnsignOverride`, `game_merge.h:89`: `lvnum`/`active`/`ensign_type`) | One line per **active** override (repeated key): `<lvnum> <ensign_type>` — `active` is implied by the line's presence, so an inactive slot writes nothing. **Confirmed (round 4): preserve everything `IntralevelData` held**, so this is a kept key, not a dropped one. |

Read/written via the existing `.cfg` line/block I/O primitives (`find_conf_block`,
`get_conf_parameter_single`, `skip_conf_to_next_line`) — no new parsing infrastructure, just a new
table and switch, the same shape every other `.cfg`-backed config in this codebase already has.

**Migration**: on first load under the new (non-`-classicmenu`) menu, if `progress.cfg` has no
section for the campaign named in `fx1contn.sav`, read the old file with the existing
`read_continue_game_progress()` shape and synthesize one: `unlocked_levels` = every level from
that campaign's first singleplayer level up to (not including) the stored `continue_level_number`,
`next_level` = `continue_level_number` itself, and the `IntralevelData` fields mapped per the table
above. This isn't a one-shot event gated on "first ever run" — see §3.4's own note on why it needs
to run as an ongoing reconciliation instead, given `-classicmenu` and the new menu can coexist
across different runs of the same install.

### 3.2 Land View: show every completed level (unchanged reasoning, still legacy code)

Change `update_ensigns_visibility()`'s mid-campaign branch: instead of marking only
`get_continue_level_number()`'s level visible, mark every level in `unlocked_levels` (§3.1) as
`LvSt_Visible`, plus `next_level` (so there's always a next, playable, not-yet-completed level
shown too — matching what "continue" has always meant). Bonus levels keep their own existing
`is_bonus_level_visible()` gate, unchanged. **Confirmed**: once a level is unlocked it stays
unlocked permanently — replaying and losing it never removes it from `unlocked_levels`; only
Reset Progress (§3.5) clears it.

Clicking an already-completed (not just the "next") level's ensign should replay it — needs
checking whether `frontmap_input`'s click-to-start-level path (`front_landview.c`) already works
for any `LvSt_Visible` ensign or silently assumes "the visible one is always the continue level"
somewhere; not confirmed yet, flagged for the implementation step rather than assumed here.

### 3.3 Campaign Select's land-view-graphics slider

**Relocated from the first draft's Land View slider** — the user placed it on Campaign Select
instead, between the preview panel and the detail panel:

```cpp
draw_land_preview_panel(ImVec2(ImGui::GetContentRegionAvail().x, content_h * 0.75f));
// <-- new slider goes here
draw_select_detail_panel(land_selection_highlighted_campaign, content_h * 0.20f);
```

(`frontgui_campaignselect_frame()`, `frontgui_screens.cpp:725-726`.) Since this screen is already
ImGui, the slider is a plain `frontgui_widgets.h` control (an `ImGui::SliderInt` wrapper, or
reusing whatever this codebase's existing slider primitive is for settings rows) — no legacy UI
work needed at all, unlike §3.2/§3.4.

**Confirmed restriction, extended in §13**: the slider steps through levels in that campaign's
`unlocked_levels` (§3.1) *plus the one level immediately next* (not yet completed, but the one the
player is about to play — not a spoiler the way a level further ahead would be), that also define
their own `LAND_VIEW` art, in campaign order — not every level the campaign defines. Land View art
depicts world state (what the dungeon/land looks like at that point in the story), so surfacing a
level beyond that would spoil it; this is a deliberate content-gating decision, not just a UI
convenience. Each slider step re-calls
`land_preview_load(&land_preview, <that level's lvnum>, true)` to swap the preview art
(**corrected in §12**: `show_ensigns` isn't about the source-image-vs-overview distinction this
draft originally assumed — passing `false` hides every ensign outright, not just repositions them,
and these per-level images are variants of the same campaign map the ensign coordinates were
authored against, so they should stay visible and correctly placed at every slider position) — this
is a browse-only preview; it does not change which campaign is "active" or select a level to play,
those still happen via the existing highlight/Enter Land flow. Default position on first
highlighting a campaign: the most recently completed level, falling back to the campaign's own
`land_view_start` bookend (today's only behaviour) if the campaign has no unlocked levels yet.
**Corrected in §14**: the slider itself carries no name label at all (round two, §13, tried a label
beside it; the user reconsidered — it read as a second, seemingly-different answer next to
`draw_select_detail_panel()`'s own title even though the two weren't actually the same value at the
time). `draw_select_detail_panel()` now falls back to the slider's current level when nothing is
separately ensign-hovered, so there is exactly one place the name shows. Moving the slider also now
preserves the preview's pan position and zoom instead of resetting to the image's own default view.

### 3.4 Continue Game (history: was two paths gated on `-classicmenu`, now just one)

**Updated 2026-09-12 — `-classicmenu` retired.** This section originally described a permanent
fork gated on `use_classic_menu()`: the legacy frontend kept reading/writing `fx1contn.sav`
completely unchanged, while the new menu branched to `progress.cfg` instead, and a player could
alternate between the two across different launches of the same install. That fork is gone now
that the frontend has no legacy sprite menus left to fall back to (`-classicmenu` retired outright
-- see `docs/refactor/ingame-gui/00-overview.md` §1/§8; the in-game HUD's own, unrelated classic
mode lives on via `GUI_ICON_PACK=CLASSIC` instead, but that's a HUD-rendering choice, not a
Continue-Game code path).

Current shape, single path only:

- `frontend_save_continue_game()` always calls `campaign_progress_record_level_completed()`
  (`progress.cfg`, §3.1). `save_continue_game()` (the old `fx1contn.sav` writer) is deleted —
  its only caller was the retired classic-menu branch.
- `frontend_load_continue_game_resolve()` always returns `FeSt_CAMPAIGN_SELECT` — no pre-selected
  campaign needed, entering that state already builds its own list fresh
  (`frontend_campaign_list_load()`), exactly mirroring the Main Menu's "Campaign" button. The
  Main Menu's own "Continue Game" button was already removed outright in the new menu's own draw
  path (§12) once both buttons resolved to the same destination. `load_continue_game()` (the old
  `fx1contn.sav` reader used only to populate gameplay state for `FeSt_LAND_VIEW`) is deleted for
  the same reason `save_continue_game()` is.
- `continue_game_available()` (`game_saves.c`) is unconditionally the `progress.cfg`-backed check
  now: "does `progress.cfg` have at least one campaign with a non-empty `unlocked_levels`."

**One function survives from the old path on purpose**: `read_continue_game_progress()`
(`game_saves.c`) still exists, because `reconcile_fx1contn_into_progress()`
(`game_campaign_progress.c`) still needs it — a player upgrading from before this migration may
have real progress sitting only in `fx1contn.sav`, and that one-time (well, every-load, cheap,
idempotent) absorption into `progress.cfg` is still exactly as necessary as it always was. This
is migration-of-old-saves, not a live dual-path fork — the distinction §3.1 draws.

### 3.5 Reset Progress action

New `SOptT_Action` row type: adds one field to `struct SettingOption`,
`void (*on_action)(void);`, meaningful only for this type; the generic renderer
(`draw_setting_options_for_category`, `frontgui_screens.cpp`) draws an `FeButton(label)` for it
instead of a checkbox/combo/slider. Confirmation (real, permanent data loss if confirmed) is a
generic gate in the renderer itself, not per-row: clicking any `SOptT_Action` button only *arms* a
single shared "pending action" pointer; a separate, always-drawn confirm modal
(`FeBeginModal`/`GUIStr_ConfirmYouSure`/Yes/No, matching this codebase's existing confirm-dialog
strings) is what actually calls `on_action()`, on Yes. This makes every future `SOptT_Action` row
confirm-gated automatically, not just this one — see §11 for the actual implementation. `on_action`
itself reaches `reset_all_campaign_progress()` (kfx_game) through a new
`ConfigReloadCallbacks.reset_campaign_progress` field (`config.h`) -- the same cross-layer pattern
every other "kfx_config needs something from above" case in this schema already uses, not a bespoke
callback struct for this one row. **Confirmed scope**: one button, resets every campaign's progress
at once (clears `progress.cfg` entirely) — not a per-campaign reset.

## 4. Explicitly out of scope for this doc

- `-classicmenu`'s own code — was, at the time this doc was written, deliberately gated off from
  every change here (§3.4's original text). Retired outright since (§3.4's 2026-09-12 update) —
  this bullet is historical, not a live constraint any more.
- Per-level replay unlocking *within* a single mission (secrets, alternate objectives) — out of
  scope; this is about which **levels'** ensigns are visible, not intra-level content.
- Reworking `IntralevelData`'s own semantics beyond what §3.1's mapping table already settles —
  every field it held (including `ensign_overrides`) is preserved by an explicit key, per the
  user's instruction to expand the format as needed rather than drop anything.

## 5. Phasing

- **A — `progress.cfg` parser + read/write + reconciliation. Landed, see §7.** New `[section]`
  format and its `NamedCommand` table (§3.1), the ongoing (not one-shot) `fx1contn.sav`
  reconciliation, and a `use_classic_menu()` branch at the *save* call site only
  (`frontend_save_continue_game()`) -- deliberately additive rather than exclusive during this
  transition (§7 explains why `continue_game_available()`/`frontend_load_continue_game_resolve()`
  stay fully unbranched until D lands alongside them). No visible behaviour change under either
  menu.
- **B — Land View shows every unlocked level. Landed, see §8.** Needs A. `update_ensigns_visibility()`
  changed to read `unlocked_levels`/`next_level` under the new menu (`-classicmenu` unchanged);
  click-to-replay for a non-current level needed no fix at all -- `clicked_map_level_ensign()` was
  already generic over whichever ensign is `LvSt_Visible`, confirmed by reading it rather than
  assumed.
- **C — Campaign Select slider. Landed, see §9.** Needs A (an `unlocked_levels` list to restrict
  and default the slider from) but is otherwise independent of B — pure ImGui addition to an
  already-migrated screen.
- **D — Continue button retirement. Landed, see §10.** Needs A. Independent of B/C otherwise.
- **E — Reset Progress action. Landed, see §11.** Needs A (something to reset) and the new
  `SOptT_Action` schema type. Independent of B/C/D.

## 6. Open questions before implementation starts

None remaining. Every question raised across four rounds of feedback is resolved in §1-§5 above,
including `ensign_overrides` (§3.1's table) — nothing here should be blocking phase A.

## 7. Phase A implementation notes

New files `src/kfx_game/src/game_campaign_progress.c` / `include/game_campaign_progress.h`, in
`kfx_game` (not `kfx_frontend`) because everything this phase reads/writes --
`struct IntralevelData`, `campaign`/`campaigns_list`, `use_classic_menu()` -- is already reachable
from `kfx_game` without new cross-layer plumbing, and `game_saves.c`'s own `fx1contn.sav` functions
(the thing this eventually replaces) already live there. `read_continue_game_progress()`
(`game_saves.c`) was made non-`static` and declared in `game_saves.h` so
`reconcile_fx1contn_into_progress()` could reuse it exactly rather than re-implementing the same
fixed-layout read.

**Parser/writer**: `find_conf_block()`/`recognize_conf_command()`/`get_conf_parameter_*` (the same
`config.h` primitives `config_campaigns.c` uses for its own `[map00001]` blocks) power a small
`progress_cfg_commands[]` table with one `case` per key from §3.1's mapping table. The writer is a
wholesale regeneration (open, write every in-memory entry's lines, close) rather than an in-place
edit -- this format has no unrelated content worth preserving around it, unlike `keeperfx.cfg`'s
writer.

**Two real bugs caught before they shipped, both the same root cause**: `IntralevelData`'s
`bonuses_found[]` field is a *bitset* whose bit index (`storage_index_for_bonus_level()`,
`config.h`) is only meaningful relative to whichever campaign is the **currently active** global
`campaign` -- not necessarily the campaign a given `struct CampaignProgressEntry` actually belongs
to while bulk-loading every campaign's progress at once, or while reconciling a campaign that isn't
the one currently loaded. An early draft called `set_bonus_level_visibility()`/
`get_level_ensign_override()` (both of which read/write the *global* `intralvl`, not the per-entry
one) directly from the parser -- caught by re-reading `game_merge.c`'s own implementation before
trusting the first draft, not by a test failure. Fixed two ways: `ensign_overrides` already had an
absolute, self-describing key (`lvnum` inside each record) needing no campaign context, so the fix
there was simply operating on `entry->intralvl.ensign_overrides[]` directly instead of the global
accessor functions. `bonus_available` couldn't be fixed the same way -- its *only* representation
was the context-relative bitset -- so it got a genuinely new field instead: `struct
CampaignProgressEntry.bonus_available[]`, a plain `LevelNumber` array storing the raw level numbers
directly, populated only at the one point `campaign`/`intralvl` are guaranteed to correspond to the
right entry (`campaign_progress_record_level_completed()`, called right after a level completes).
This is the one place this phase deviates from §3.1's original table shape -- corrected there too.

**Known, deliberately-accepted limitation**: `reconcile_fx1contn_into_progress()` copies old
`fx1contn.sav`'s entire `IntralevelData` wholesale (simplest correct option for every field
*except* bonuses), which means any bonus-level availability recorded under the *old* Continue flow
is not carried into the new `bonus_available[]` array during migration -- decoding it correctly
would need this campaign loaded as the active one first, which reconciliation deliberately avoids
(it's meant to be cheap and side-effect-free). A `WARNMSG` fires when this is actually the case
(non-zero old bonus bits found) rather than silently dropping the data unnoticed. Fixing this
properly is deferred rather than blocking Phase A on it -- flagged here for whoever picks up
Phase B onward to decide whether it's worth revisiting.

**Verification**: all four build configs, `check_layering.py --strict`, and the full 1547-test
ctest suite (19 new cases, `game_campaign_progress_test.cpp`) pass. Real file I/O
(`load_campaign_progress_file()`/`save_campaign_progress_file()`) is only tested for "no `save/`
directory present" behaviour -- this test binary's environment has none, matching this codebase's
established precedent (`game_heap_test.cpp`'s own note on the same kind of gap for `creature.jty`)
-- so the parser itself is tested by calling `parse_progress_cfg_campaign_block()` directly against
hand-written buffers (exposed non-`static` for exactly this reason, with a comment explaining why).
`TRANSFER_CREATURE` round-trips are not verified end-to-end for the same reason
`game_saves_transfer_test.cpp` doesn't attempt real creature-name resolution: `parse_creature_name()`
resolves names against `creature_desc[]`, populated from loaded creature config data that doesn't
exist in this environment -- only the graceful-rejection path is tested for that key.

Not yet verified interactively (no visible behaviour change to check yet -- this phase is pure
plumbing). Phases B-E build on this; see §5.

## 8. Phase B implementation notes

`update_ensigns_visibility()` (`front_landview.c`) now branches on `use_classic_menu()`: the
classic-menu branch is the original code, byte-for-byte; the new-menu branch reads
`get_campaign_progress(campaign.fname, false)` and marks every level in its `unlocked_levels` plus
`next_level` visible, falling back to `get_continue_level_number() == SINGLEPLAYER_FINISHED` (the
old signal) if no progress.cfg entry exists yet this session (e.g. before reconciliation has had a
chance to run). A small `mark_ensign_visible_if_present()` helper was pulled out since both
branches (and the always-on bonus/extra-level code below them) needed the same
"look up, set state if found" step.

**Verified rather than assumed**: the design doc's own §3.2 flagged "does clicking an
already-completed level's ensign actually replay it, or does something silently assume the visible
one is always the continue level" as unconfirmed. Reading `clicked_map_level_ensign()`
(`front_landview.c`) settled it: it operates purely on `mouse_over_lvnum` (whichever `LvSt_Visible`
ensign was hovered, from `frontmap_input_active_ensign()`) with no reference to
`get_continue_level_number()` anywhere -- already proven generic by the pre-existing
"campaign finished, show every level for free replay" (`show_all_sp`) path. No code changes were
needed there at all.

**Second correction to Phase A's own code, found while wiring this phase in**:
`campaign_progress_record_level_completed()` rejected every non-positive `LevelNumber`, including
`SINGLEPLAYER_FINISHED` (`-1`) -- meaning finishing an entire campaign under the new menu would
never actually record that fact into `next_level`, silently leaving Land View's "show every level"
fallback path unreachable except via the old `fx1contn.sav` signal. Fixed to special-case
`SINGLEPLAYER_FINISHED` as a real, meaningful value (skip adding it to `unlocked_levels`, since it
isn't a real level, but still record it as `next_level`).

**Not unit-tested**: `update_ensigns_visibility()` itself, consistent with this codebase's own
established precedent for `front_landview.c`/`frontmenu_landpreview.c` --
`frontmenu_landpreview_test.cpp`'s own header comment explicitly declines to test anything
"touch[ing] real campaign state, sprite sheets, or the active renderer palette," which is most of
what this function calls (`get_level_info`/`get_first_level_info` need `campaign.lvinfos`
populated, `is_bonus_level_visible`/`bonus_level_for_singleplayer_level`/`get_extra_level` all need
a real loaded campaign). Confidence instead comes from: the underlying `CampaignProgressEntry`
behaviour is already covered by Phase A's own 20 tests, the branch added is small and was read
line-by-line against the existing (also untested) legacy branch it sits beside, and
`clicked_map_level_ensign()`'s generic behaviour was confirmed by reading it rather than assumed.

Verification: all four build configs, `check_layering.py --strict`, and the full 1548-test ctest
suite (1 new case, for the `campaign_progress_record_level_completed` fix) pass. Not yet verified
interactively -- Land View is legacy-rendered either way (this change doesn't touch
`-classicmenu`'s own code path at all), so there's nothing new to check visually without actually
playing through unlocking a second level under the new menu.

## 9. Phase C implementation notes

`draw_landview_slider()` (new, `frontgui_screens.cpp`, right after `draw_land_preview_panel()`) is
wired into `frontgui_campaignselect_frame()` only, between the preview panel and the detail panel
exactly as specified -- **not** the merged Free Play/Skirmish screen
(`frontgui_freeplayselect_frame()`), which has its own, separate `draw_land_preview_panel()` call
site that's untouched. No `use_classic_menu()` gate was needed in the function itself: this whole
screen is already new-menu-only (Campaign Select's ImGui version), so the function is simply never
called from the legacy draw path at all.

Rebuilds its ordered level list (`rebuild_landview_slider_levels()`) only when the highlighted
campaign actually changes (a cached-pointer comparison, not every frame), filtering
`campaign.single_levels[]` down to levels that are both in `unlocked_levels` (Phase A/B's own data)
*and* define their own `LAND_VIEW` art -- per §3.3's explicit content-gating rule, a not-yet-reached
level's art is excluded even if the level itself were somehow unlocked, since the art depicts world
state and would spoil it (this is naturally satisfied anyway: a level isn't in `unlocked_levels`
until completed). Reuses the existing `FeSlider` float-slider wrapper (the same one Phase G's int
settings rows use, `%.0f` format) rather than adding a new int-specific widget, rounding the float
back to an index on change.

**One deliberate behaviour trade-off, flagged rather than silently accepted**: `FeCaption` shown
above the slider is the *level's* display name, not any land-view-specific label -- there's no
separate "which land view is this" string in `struct LevelInformation`, so the level name is what a
player would recognize the view by. This means highlighting a campaign now shows the *most
recently unlocked level's* art (no ensigns, `show_ensigns=false` per §3.3) as soon as it's
highlighted, rather than the campaign's own `land_view_start` bookend art with ensigns that
`frontend_campaign_select_by_index()` still loads first (its own `land_preview_load(&land_preview,
SINGLEPLAYER_NOTSTARTED, true)` call runs earlier in the same frame, then this slider's own default
load overrides it before anything is actually drawn) -- exactly what §3.3 specifies ("default...
the most recently completed level"), but worth flagging explicitly since it changes what a
just-highlighted, already-in-progress campaign visually opens on. A campaign with no progress yet
is unaffected (falls through to `land_view_start`, since `s_landview_slider_levels` stays empty).

**Not unit-tested**, for the same reason as Phase B's `update_ensigns_visibility()`: this is ImGui
drawing code touching real campaign/sprite/renderer state, the class of code
`frontmenu_landpreview_test.cpp`'s own header comment already declines to cover, and no test file
exists for `frontgui_screens.cpp` at all to extend. `rebuild_landview_slider_levels()`'s filtering
logic is a plausible future extraction point if this ever needs isolated coverage, but wasn't
pulled out for testing alone, matching this file's own existing convention of keeping its helpers
file-internal.

Verification: all four build configs, `check_layering.py --strict`, and the full 1548-test ctest
suite (no new cases -- see above) pass. Not yet verified interactively.

## 10. Phase D implementation notes

Three call sites branch on `use_classic_menu()`, matching §3.4 exactly:

- `continue_game_available()` (`game_saves.c`): new-menu branch calls
  `load_campaign_progress_file()` + `reconcile_fx1contn_into_progress()` + the new
  `any_campaign_progress_exists()` (added to `game_campaign_progress.{h,c}` for this -- a plain scan
  of the in-memory table, exposed rather than iterated ad hoc from `game_saves.c` to keep the
  table's own storage private to the module that owns it).
- `frontend_load_continue_game_resolve()` (`frontend.cpp`): new-menu branch is a single
  `return FeSt_CAMPAIGN_SELECT;`.
- `frontend_save_continue_game()` (`frontend.cpp`): now genuinely either/or --
  `use_classic_menu() ? save_continue_game(lvnum) : campaign_progress_record_level_completed(lvnum)`
  -- Phase A's own additive version (calling both) is retired now that the read side is branched
  too; keeping both writes going would have been dead weight with nothing to lose by removing.

**Simplification found only by reading the code being mirrored, not assumed from the design doc's
own speculative language**: §3.4's first-drafted text (during the design phase, before any
implementation) said the new-menu branch should resolve to `FeSt_CAMPAIGN_SELECT` "pre-highlighting
whichever campaign has the most recent progress." Building it surfaced two problems with that: (1)
`struct CampaignProgressEntry` has no recency/timestamp field to answer "most recent" from at all,
and (2) it turned out to be solving a problem that doesn't exist -- reading
`frontend_start_new_game_resolve()` (the Main Menu's "Campaign" button, `frontend.cpp:~1780`) showed
it already returns bare `FeSt_CAMPAIGN_SELECT` with zero pre-arranged state whenever more than one
campaign exists, and `frontend_campaign_list_load()` (run on entering that state) already builds a
correct, fresh list and list-selection state every time regardless of how the screen was reached.
Continue Game's own resolve function now does exactly the same one-line thing, for the same reason:
the screen doesn't need help. §3.4's own text (above) has been corrected to match what actually
shipped rather than left describing the abandoned idea.

**Not unit-tested**: `frontend_load_continue_game_resolve()`/`frontend_save_continue_game()`
(`frontend.cpp`) -- both call deeply into live gameplay/frontend state
(`get_players_dungeon`/`clear_game_for_save`/`frontend_set_state` etc.) with no existing test
coverage for this file at all to extend, the same class of gap as Phases B/C. `continue_game_available()`
(`game_saves.c`), by contrast, *is* covered -- it's a much thinner function, and its new-menu branch
is a direct, already-isolated composition of three already-tested Phase A/D functions. New tests
(`game_campaign_progress_test.cpp`, alongside the rest of this feature's own tests since they
exercise `any_campaign_progress_exists()` and `continue_game_available()`'s new-menu branch
together): the "false" case (no progress, no `fx1contn.sav`) is verified directly; the "true" case
turned out **not** testable in this environment either -- `continue_game_available()` itself calls
`load_campaign_progress_file()`, which unconditionally resets the in-memory table before re-reading
from disk, so any entry set up directly in a test (bypassing real persistence) is wiped by that same
call before the check ever runs. Flagged in the test file's own comment rather than silently
dropped; this matches real usage correctly (progress only "counts" once actually persisted via
`campaign_progress_record_level_completed()`, which does write to disk) -- it's a gap in what this
sandboxed environment can exercise, not a gap in the logic itself.

Verification: all four build configs, `check_layering.py --strict`, and the full 1551-test ctest
suite (4 new cases) pass. Not yet verified interactively -- this is the first phase with a
player-visible behaviour change to check (Continue Game's actual destination under the new menu),
worth confirming live before Phase E.

## 11. Phase E implementation notes

`enum SettingOptionType` gains `SOptT_Action`; `struct SettingOption` gains `on_action`. The Reset
Progress row itself has no `cfg_key` (nothing to write to `keeperfx.cfg` -- it's not a stored value
at all), which needed one small fix to the schema's own shape test
(`config_settingschema_test.cpp`'s "covers all four categories" case): its blanket
`REQUIRE(opt.cfg_key != nullptr)` now exempts `SOptT_Action` rows explicitly rather than assuming
every row has one.

**Confirmation is generic, not bespoke to this row**: `draw_pending_action_confirm_modal()`
(`frontgui_screens.cpp`) is one shared modal, gating every `SOptT_Action` button uniformly -- a
click only sets a static `s_pending_action_option` pointer; the modal (drawn unconditionally at the
end of `frontgui_options_frame()`, the same "always call, check internally" shape
`draw_error_box_overlay()` already uses elsewhere in this file) shows the row's own help text plus
`GUIStr_ConfirmYouSure`/Yes/No, and only calls `opt->on_action()` if Yes is clicked. This means any
future `SOptT_Action` row added to the schema is confirm-gated for free, without repeating this
logic per row.

**Cross-layer reach, same pattern as every other row needing one**: `config_settingschema.c`
(`kfx_config`) can't call `reset_all_campaign_progress()` (`kfx_game`) directly -- `kfx_game` ranks
above `kfx_config` in the layering. Rather than inventing a bespoke callback struct for one
function, it was added as `ConfigReloadCallbacks.reset_campaign_progress` (`config.h`), following
the exact convention this schema already relies on for `SCREENSHOT`/`HAND_SIZE`/`POINTER_SENSITIVITY`
etc.: a new field in the existing struct, a noop stub in `config.c`'s `default_config_reload_callbacks`
(same position in both), and the real implementation wired in `main.cpp`'s
`config_reload_callbacks_impl` (same position again) -- `reset_all_campaign_progress()`'s own
signature (`void(void)`) already matches the callback field exactly, so it's passed directly with
no wrapper needed, the same shape `script_strdup`/`script_strval` already use.

**Not gated on `-classicmenu`, deliberately not needed**: the legacy Options screen never reads
`setting_options[]` at all -- it has its own hand-written sprite-drawn controls -- so this row (and
the confirm modal) simply never renders there. No runtime check was added for it, matching how
every other schema row already works.

**Not unit-tested**: `draw_pending_action_confirm_modal()`/the button-click wiring in
`draw_setting_options_for_category()` (`frontgui_screens.cpp`), same class of gap as Phases B/C/D --
ImGui drawing code with no existing test file to extend. What *is* covered (`config_settingschema_test.cpp`):
the row's own shape (type, category, no `cfg_key`, `on_action != nullptr`) and that `on_action`
correctly reaches `ConfigReloadCallbacks.reset_campaign_progress` when called directly (bypassing
the ImGui button/modal entirely, the same "test the schema logic, not the drawing" split every
other row's tests already use) -- plus the reload-callbacks noop-default smoke test
(`config_reload_callbacks_test.cpp`).

Verification: all four build configs, `check_layering.py --strict`, and the full 1553-test ctest
suite (2 new cases) pass. Not yet verified interactively -- along with Phase D's Continue routing,
this is the other player-visible change worth checking live: does the button show up on the Game
tab, does the confirmation dialog appear and correctly cancel/confirm, and does progress actually
clear.

## 12. Live-testing fixes (post Phase E)

Four issues from the user's own hands-on testing of the built game, not caught by anything in
Phases A-E's own (necessarily code-review-only, no live desktop access) verification:

1. **The slider wasn't appearing at all.** Root cause: `rebuild_landview_slider_levels()`
   (`frontgui_screens.cpp`) called `get_campaign_progress(campgn->fname, false)` without first
   calling `load_campaign_progress_file()` -- it silently depended on *something else* (in
   practice, `continue_game_available()`'s own new-menu branch, Phase D) having already loaded
   `save/progress.cfg` into memory earlier in the session. Reaching Campaign Select by a path that
   never triggered that load left the in-memory table empty, so `get_campaign_progress()` always
   returned `NULL` and the slider silently declined to show anything. Fixed by making the slider
   self-sufficient: it now calls `load_campaign_progress_file()` +
   `reconcile_fx1contn_into_progress()` itself, on every highlight change (not every frame, so the
   cost is bounded the same way it already was).

   A second, related bug surfaced while fixing this: the slider's own *default* index (the most
   recently unlocked level, per §3.3) was computed but never actually applied to the displayed
   preview -- only dragging the slider at least once called `land_preview_load()`. Highlighting a
   campaign kept showing `frontend_campaign_select_by_index()`'s own initial `land_view_start` load
   until manually touched. Fixed by calling `land_preview_load()` once, right after computing the
   default index, inside `rebuild_landview_slider_levels()` itself.

2. **The slider was too wide.** `FeSlider` (a thin `ImGui::SliderFloat` wrapper) has no size
   parameter of its own and defaults to ImGui's own full-column width via `CalcItemWidth()`. Fixed
   with `ImGui::SetNextItemWidth(160.0f)` immediately before the `FeSlider()` call -- the one-off,
   narrow way to size a single widget without changing `FeSlider`'s own signature or affecting
   anything else that calls it.

3. **Using the slider made every ensign disappear.** Root cause: the slider's own
   `land_preview_load(..., false)` call for `show_ensigns` -- this was §3.3's own original,
   untested assumption, based on `land_preview_load()`'s header comment ("pass `show_ensigns = true`
   only when target_lvnum's image is the shared campaign overview the ensign coordinates were
   authored against"), read as "any other target_lvnum should pass `false`." That comment describes
   a real constraint (ensign x/y coordinates are only valid against the image they were authored
   for) but was written for a different, pre-existing use case (Free Play/multiplayer level
   preview, where a level's own land_view is a wholly unrelated image with no shared coordinate
   space) -- not this one, where each campaign level's own land_view is itself a variant of the
   *same* campaign map the ensigns were placed on, sharing its coordinate space. `show_ensigns`
   doesn't just reposition ensigns for a mismatched image, either -- `LandPreviewPanel.show_ensigns`
   gates `land_preview_draw()`'s entire ensign-drawing pass, so `false` hid them outright rather than
   drawing them in a wrong position (which would have been a different, more forgivable bug).
   Fixed by passing `true` in both of the slider's `land_preview_load()` calls (the default-position
   one added in fix #1, and the on-drag one) -- confirmed against live feedback rather than
   re-guessed from the header comment a second time.

4. **The Main Menu's "Continue Game" button needed outright removal, not repointing.** Phase D
   (§3.4, §10) kept the button in place and only changed its destination. Once both "Continue Game"
   and "Campaign" resolve to the identical `FeSt_CAMPAIGN_SELECT` with no pre-arranged state, having
   two buttons for one destination is redundant, and the user confirmed removal was the original
   intent. Removed the `FEBtn_MnuContinueGame` block from `frontgui_mainmenu_frame()`
   (`frontgui_screens.cpp`) entirely -- `-classicmenu`'s own `frontend_main_menu_buttons[]`
   (`frontend.cpp`) is untouched, keeping its own Continue Game button exactly as it always has.
   `continue_game_available()`'s new-menu branch (Phase D) still runs on every Main Menu entry --
   its `any_campaign_progress_exists()` result isn't consumed by anything in the new-menu UI
   anymore, but the call also runs `load_campaign_progress_file()`/`reconcile_fx1contn_into_progress()`
   as a side effect, which is still worth keeping fresh.

**Not unit-tested**: all four fixes are in ImGui drawing/state code
(`frontgui_screens.cpp`), the same class of gap as Phases B/C/D -- no test file exists for this
file, and these fixes don't introduce any new pure-logic function worth extracting and testing in
isolation the way Phase A's `parse_progress_cfg_campaign_block()` was.

Verification: all four build configs, `check_layering.py --strict`, and the full 1553-test ctest
suite (no new cases) pass. These fixes address the user's own live report directly; re-confirming
them live is the natural next step, along with the Phase D/E items §10/§11 still hadn't had checked.

## 13. Live-testing fixes, round two

Confirmed from round one: ensigns now correctly stay visible while browsing (§12's fix #3 held).
Three more issues from further live testing:

1. **The slider should include the next level too, not just completed ones.** `rebuild_landview_slider_levels()`'s
   filter loop now also admits `lvnum == progress->intralvl.next_level` alongside
   `campaign_progress_has_unlocked_level()` -- the level the player is about to play isn't a spoiler
   the way one further ahead would be (Land View's own ensign for it is already visible, per Phase
   B). Both checks still gate on the level actually defining its own `LAND_VIEW` art, and the loop
   still walks `campgn->single_levels[]` in campaign order, so `next_level` naturally lands at the
   end of the list without any special-casing. §3.3's own restriction text updated to match.

2. **The slider's title duplicated the detail panel's own title, and was too small/on its own
   line.** `draw_landview_slider()` used to draw the current level's name via `FeCaption()` on a
   line above the slider -- `draw_select_detail_panel()` already shows the same name (as
   `FeSubheading()`) in the box below. Moved the name to sit beside the slider
   (`ImGui::SameLine()`) instead of stacking above it, and switched it from `FeCaption` (the
   de-emphasized, smallest text role) to `FeBodyText` (one size up) now that it's the only place
   this name shows while just browsing via the slider rather than hovering a specific ensign.

3. **The detail panel below the preview should hide entirely when a level (or campaign) has no
   `DESCRIPTION` line in its own `.cfg`.** `draw_select_detail_panel()` used to always open the
   bordered child panel once a name was known, showing just the name with an empty box beneath it
   if there was no description. Now returns before `FeBeginPanel()`/`FeEndPanel()` at all when
   `description` is null/empty -- both calls needed skipping together (`imgui.h`'s own
   `BeginChild`/`EndChild` pairing rule: never call `EndPanel()` without a matching `BeginPanel()`),
   not just the text inside them. **Reverted in §15**: gating on `description` rather than `name`
   turned out wrong once §14 removed the slider's own label -- the panel became the *only* place a
   level's name showed at all, and hiding it whenever a level simply had no description also lost
   the name. The panel now gates on `name` again (its original, pre-this-fix condition); a missing
   description alone still just skips that one line, same as before this fix ever landed.

**Not unit-tested**: same reasoning as §12 -- all three fixes are ImGui drawing code in
`frontgui_screens.cpp`, no test file exists for it, and none of the three introduce a new pure-logic
function worth extracting in isolation.

Verification: all four build configs, `check_layering.py --strict`, and the full 1553-test ctest
suite (no new cases) pass.

## 14. Live-testing fixes, round three

The user reconsidered §13's own title-beside-slider fix ("this could be misleading") and asked for
two more changes.

1. **Remove the slider's title entirely, and restore the selected level's name via the detail
   panel instead.** §13's fix moved the level name beside the slider assuming it duplicated
   `draw_select_detail_panel()`'s own title -- but that panel's title was actually driven by
   `land_preview.highlighted_lvnum` (ensign hover) or the campaign fallback, *not* the slider's own
   position, so the two weren't really showing the same thing even though they looked
   confusingly similar side by side. Fixed at the source rather than by relabeling again:
   `draw_landview_slider()` now draws a bare, unlabeled slider, and `draw_select_detail_panel()`
   gained a new fallback tier between ensign-hover and the campaign fallback -- the slider's own
   current level, read directly off the same `s_landview_slider_levels`/`s_landview_slider_index`
   statics `draw_landview_slider()` itself uses (both live in the same anonymous namespace already,
   no new plumbing needed). Net effect: exactly one place shows "what you're looking at," and it's
   now correctly tied to the slider's actual position rather than an unrelated campaign-level
   fallback.

2. **Moving the slider should retain the preview's current pan position and zoom**, not reset to
   the new image's own default view. `land_preview_load()` always resets
   `screen_shift_x`/`screen_shift_y`/`units_per_px` to sane defaults -- correct for its other
   callers (a fresh campaign/level highlight, where starting from a known view makes sense) but not
   for a slider drag, where the player is deliberately browsing art at whatever pan/zoom they
   already set. Rather than changing the shared `land_preview_load()` (used by several callers with
   different needs), the slider's own on-change handler now saves those three fields before calling
   it and restores them after, then calls `land_preview_clamp_shift()` to re-clamp the retained
   shift against the *new* image's own dimensions (a different level's land_view art can be a
   different size, so a shift valid for the old one might be out of bounds for the new one).

**Not unit-tested**: same reasoning as §12/§13.

Verification: all four build configs, `check_layering.py --strict`, and the full 1553-test ctest
suite (no new cases) pass.

## 15. Live-testing fixes, round four

One fix, an interaction between two earlier ones the user caught immediately after §14 landed: "no
description means the detail panel isn't showing, we need it again for the name."

§13's fix #3 gated `draw_select_detail_panel()`'s entire panel on whether `description` was
present, on the (then-true) assumption that a level's name would still show somewhere else even if
the panel itself hid -- which held right up until §14 removed the slider's own label and made this
panel the *only* place a level's name shows while browsing via the slider. The two changes,
individually reasonable, combined into a real regression: a level with a name but no description
(routine -- `DESCRIPTION` is optional in a level's own `.cfg`) now showed nothing at all while
selected via the slider.

Fixed by gating on `name` instead of `description` -- restoring `draw_select_detail_panel()`'s
original, pre-§13 condition. A level with a name but no description now shows the panel with just
the name, same as before §13 ever touched this function; a level with neither still hides the panel
entirely (nothing to show); the description line itself still only draws when non-empty, unchanged
from every version of this function so far.

**Not unit-tested**: same reasoning as §12/§13/§14.

Verification: all four build configs, `check_layering.py --strict`, and the full 1553-test ctest
suite (no new cases) pass.

## 16. Land selection without a land-view picture (campaign editor follow-up)

Building the Campaign Editor's own Land view page (`docs/refactor/editor/fx-plans/08-campaign-editor.md`
§12.6/K7) turned up a real gap in this screen: a campaign whose `LAND_VIEW_START` is unset, or whose
image is missing/broken, leaves `land_preview_load(&land_preview, SINGLEPLAYER_NOTSTARTED, true)`
failing outright at Campaign Select. Its own fallback (`land_preview_build_minimap()`, §3.2's "common
case for an individual freeplay level" path) cannot help here: it builds a minimap from a *level's* own
`map%05d.slb`/`.own` files, and there is no such level for the campaign overview itself
(`SINGLEPLAYER_NOTSTARTED` is not a real level number). `land_preview.loaded` stays `false`, and
`draw_land_preview_panel()`/`draw_landview_slider()` both bail out immediately (their own
`!panel->loaded` / "fewer than 2 stops" early-outs) — the screen showed an empty picture area, but
critically **you could still click "Enter this land"**: `frontend_land_selection_enter_resolve()`
doesn't require a highlighted ensign at all, it falls back to `first_singleplayer_level()` when
`land_preview.highlighted_lvnum == SINGLEPLAYER_NOTSTARTED`. So the screen wasn't broken as in
unusable, just silently unable to let you pick anything other than the first level — no way to jump to
a later unlocked level, replay one, or reach an unlocked bonus/extra level, exactly the situation the
user asked to fix.

Fix (`frontgui_campaignselect_frame()`, `frontgui_screens.cpp`): when a campaign is highlighted and
`land_preview.loaded` is false, the picture/slider column is replaced by a plain scrollable list of the
campaign's levels (`draw_land_selection_level_list()`) instead of the picture. Every single level is
listed, using the same name resolution the detail panel and the ensign hit-test already use
(`name_stridx` else `name`); a locked one (`LevelInformation::state != LvSt_Visible` — the same gate
`land_preview_ensign_at()` uses for which ensigns are even clickable on a working picture) is shown
disabled (`ImGui::BeginDisabled`), not omitted — the player can see the campaign's full level sequence,
just can't select past where they've unlocked. A bonus or extra level is only listed once unlocked
(labelled "(bonus)"/"(extra)"): unlike single levels, these are a discovered reward, and listing a
locked one by name (even disabled) would spoil that it exists before the player finds it — the working
picture never draws an ensign for one either, for the same reason. Selecting a row sets
`land_preview.highlighted_lvnum` exactly like clicking an ensign does (plus the same description-speech
preview, `play_description_speech()`), so "Enter this land" commits it through the existing,
unmodified `frontend_land_selection_enter_resolve()` path — no change needed there.

Free play (`FeSt_MAPPACK_SELECT`) does not need the same fix: it has no campaign-overview picture at
all, only each level's own art (already covered by the minimap fallback) or, missing that, the level
list itself already used as the row source.

**Not unit-tested**: this is a `kfx_frontend` menu-screen change (same class as §12-§15 above) with no
existing ftest harness reaching FeSt_CAMPAIGN_SELECT's ImGui draw path; not live-tested against a real
broken campaign in this session either.

Verification: `kfx_editor_utest`/`kfx_config_utest` (no regressions; this file isn't covered by either
suite), `check_layering.py --strict`, and a clean build of `keeperfx`/`keeperfx_hvlog` (native Linux,
`-DKFX_FUNCTESTING=ON` tree) and the mingw cross-compile.
