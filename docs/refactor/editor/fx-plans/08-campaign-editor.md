# FX plan 08 — campaign and mappack editor

Status: **plan, second pass (§10) with the decisions confirmed (§11).** Nothing built. Depends on [03](03-content-editors-foundation.md) for the
content layer ([10](10-config-content-model-and-writers.md) W1–W4, `CampaignConfigWriter`) and the Tools menu (F0); otherwise independent of the entity editors and can be
built in parallel with them.

## 1. Goal

Let an author create and maintain a **campaign** (`campgns/<id>.cfg`) or a **mappack**
(`levels/<pack>.cfg`, also `multiplayer/`): its identity, its ordered level lists, the per-level
entries (names, players, land-view markers), and the campaign-wide config files — without
hand-editing `.cfg` text, and with the checks the loader does not give.

## 2. What the engine gives us

- One loader, `load_campaign()` (`config_campaigns.c`), for three families (`CampgnT_*`): campaigns
  in `campgns/`, free-play mappacks in `levels/`, multiplayer mappacks in `multiplayer/`. Lists on
  `struct GameCampaign`: `single_levels`, `multi_levels`, `bonus_levels`, `extra_levels`,
  `freeplay_levels`.
- `[common]` keys (24): `NAME`, `DESCRIPTION`, `SINGLE_LEVELS`, `BONUS_LEVELS` (parallel to
  `SINGLE_LEVELS`), `EXTRA_LEVELS`, `LEVELS_LOCATION`, `LAND_LOCATION`, `CREATURES_LOCATION`,
  `CONFIGS_LOCATION`, `MEDIA_LOCATION`, `CREDITS`, `HIGH_SCORES`, `INTRO_MOVIE`, `OUTRO_MOVIE`,
  `LAND_VIEW_START/END`, `LAND_MARKERS`, `LAND_AMBIENT`, `HUMAN_PLAYER`, `SOUNDTRACK`,
  `ASSIGN_CPU_KEEPERS`, `NAME_TEXT_ID`. `[strings]` and `[speech]` map languages to files.
- Per-level entries `[mapNNNNN]`: `NAME_TEXT`, `NAME_ID`, `ENSIGN_POS`, `ENSIGN_ZOOM`, `PLAYERS`,
  `ENSIGN`/`OPTIONS` (`DEFAULT`, `TUTORIAL`, `SINGLE`, `BONUS`, `FULL_MOON`, `NEW_MOON`, `COOP`),
  `SPEECH`, `LAND_VIEW`. A level's own `.lof` (KIND, AUTHOR, DESCRIPTION, DATE, MAPSIZE) and the
  `.lif` (free-play registry) are already written by the map editor (phase 3).
- Folder layout per campaign: `<id>/` levels, `<id>_lnd/` land images, `<id>_crtr/` creature
  files, `<id>_cfg/` config files, `<id>_media/` audio/video, `<id>_<lang>/` speech.
- **Locations can be shared.** `keeporig.cfg` points `CONFIGS_LOCATION` and `CREATURES_LOCATION` at
  `levels/classic_cfgs` / `levels/classic_crtr`, the same folders the *Classic* mappack uses. An edit
  at "campaign scope" then changes every campaign that names that folder.
- The map editor already has the mappack primitives (`editor_mappack`: `editormaps.cfg`, next free
  level number, rescan the pack list) and Save As with sidecar handling (`editor_sidecars`).

## 3. Screen design

```
┌ Campaign Editor ─ [Tyraels Realms ▾]  (campaign)  [New…] [Duplicate…] ──────────────────┐
│ ┌ Identity ┬ Levels ┬ Config files ┬ Land view ┬ Check ┐                                │
│ │ Levels:   Single      Bonus       Extra    Multi   Free play                          │
│ │  #  Level  Name           Players  Land view    Ensign     Map                        │
│ │  1  00300  Get to know       1     rgmap01       589,847   [Open in Map Editor]       │
│ │  2  00301  Raid              1     rgmap02       410,777   [Open in Map Editor]       │
│ │  [Add existing…] [New level…] [Move ▲▼] [Remove]                                      │
│ └──────────────────────────────────────────────────────────────────────────────────────┘ │
│  This campaign's config folder is shared with: keeporig, classic (2)   [Save] [Close]    │
└──────────────────────────────────────────────────────────────────────────────────────────┘
```

**Workflow this editor is built around** (decision 5): create the campaign definition, then create and
edit its maps **inside the campaign's own folder** (Save As / New Map target a campaign, §3 below).
Maps therefore never need copying between campaigns, and any content added later (out of scope for v1)
is visible to them without extra steps.

Pages:

- **Identity**: the `[common]` fields as typed widgets (text, player colour combo, file names with
  existence check, movie names), `DESCRIPTION`, `HUMAN_PLAYER`, `ASSIGN_CPU_KEEPERS`, `SOUNDTRACK`.
  File-name fields show a ✓/✗ against the campaign's media folders.
- **Levels**: one list per kind (single / bonus / extra / multi / free play), reorderable, with the
  per-level entry editable in place. *Add existing…* scans the levels folder for `map*.` sets
  (`.slb`/`.lof`) not yet listed; *New level…* creates a level number (`next_free_number`) and opens the
  Map Editor's New Map on it, registering the entry when it is first saved; *Open in Map Editor*
  starts a Map Editor session on that level (the frontend already does this for Tools ▸ Map Editor).
  *Move to…* / *Copy to…* another campaign reuses Save As's sidecar-aware copy (`editor_sidecars`,
  `MapContentWriter`) and asks before touching level numbers.
