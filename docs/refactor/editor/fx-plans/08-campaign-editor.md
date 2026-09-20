# FX plan 08 — campaign and mappack editor

Status: **plan, first pass.** Nothing built. Depends on [03](03-content-editors-foundation.md) for the
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