- **Config files**: the 19 config files with, per file, "exists in this campaign", size, override
  count, and an *Open* button that launches the matching editor at Campaign scope ([04]
  creatures, [05] traps/doors, [06] spells, [07] rooms, Rules editor, raw editor [F3] for the rest).
  *Create…* writes a partial-file skeleton with the standard header. The creature folder lists its
  per-creature files.
- **Land view** (later slice): the land-view image with the ensign markers; drag to set
  `ENSIGN_POS` / `ENSIGN_ZOOM`, using the existing land-preview drawing (`land_preview_*`).
- **Text**: the campaign's language strings (`text_<lang>.dat`), per language, plus which strings each
  level uses: a page of the Text editor, [09](09-text-strings-editor.md).
- **Check**: the validator report (§4).
- **Play**: *Play level…* and *Play campaign from level N* (foundation §10). The run returns here.

**Making maps inside the campaign** (Map Editor side of this plan): the Map Editor's New Map and Save
As dialogs get a **campaign / mappack picker** (replacing today's fixed "Editor Maps" default, which
stays as the fallback). Saving a new map into a campaign allocates the next free level number in that
campaign's `LEVELS_LOCATION`, writes the `.lof`/`.lif`, and adds the `[mapNNNNN]` entry and the list
entry (single / bonus / extra / free play, chosen once). The session records the map's campaign
context (foundation §10).

## 4. Validation (new value: the loader is silent about most of this)

Errors: level listed but its map files are missing; a level number listed twice or in two lists;
`LEVELS_LOCATION` not a folder; a required location or referenced file missing (credits, movies,
`HIGH_SCORES` file); `HUMAN_PLAYER` invalid. Warnings: a level's `PLAYERS` differs from its `.lof`;
a `[mapNNNNN]` entry with no level; a level with no entry (no name); shared config folder (§2);
free-play pack with no `.lif` (it will not be listed — the rule from the Editor Maps work);
`BONUS_LEVELS` not parallel to `SINGLE_LEVELS`; a level whose script uses commands the validator
rejects (reuses the map editor's script checks, per level, on demand).

## 5. Writing

Through `ConfigDocument` and `CampaignConfigWriter` ([10]): comments, ordering and unknown keys survive; only edited keys change;
atomic write. Number-list keys (`SINGLE_LEVELS = 300 301 …`) keep their column layout when unchanged.
After a save the campaign/mappack lists are **rescanned** (same call the Editor Maps registration
uses) so Free Play and the main menu see the change without a restart.

## 6. Slices

| Slice | Content | Tests |
|---|---|---|
| **K1** | Read-only browser of campaigns / mappacks / multiplayer packs (the three families) showing identity and level lists, using `ConfigDocument` (not the loader) so it works on any file; the validator (§4). | validator over every shipped `.cfg` (expected report snapshot); no false errors on shipped data |
| **K2** | Identity editing + atomic write + rescan. | round trip: parse → write → byte-identical when untouched; edit one key, others intact |
| **K3** | Levels page: list editing, per-level entries, add existing, reorder, remove, *Open in Map Editor*. | fixtures for each list kind; parallel-list invariant |
| **K4** | Config files page (opens the other editors; creates skeleton files). | file inventory equals directory listing |
| **K5** | New campaign / mappack wizard: folder skeleton (`<id>/`, `_cfg/`, `_crtr/`, `_lnd/`, `_media/`), `[common]`, level number allocation. | created tree loads with the real loader (ftest) |
| **K5b** | Map Editor: campaign picker in New Map / Save As; save into a campaign registers the level (entries, `.lof`, `.lif`); session campaign context. | save into a fixture campaign, reload, the entry and files exist |
| **K5c** | *Play level…* / *Play campaign from level N* with return (needs foundation F5). | ftest starts a campaign level and returns |
| **K6** | *(optional, late)* Move / copy a level between campaigns (sidecar-aware, renumber option). Not needed by the intended workflow. | copy preserves every `map%05d.*` file; entry moves |
| **K7** | Land view: ensign placement. | position round trip |

Order: K1 → K2 → K3 is the first release (identity + levels). K5 → K5b next: it enables the intended
workflow (campaign first, maps inside it). K4 needs the other editors' hooks and F3; K5c needs F5.
K6/K7 last.

## 7. Interactions

- **Map Editor:** Save As target choices ("Editor Maps" today) extend to *any* mappack the campaign
  editor knows; the Map Editor's **Tools ▸ Campaign Editor** opens on the pack the current map is in.
- **Content editors:** Campaign scope in 04–07 is exactly this editor's `CONFIGS_LOCATION` /
  `CREATURES_LOCATION`; those editors also list "shared with N other packs" using the check in §4.
- **Frontend:** the campaign list and free-play list refresh after saves (§5).

## 8. Non-goals

Speech and music files, movies, land-view art, high-score file contents, packaging and distribution
(zip/CPack). (Language strings moved into scope: [09](09-text-strings-editor.md).)

## 9. Risks and open questions

| Risk | Mitigation |
|---|---|
| A campaign folder reached only through a shared config location | Sharing check (§4) before any edit |
| Renumbering a level breaks references (scripts, other packs, saved games) | Renumber only on explicit request; warn about scripts that name level numbers (search the level's script text); never renumber silently |
| Shared config folders | Show the sharing list; make copying the folder ("give this campaign its own config") a one-click, confirmed action |
| Campaign files edited by hand differently (case, spacing, `;` comments) | `ConfigDocument` preserves; validator never rewrites |
| Free Play only lists packs with a `.lif` | Validator warns; creating a level from here writes one (existing map editor code) |

Resolved: *Duplicate* is not needed (decision 5). Language strings are **in scope**
([09](09-text-strings-editor.md)); level names can use either `NAME_TEXT` (literal) or `NAME_ID` (a
string id), and the editor supports both.

## 10. Second pass: what the shipped data and the loader say, and the refined design

Investigation of all 40 shipped campaign / mappack files, the loader, and the pieces the other editors already give us.

### 10.1 The three families are not the same shape

| Family | Files | What defines its levels | What its `.cfg` holds |
|---|---|---|---|
| Campaign (`campgns/`, 19) | `[common]`, `[strings]`, `[speech]`, and one `[mapNNNNN]` per level | Lists in `[common]`: `SINGLE_LEVELS`, `BONUS_LEVELS` (parallel to it, `0` = none), `EXTRA_LEVELS` | everything in §2 |
| Free-play map pack (`levels/`, 16) | `[common]` and `[strings]` only, **no level list, no `[mapN]`** | **The folder**: every `map%05d.lof` / `.lif` found in `LEVELS_LOCATION` (`find_and_load_lof/lif_files`) | identity, locations, high scores, `HUMAN_PLAYER`, strings |
| Multiplayer pack (`multiplayer/`, 5) | `[common]`, `[strings]`, optional `[mapN]` (`MAPSIZE`, ensign) | `MULTI_LEVELS` (one file has it), or the folder | identity, locations, landview |

Consequence: the **Levels page is two different pages**. For a campaign it edits three lists and the per-level entries. For a pack it is an
**inventory of the folder** (which levels exist, from their `.lof`/`.lif`), with create / open-in-Map-Editor; the only thing to edit is which levels
exist, which is done by creating or deleting files, not by a list. A campaign level number only has to be unique **within the campaign's folder**
(Tyraels uses 300-304, keeporig 1-20 plus bonus 100-105), so numbering is a per-campaign decision (§10.7-c).

Other facts that matter:

- **Numbers** in the lists are written column-aligned (`SINGLE_LEVELS =   300   301   302`); the editor rewrites a changed list with the same
  column width and leaves an untouched list byte for byte.
- **Ordering in the menu** comes from `campgns/campgn_order.txt` / `levels/mappck_order.txt` (names not listed sort last, alphabetically). Not part of the
  `.cfg`.
- **Sharing** is common: `keeporig` and the *Classic* pack share `levels/classic_cfgs` and `levels/classic_crtr`. `ContentCampaign` already carries every pack's
  resolved `cfg_dir` / `crtr_dir`, so "shared with N others" is a comparison, no new scan.
- The `[strings]` block (language -> path) is what the Text editor needs; a campaign with **no line for a language** cannot get a campaign-layer string
  file until this editor adds the line (the Text editor tells the author so today).
- `NAME_ID` on a level entry is a **string id** into the campaign strings (the Text editor's usage scan already reads it); `NAME_TEXT` is the literal.

### 10.2 What we reuse (so this is smaller than it looks)

- **Writing**: the campaign `.cfg` is an ordinary INI-like file, so the existing `ConfigDocument` + patch-in-place writer + `WriteBatch` apply as they are
  (comments, key order, column layout, CRLF all survive). It needs a **schema kind `campaign`** (curated shapes, like the creature schema) and a small
  `CampaignConfigWriter` (header only; the generic table writer does the rest). There are no layers: the session opens the file **as the single writable
  layer** (the same `StructuredSession`, with the pack's own directory as its "campaign" layer and no base), so form rows, Reset, pending edits and
  validation come for free.
- **Form widgets**: text, numbers, name pickers, flag lists, the composite rows of the creature editor (a per-level entry's ensign position pair, land-view
  pair, speech pair, `PLAYERS`, ensign flags).
- **Lists**: the research tab's item editor is the model for the level lists (add from what is not listed yet, reorder by drag or buttons, totals).
- **Picker**: `ContentPicker` gets a *preselect this pack* entry so the other editors can be opened from here at Campaign scope.
- **Map Editor primitives**: `editor_mappack` (Editor Maps folder, next free level number, register + rescan), Save As with sidecar copying, `.lof`/`.lif`
  writers (phase 3), the playtest hand-off (`frontend_request_editor_playtest`, campaign switch and restore from F5).
- **Validation**: a pure checker over `ConfigDocument` + the folder listing (so it runs on any file, not only the loaded campaign), like `cfgc_validate_document`.

### 10.3 Refined pages (first release in bold)

1. **Identity (K2)** - the 22 `[common]` keys as typed widgets; file-name keys (credits, movies, high scores, soundtrack, landview files) show a check
   mark when the file exists in the media/land folders; the four locations show "folder exists" and "shared with N other packs (names)". `[strings]` and
   `[speech]` as language -> path rows (add / remove a language line; the path can be created from the Text editor).
2. **Levels (K3)** - *campaign*: one table per list (single, bonus, extra; multi for a multiplayer pack) with the per-level entry edited in place
   (name literal or `NAME_ID` with the resolved string beside it, players, ensign flags, positions), **add existing / add from folder**, reorder, remove
   *from the list* (never deletes files), *Open in Map Editor*. *Pack*: folder inventory (number, name from `.lof`, players, file completeness) with
   *New level* and *Open in Map Editor*.
3. **Check (K1)** - the validator report, on demand and shown as a banner in the other pages (§10.4).
4. **New campaign / pack (K5)** - a wizard: name, id (folder stem), kind, human player, first level; creates the folder skeleton and `.cfg`, refreshes the
   lists so the pack shows in the menu, and can open a New Map inside it.
5. **Config files (K4)** - inventory of the target's config and creature files (exists, size, keys overridden); *Open* launches the matching editor already
   at this campaign (Rules, Trap and Door, Spell and Ability, Room, Creature, Text, Config Files for the rest); *Create* writes an empty partial file with the header.
6. **Play (K5c)** - *Play level* and *Play campaign from level N* through the existing start path; the run returns to the Campaign Editor (§10.5).
7. later: land-view / ensign placement (K7), move/copy a level between packs (K6, not needed by the intended workflow).

### 10.4 Validator (pure, runs on any file)

Errors: a listed level whose map files (`.slb`/`.dat`/... or the native `.tngfx` set) are missing; a level listed twice or in two lists; `BONUS_LEVELS`
not the same length as `SINGLE_LEVELS`; `LEVELS_LOCATION` not a folder; an invalid `HUMAN_PLAYER`; a `[mapNNNNN]` whose number is a list member twice.
Warnings: a location folder missing or **shared** with other packs (names them); a `[mapNNNNN]` entry with no level and a level with no entry; a level's
`PLAYERS` different from its `.lof`; a referenced file missing (credits, movies, land view, speech, `[strings]` paths); a language line whose file is
shorter than the loader's 16-byte minimum; a free-play pack level with no `.lif`. The report over the 40 shipped files is committed as a snapshot so a new
false positive shows up in review; expected findings on shipped data are recorded, not hidden.

### 10.5 The Play hand-off (K5c) and where the editor lives

The Campaign Editor is a **main-menu tool** (no map). *Play* starts the level through the normal single-player start (the frontend already does this for
the editor's Playtest, F5); when the game ends the frontend returns to the **main menu with the Campaign Editor reopened on the same pack**: a small
`frontend_request_content_tool_return(tool, pack)` next to `editor_playtest_running`, generalising "return to origin" from *map editor* to *content tool*.

### 10.6 Refined slices

| Slice | Content | Gate |
|---|---|---|
| **K0** | Schema kind `campaign` (common, strings, speech, `[mapN]` shapes) + `CampaignConfigWriter`; coverage test over the 40 files; `ContentPicker` preselect. | every shipped file: no unknown section/key; parse -> write untouched is byte-identical |
| **K1** | Validator + committed snapshot; read-only browser page (identity + levels + check) for all three families. | no false errors on shipped data (findings listed, not hidden) |
| **K2** | Identity editing (form on the single-layer session) and `[strings]` / `[speech]` rows; rescan of the pack lists after Apply. | edit one key, everything else byte-identical; the pack list shows the change without a restart |
| **K3** | Levels page: campaign lists and per-level entries; pack folder inventory; add / reorder / remove; *Open in Map Editor*. | fixtures per list kind; parallel-list invariant; real loader reads the edited lists (ftest) |
| **K5** | New campaign / pack wizard with the folder skeleton. | the created tree loads with the real loader (ftest) |
| **K5b** | Map Editor: New Map / Save As target picker (any campaign or pack; Editor Maps stays the default); saving a new map into a campaign allocates a number, writes `.lof`/`.lif` and adds the entry and list member; campaign context in the session. | save into a fixture campaign, reload; entry and files exist |
| **K4** | Config files page (inventory, open the other editors at this pack, create skeleton). | inventory equals the directory listing |
| **K5c** | Play level / from level N, with return to the tool (needs §10.5). | ftest starts a campaign level and returns |
| **K7 / K6** | Land-view ensign placement; move/copy a level. | later |

### 10.7 Decisions needed before building

a. **Scope of v1 across families.** Proposed: campaigns fully (K0-K5b); free-play packs get identity, strings and the folder inventory; multiplayer packs get
   identity and `MULTI_LEVELS`. Is that the right cut, or campaigns only first?
b. **Editing shipped packs.** The shipped campaigns live in the game folder like user ones; nothing distinguishes them. Proposed: no special protection, but the first
   Apply to a pack asks once ("this changes files in <folder>"), and the *original campaign* (`keeporig`) is edited only after a per-session confirm. Alternatives:
   read-only for anything that ships, or a "copy to a new pack" first.
c. **Level numbers for new levels.** Proposed: campaign -> next number after the highest listed (starting at 1 for an empty campaign), bonus levels 100+ like
   keeporig, packs -> next free number in the folder (as Editor Maps does). Numbers are unique per pack only.
d. **Removing a level.** Proposed: *Remove from campaign* takes it out of the list and its `[mapN]` entry (asks about the entry) but **never deletes map files**;
   the inventory then reports the files as unlisted, and a separate *Delete level files...* (confirm, lists every `map%05d.*`) exists only in the pack inventory.
e. **Menu order.** Show the pack's position from `campgn_order.txt` / `mappck_order.txt` and offer move up/down (writes that file)? Proposed: yes but late (K4+),
   the files are outside the `.cfg`.
f. **Shared config folders.** Proposed: the sharing note plus a one-click **"Give this campaign its own configuration"** (confirmed; copies the shared folder to
   `<id>_cfg` / `<id>_crtr` and repoints the two locations) - it is what makes campaign-scope edits safe. Or leave it to the author?
g. **K5b timing.** The intended workflow needs maps created inside the campaign; K5b touches the Map Editor's New Map / Save As. Build it right after K3 (before K4),
   as the order in §6 says, or defer until the editor itself is proven?
h. **Land-view placement (K7)** stays out of v1 unless you want it sooner (it needs the land-view drawing code from the frontend, which the editor can already call).

## 11. Decisions (confirmed by the user) and their consequences

| # | Decision | Consequence for the design |
|---|---|---|
| a | **Campaigns first.** Free-play packs and multiplayer packs follow after the campaign editor is proven. | K0-K7 are campaign-only; the pack inventory page and the `MULTI_LEVELS` list are out of v1. |
| b | **No special protection**, including the original campaign: the pre-FX game had no hard-coded campaign cfg, only the `keep_orig` folder, so `keeporig.cfg` is an ordinary campaign file. | No confirm dialogs, no shipped-campaign list. Base game data (`fxdata`) stays protected as everywhere. |
| c | Level numbers: next after the highest listed (1 for an empty campaign), bonus levels 100+, unique per campaign only. | K3 / K5b number allocation. |
| d | **No deleting level files at all** ("leave that to the user via OS tools"). | *Remove from campaign* only edits the list and (after asking) the `[mapN]` entry. There is no delete action and no inventory-delete. The check reports files no list refers to. |
| e | Menu order: show the position from `campgn_order.txt` and offer move up / down, late. | K4+ (writes `campgns/campgn_order.txt`). |
| f | Shared config folders: show who shares them, plus a confirmed one-click **"Give this campaign its own configuration"** (copy the shared `CONFIGS_LOCATION` / `CREATURES_LOCATION` and repoint the two keys). | Part of Identity (K2); the copy uses `WriteBatch` (all files or none). |
| g | The Map Editor's save-into-campaign (K5b) is built **right after the Levels page (K3)**. | Order below. |
| h | **Land-view placement is in v1**: the land-selection screen (main menu) is the only way to pick a campaign's levels, so a level with no usable ensign cannot be started. | New slice K7 in v1; K3 gives every new level a working default ensign so it is selectable at once. |

### 11.1 Land-view placement (K7), what it needs

How the game uses it (checked): each level entry carries `ENSIGN_POS` (position on the campaign's overview picture) and `ENSIGN_ZOOM` (position on the
zoomed view), both in map-bitmap pixels; `LAND_VIEW = <image> <frame>` per level and `LAND_VIEW_START` / `LAND_VIEW_END` for the campaign overview
(`rgmapNN.raw` / `.pal` in `LAND_LOCATION`, `viframeNN.dat`); `LAND_MARKERS = ENSIGNS | PINPOINTS` picks the marker sprites; `ENSIGN` flags choose the flag
kind (`DEFAULT`, `TUTORIAL`, `SINGLE`, `BONUS`, `FULL_MOON`, `NEW_MOON`). The frontend already has a reusable panel, `land_preview_*`
(`frontmenu_landpreview`: loads the overview image, pans, draws ensigns, hit-tests), used by the Land selection screen.

Design: a **Land view** page in the Campaign Editor that shows the overview picture with every level's ensign; click a level in the table and click the
picture to set `ENSIGN_POS`, drag to move it, a second mode sets `ENSIGN_ZOOM`; numeric fields beside it for exact values; ensign kind and marker style
pickers. Default placement for a new level: spread along the picture (a grid), so the level can be selected before the author has positioned it.

Risks and the spike (**K7-S1**, before K7): the panel draws through the legacy renderer into a rectangle of the frontend screen and loads the image for the
**currently loaded campaign** (a global), while the editor edits a chosen campaign; the spike decides between (1) hosting the panel in the editor window after
a `change_campaign()` to the edited pack (restored on close, as F5 does), or (2) a small independent image loader in the editor (the `.raw` is a raw 8-bit bitmap
with a `.pal` palette, easy to read) drawn as an ImGui image with the markers as an overlay. (2) is likely simpler and safer; the spike measures it.

### 11.2 Revised order

**K0** schema + writer -> **K1** validator + read-only browser -> **K2** identity (incl. strings/speech rows, the sharing note and *own configuration* copy) ->
**K3** levels page (lists, entries, default ensigns) -> **K5b** Map Editor save-into-campaign -> **K5** new-campaign wizard -> **K7-S1 / K7** land view ->
**K4** config files page and menu order -> **K5c** play with return. Each slice ships with its gate (§10.6) and the same acceptance style as the other editors
(a real-loader ftest for anything the game reads back).

### 11.3 PNG land-view images (decision: the game accepts PNG directly)

Suggested by the user: let the game load the land-view background as a **PNG**, so authors do not have to encode `.raw` + `.pal`. Investigation:

- Today the background is `<name>.raw` (exactly 1280 x 960, 8-bit indexed, 1 228 800 bytes) plus `<name>.pal` (768 bytes, 6-bit VGA palette) in `LAND_LOCATION`,
  loaded by `load_map_and_window()` (`front_landview.c`) into the frontend's land buffer and palette; the whole land pipeline (zoom, palette fades, ensign
  drawing, the Land selection panel `land_preview_*`) works on that indexed data. The decorative frame is a separate RNC sprite `<viframe>.dat`
  (`LAND_VIEW = rgmap01 viframe01`); authors can keep using the shipped frames.
- **No renderer change is needed.** PNG decoding already exists in the tree (`spng`, used by `custom_sprites.c`). The loader gets a PNG path in front of the
  `.raw` path: if `<name>.png` exists it is used, else `.raw` + `.pal` as before. An **8-bit indexed PNG** is used as it is (its palette, shifted to 6 bits, its pixels);
  any other PNG (RGB/RGBA, 16-bit) is **quantised to 256 colours** (median cut, deterministic) into the same buffer and palette, so everything downstream is
  unchanged. The image must be 1280 x 960 (anything else is rejected with a clear message rather than silently scaled; the editor says so before Apply).
- **One shared helper** (`landview_image_load`, a small function in the frontend layer that both the game and the editor call) decodes `.png` or `.raw`/`.pal`
  into an indexed image (pixels + palette) and can also give RGBA for display. This settles spike K7-S1 in favour of **option (2)** of §11.1: the Campaign Editor's Land view
  page draws the picture as an ImGui image with the ensigns overlaid, without switching the loaded campaign and without touching the legacy renderer path.
- New checks in the validator: the referenced image exists (as PNG or `.raw` + `.pal`), is 1280 x 960, and (for `.raw`) the `.pal` is 768 bytes.
- Slice **K7a** (before K7): the shared loader, the PNG path in `load_map_and_window()`, unit tests (indexed PNG round-trips to the same pixels and palette as the `.raw`
  it came from; the quantiser is deterministic and stays within 256 colours; wrong size rejected), documentation for modders (`docs/`), and a converter-free workflow
  note: drop `rgmap01.png` next to (or instead of) `rgmap01.raw`. The original `.raw`/`.pal` files keep working and take second place when a PNG of the same name exists.

## 12. Built (results)

### 12.1 K0: campaign schema, writer, corpus check

- Schema kind `campaign` (`cfgc_schema_campaign.cpp`): `[common]`, `[strings]`, `[speech]` (one key per `lang_type` code, value a path) and numbered `[mapNNNNN]`. Keys come from the loader's own tables (`cmpgn_common_commands`, `cmpgn_map_commands`, now exported), value shapes from a curated table. The shape machinery was extracted from the creature schema into `cfgc_schema_shapes.{h,cpp}` (`cfgc_apply_shape`) and is shared.
- `MULTI_LEVELS` (in one shipped pack) is recorded as an ignored key: the loader has no such key.
- `cfgc_make_writer(schema, "campaign")` writes with the header "; KeeperFX campaign file -- written by the map editor.".
- Writer change: setting a value to the same words with different spacing (`99  111`) leaves the line as written.
- Corpus test (`cfgc_schema_test.cpp`): all shipped `.cfg` files directly in `core_files/campgns`, `levels`, `multiplayer` are in the schema (no unknown section or key), validate with no warnings (a few leave `NAME_TEXT_ID` / `HIGH_SCORES` empty, reported as `missing_value` and tolerated in the test), and setting every single-valued key to its own value leaves each file byte-identical.
- Finding: `AbyssGuardians.cfg` has a stray Ctrl-Z byte before `[map10005]`, so the loader does not see that header and the entry's keys land in `[map10000]` (last value wins). The K1 validator should flag a line that looks like a header but is not one.
- Still open from K0: `ContentPicker::open(..., campaign_fname)` preselect (done with K1, where the browser needs it).

### 12.2 K1: checker and read-only browser

- `cfgc_check_campaign(doc, schema, env)` (`cfgc_campaign_check.{h,cpp}`, `kfx_config`): pure, the file system and the other packs come in through `CampaignCheckEnv`. It runs the generic validator with the `campaign` schema, then: level lists (duplicates across lists, listed level with no `map%05d.slb`), `HUMAN_PLAYER`, locations (missing folder, **shared** `CONFIGS_LOCATION` / `CREATURES_LOCATION` naming the other packs), `[mapNNNNN]` entries no list contains and listed levels with no entry (note), a repeated entry for one level, land-view images (`.raw` or `.png`), `[strings]` files (missing, under 16 bytes), and a line that looks like a block header but has stray characters before it (the game does not see that block).
- Adjusted from §10.4 by the shipped data: a `BONUS_LEVELS` list **shorter** than `SINGLE_LEVELS` is normal (`necro`, `tempkpr`) and is not reported; only a longer one warns. Empty `NAME_TEXT_ID` / `HIGH_SCORES` values are notes.
- The snapshot `src/kfx_config/tests/fixtures/campaign_check_snapshot.txt` holds the findings over every shipped campaign. It leaves out the checks that depend on files the repository does not ship (the original game's maps, art, text), so it is the same on any checkout. Regenerate with `KFX_UPDATE_SNAPSHOT=1 kfx_config_utest "[cfgc_campaign_check]"`. Findings on shipped data: the stray Ctrl-Z in `AbyssGuardians.cfg`, three unused `[map0010x]` entries in `dak_21map`, and the shared configuration folders (keeporig with six others, etc.).
- The **Campaign Editor** window (`content_campaign.{h,cpp}`) is listed in both Tools menus (no longer "coming soon"): a campaign combo (campaigns only, per decision a), Reload, Close, and tabs **Identity** (every `[common]` key with the value, `folder exists/missing` beside the locations, then the `[strings]` and `[speech]` rows), **Levels** (one row per listed level: list, name or string id, players, ensign, whether its `.slb` is present; resizable columns) and **Check (n)** (line, kind, finding). Read-only.
- Tested: unit tests (fixtures for each finding, the snapshot), the tool-smoke ftest draws the window in both hosts.
- `ContentPicker` preselect is not needed by this window (it has its own campaign combo); it moves to K4, where the other editors are opened from here at Campaign scope.

### 12.3 K2: identity editing, strings/speech rows, own configuration

- The Campaign Editor's **Identity** tab is now a form on the campaign's own `.cfg` (a single writable layer, no base): every `[common]` key is a text box, or a combo for the enumerated ones (`HUMAN_PLAYER`, `ASSIGN_CPU_KEEPERS`, `LAND_MARKERS`); the level lists show their value and point to the Levels page (K3). Below them the `[strings]` (language -> text file) and `[speech]` (language -> speech folder) rows. Each row has *Remove* (except `NAME`); a combo per block adds a key or language. Edited rows are shown in the warning colour; the Check list and the summary line follow the pending edits; the campaign combo, Reload and Close are locked until Apply or Revert.
- Apply patches the file through the campaign writer (comments, order, spacing survive; new keys are appended to their block, and the writer aligns them to the block's key column), commits with `WriteBatch`, then **rescans the campaign list** (`load_campaigns_list`, as Editor Maps does for packs) and reselects the campaign, so a renamed campaign shows in the main menu at once.
- The **sharing note** sits beside each `*_LOCATION` (folder exists / missing, shared). **Give this campaign its own configuration** (enabled when nothing is pending and a folder is shared; confirmation dialog) copies each shared `CONFIGS_LOCATION` / `CREATURES_LOCATION` folder to `<parent>/<campaign>_cfg` / `_crtr` (`cfgc_own_location`, `cfgc_plan_folder_copy` in `cfgc_campaign_edit.{h,cpp}`) and repoints the two keys, in one `WriteBatch` (all files or none; refuses an existing target folder).
- Tests: unit tests for the naming and the staged folder copy; ftest `config_content_campaign_editor` (a scratch campaign sharing keeporig's folders: renamed, `HUMAN_PLAYER` set, comment kept, pack list shows the new name, own folders hold a copy of every shared file, keeporig's file untouched, scratch files removed). The smoke test draws the window.
- Not done: nothing is written into the file for a language line whose text file does not exist yet (the Text editor creates the file; K4 links the two).

### 12.4 K3: Levels page (campaigns)

- **Model** (`cfgc_campaign_levels.{h,cpp}`, pure, unit-tested): `CampaignLevels` = single levels, the parallel bonus list (padded with 0), extra levels; add / set bonus / move / remove keep the lists parallel (moving a single level moves its bonus level; removing one drops its bonus level from the lists); `cfgc_next_level_number` (decision c: next after the highest listed, 1 when empty, bonus/extra from 100); list text written in the file's own column pitch (single and bonus share one width so they line up; an unchanged list leaves the file byte-identical); `cfgc_default_entry` gives a new level a five-digit `[map0000N]` entry with `NAME_TEXT`, `PLAYERS = 1` and `ENSIGN_POS` / `ENSIGN_ZOOM` spread over the 1280 x 960 overview in rows of six, so a level is selectable on the land screen at once.
- **Writer**: a new numbered block in a campaign file is named with five digits (`[map00007]`), as the shipped files do.
- **Page**: the table lists every single level (its bonus level under it) then the extra levels, with name, players, ensign and whether the `.slb` exists. *Add* takes a map file of the campaign's folder that no list has (as single, as extra, or as the selected single level's bonus), creating its entry; *Up/Down*; *Remove from campaign* (confirmation, with "also remove its [map] entry"; never touches map files); *Open in Map Editor* (main-menu host, nothing pending): makes the campaign current and opens the level (`frontend_request_map_editor_open`). Below the table the selected level's entry is edited in place: name, string id, players, ensign flags, ensign and zoom positions (graphical placement is K7). Apply / Revert moved to the window's top row and cover every tab; new entries are kept in the order they were made.
- **Bug found and fixed**: `content_list_campaigns` read the bonus / extra lists with the count of non-zero levels, so a bonus level after a `0` slot (e.g. `0 0 100`) was missing from a campaign's level list (it affected the level pickers of the other editors). It now reads every slot and skips the zeros.
- Tests: unit tests for the model and the file layout; ftest `config_content_campaign_editor` also adds single / extra / bonus levels, reorders, removes with its entry, applies, and checks the game's own list after a rescan and the written text.
- Not built here (by decision): deleting map files; creating a new level (K5b / K5).

### 12.5 K5b and K5: save into a campaign, new campaign wizard

- **Finding that shaped K5**: the game lists a campaign only when it has at least one **single level** (`load_campaign_to_list`). A campaign file with no `SINGLE_LEVELS` is invisible to the game, to `content_list_campaigns` and so to this editor. A new campaign therefore starts with `SINGLE_LEVELS = 1`, `BONUS_LEVELS = 0` and a `[map00001]` entry, and the checker reports level 1's missing map file as an error until the first map is saved into it (honest, and it is what stops the campaign being played empty).
- **K5b, Save As**: the Map Editor's Save As dialog has a **Save into** combo: Editor Maps (still the default for an untitled map), any campaign, or another folder (Browse... only for that one). A campaign target fills in the levels folder and the next level number, and an **Add to the campaign as** combo (single level, extra level, not listed). After a successful save the level is registered (`content_campaign_register_level`: list entry through the same list model as the Levels page, a default `[map0000N]` entry with the level name and a spread-out ensign, campaign list rescanned); saving over a listed level with an entry changes nothing. Re-saving a level of a campaign's own folder starts on that campaign with "not listed". Pure part: `cfgc_campaign_add_level` (unit-tested).
- **Next number** (`content_campaign_next_level`): the first listed single level whose map file does not exist yet (a new campaign's level 1), else next after the highest listed (decision c).
- **K5, wizard**: **New campaign...** beside the campaign combo: name, file/folder name (filled from the name, `cfgc_campaign_id_from_name`; letters, digits, underscore; must be free, any letter case), human player, "its own configuration and creature folders" (default on; creates empty `<id>_cfg` / `<id>_crtr`). It creates `campgns/<id>.cfg` (`cfgc_new_campaign_text`: name, levels folder, land view `rgmap00`/`rgmap07` from the base data so the land screen works, ensign markers, human player, level 1), the folders, rescans the list and selects the new campaign.
- **New map in this campaign** (Levels page, main menu host): makes the campaign current, starts the Map Editor on a blank map (`frontend_request_map_editor_open(..., is_new)`), and the first Save As offers that campaign with the next level number.
- Tests: unit tests for the new-campaign text (reads back, checks clean with its folders and map present) and the level registration; ftest `config_content_campaign_editor` now also registers a saved level (list, entry, next number) and creates a campaign through the wizard's function (file, folders, the game's list, id taken, first map takes level 1).
- Not built: campaign land-view images of its own (K7 / K7a), and free-play / multiplayer packs (later, decision a).

### 12.6 K7a and K7: PNG land views, the Land view page

- **K7a, shared loader** (`landview_image.{h,cpp}`, `kfx_render`): `landview_decode_png` (an **8-bit indexed PNG keeps its pixels and palette**, the palette shifted to the game's 6 bits; any other PNG is **quantised to 256 colours** by a deterministic median cut over a 5-bit histogram; not 1280 x 960 is refused with both sizes in the message), `landview_load` (`<base>.png`, else `<base>.raw` + `<base>.pal`, the `.raw` unpacked like the game does since the originals are RNC-compressed), `landview_to_rgba` for display, and the C entry `landview_load_png_indexed` used by the game.
- **Game**: `load_map_and_window()` (`front_landview.c`) tries the PNG first (pixels straight into the land buffer, palette into `frontend_palette`); with no PNG the old `.raw` + `.pal` path is unchanged. A PNG that cannot be used is logged and the level's land view fails like a missing `.raw` does. Modders: drop `rgmap01.png` next to (or instead of) `rgmap01.raw`; the `viframe` window sprite stays as it is.
- **Checker**: a referenced image must exist as `.png` or `.raw` + `.pal`; a PNG must be 1280 x 960 (read from its header), a `.pal` 768 bytes. A `.raw` size is **not** checked: the shipped ones are compressed.
- **K7, Land view tab**: level list on the left, the picture on the right (scaled to fit, drawn as a texture; the shared loader, so no change of the loaded campaign and no legacy renderer). Every level with a position gets a numbered marker (the selected one larger, yellow). *Place* chooses the **ensign position on the overview** (`LAND_VIEW_START`'s picture) or the **ensign zoom position** (the level's own `LAND_VIEW` picture when it has one, else the start picture); clicking or dragging on the picture sets the selected level's `ENSIGN_POS` / `ENSIGN_ZOOM` as a pending edit (Apply / Revert as everywhere). Needs `LAND_LOCATION` and `LAND_VIEW_START`; says so otherwise.
- **Findings that changed K5**: (1) with no `LAND_LOCATION` the game builds an empty path, so a campaign must have its own land folder: the wizard now creates `campgns/<id>_lnd` with a copy of `rgmap00.raw/.pal` and `viframe00.dat` from keeporig's land folder (or any land folder that has them), and points the land view keys at it. (2) **The game refuses to load a campaign without `[strings]` and `[speech]` blocks** (list-only loading, used for the menu list, does not notice); the wizard's file now has both (base game text `fxdata/gtext_eng.dat`, keeporig's speech). Both were found by the new ftest that makes the game load the wizard's campaign.
- Tests: `kfx_render_utest` (indexed PNG round trip, quantiser bounds and determinism, wrong size, loader preference and fallback); checker tests for PNG size and palette; ftest `config_content_landview_png` (the game loads a wizard campaign, takes the PNG's pixels and palette, and still loads `.raw` + `.pal` without it).
- Not verified on screen: the Land view tab's drawing and dragging.

### 12.7 K4: Config files page and menu order

- **Config files tab**: every file the campaign's configuration and creature layers can hold (the base list of `content_raw_list_files`), with *In this campaign*, size and the number of keys it sets; "Only the files this campaign has" (default on). **Open** starts the matching editor already on this campaign (`rules.cfg` Rules, `trapdoor.cfg` Trap and Door, `magic.cfg` Spell and Ability, `terrain.cfg` Room, `creature.cfg` and creature model files Creature, anything else the raw Config Files editor on that file); this is the `ContentPicker` preselect deferred from K1, done as `content_picker_set_last_campaign` (the pickers already restore the last-used campaign) plus `content_tools_open_file`. **Create** writes the campaign-layer file with one comment line, so the other editors have a file to work on. A campaign with no `CONFIGS_LOCATION` / `CREATURES_LOCATION` gets a *Create one* button that makes the folder and stages the key (Apply to keep it).
- **Menu order**: the Identity tab shows the campaign's place in the campaign menu (from the list the game holds) with *Move up / Move down*: the whole order is written to `campgns/campgn_order.txt` (its `#` comment header kept, its line endings kept; `cfgc_move_in_order`, `cfgc_order_file_text`, unit-tested) and the lists are read again.
- Tests: unit tests for the order helpers; ftest `config_content_campaign_editor` moves a campaign up (and puts the order file back) and creates a campaign `rules.cfg` from the page. Not done: keys-overridden counts are per file only (no per-key diff against the base).

### 12.8 Per-level land view and speech, K5c play with return

- **Per-level land view picture and frame** (`LAND_VIEW = image frame`): pickers on the Levels tab (entry panel) and above the Land view picture. They list the images (`.raw` or `.png`) and `viframe*.dat` frames found in the campaign's land folder; "(campaign default)" removes the key so the level uses the start picture; choosing one of the two fills the other from `LAND_VIEW_START`. The zoom position of the Land view tab is placed on that picture.
- **Per-level speech** (`SPEECH = before after`, files of the campaign's speech folder): two text boxes with a found / not-in-the-speech-folder hint; both names are needed (the loader reads two). The checker gained `landview_missing` / `landview_frame` for `LAND_VIEW`, `speech_pair` and `speech_missing` for `SPEECH` (the last excluded from the committed snapshot because the original speech files are not in the repository).
- **K5c, Play from this level** (Levels tab, main menu host, nothing pending): makes the campaign current and starts the level through the normal single-player start (`frontend_request_content_tool_play`, deferred like the other transitions); when that game ends (win, lose or quit) `get_startup_menu_state` returns to the main menu, which opens the Campaign Editor again (`content_tool_play_running` / `content_tool_return_tool` / `content_tool_reopen_tool`); the editor reopens on the campaign it was showing. The plan's separate "play campaign from level N" is this same button: the game's own progression decides what follows a win, and the return happens whenever the game ends.
- Tests: checker tests for the per-level entries; ftest `config_content_campaign_editor` also drives the frontend's end-of-game branch (main menu state, tool to reopen, flags cleared). The start itself (a real game run from the menu) is not driven by an ftest.
