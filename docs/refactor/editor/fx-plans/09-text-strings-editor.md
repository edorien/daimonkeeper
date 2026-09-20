# FX plan 09 — text (language strings) editor

Status: **plan, first pass.** Nothing built. Added after the first review of the content editors
(decision 4 in [03](03-content-editors-foundation.md) §1): a campaign's level names and mission
objectives live in its language string file, so authoring a campaign needs to edit it. Depends on
[03](03-content-editors-foundation.md) F0 (Tools menu); the layered reader is a sibling of `ConfigStack` ([10]),
not a user of it, because the format is different (its writer family: `StringsFileWriter`, [10]).

## 1. Goal

Let an author write and translate the text a campaign shows — level names, objectives, briefings,
tooltips they override — per language, see which strings the campaign actually uses and which are
missing, and preview how they will read in the game, without editing a NUL-separated binary-looking
file by hand.

## 2. What the engine gives us (verified)

- **Format.** A string file is a raw byte file of **NUL-terminated entries**; the **ordinal is the
  string id** (`fill_strings_list`, `config_strings.c:61`). The file usually begins with a NUL
  (entry 0 is empty). Entries may contain CR LF (the shipped `dungeon_architect.eng.dat` has them);
  the loader rejects a file shorter than 16 bytes (`load_campaign_strings_data_from_file`). Capacity:
  ids `0 … STRINGS_MAX-1` = 2 000 for campaign and level strings.
- **Encoding.** The file is in the **language's code page** and is converted to UTF-8 at load
  (`convert_codepage_to_utf8_buffer(…, lang_id)`, `bflib_text.h`). Editing therefore needs the
  *reverse* conversion, which does not exist yet (Spike T-S1).
- **Three layers, resolved per id** (`get_string`, `config_strings.c:414`), first non-empty wins:
  1. **level** `map%05d.<lang>.dat` in the level's folder (`lvl_filesdk1.c:1385`, language = the
     campaign's default language),
  2. **campaign** `text_<lang>.dat`, named by `[strings] ENG = path` (one line per language) in the
     campaign `.cfg`,
  3. **base** `fxdata/gtext_<lang>.dat`.
  An **empty entry means "inherit"** (`fill_strings_list`: "do not replace empty string"), so a
  campaign file only has to contain the ids it changes. If the player's language has no campaign
  file the loader falls back to the campaign's English file.
- **Who refers to string ids:** the campaign's `[mapNNNNN] NAME_ID`; `NameTextID` / `TooltipTextID`
  keys in every content config file ([04]–[07]); script commands `DISPLAY_OBJECTIVE(n,…)` and
  `DISPLAY_INFORMATION(n,…)`. (`QUICK_OBJECTIVE(n,"text")` carries its text inline in the script and
  needs no file; the map editor's message helper writes those.)
- The base game strings (ids the game already uses, e.g. 200-ish level names, tooltips) share the
  same id space; a campaign string simply overrides the base one.

## 3. What the editor does

**Model** (`strings_file`, pure, unit-tested): a list of entries holding the **original bytes** of every
entry and, lazily, its decoded UTF-8. Entries the author does not touch are written back **byte for
byte**; only edited entries are re-encoded. Round trip must be byte-identical for every shipped
campaign and mappack string file.

**Stack** (`strings_stack`): base → campaign → level for one language, giving for each id the
effective text, the layer it comes from, and what it overrides. The same badges as the config editors
(Base / Campaign / Level, inherited / overridden, Reset).

**Usage scan** (`text_usage`): scans the campaign's `[mapNNNNN]` entries, its config files, and the
scripts of its levels for string ids and builds "used by" lists. Reuses the script editor's tokeniser
for `DISPLAY_OBJECTIVE` / `DISPLAY_INFORMATION` and the config document reader.

### Screen

```
┌ Text Editor ─ Campaign: Tyraels Realms   Language: [English ▾]  Layer: [Campaign ▾] ─────────┐
│ Filter: [all ▾] [ used only ☐ ] [ missing ☐ ]           [Add string] [Import…] [Save]       │
│  Id   Text (campaign)                          Base / inherited              Used by        │
│  1    This realm is ruled by King Arthur …     (none)                        map00300       │
│  2    Prince Tyrael died.                      (none)                        map00300       │
│  202  Get to know                              "Introduction" (base)         [map00300] name│
│ ┌ selected string ──────────────────────────────────────────────────────────────────────┐   │
│ │ multi-line box, word wrap on (display only)         [Insert line break] [Wrap ☑]      │   │
│ └───────────────────────────────────────────────────────────────────────────────────────┘   │
│ Preview (in-game width): …                                                                  │
└──────────────────────────────────────────────────────────────────────────────────────────────┘
```

- **Word wrap is view-only.** The edit box wraps long text for reading (the same text widget option
  the script editor now has: *Word Wrap*), and **nothing is inserted into the string**. A hard line
  break is a real CR LF the author types or inserts with the button, exactly as shipped files do; the
  game wraps everything else itself. So "virtual line breaks for display" are safe by construction.
- **Preview** draws the string with the game's own font at the width of the objective / information
  panel (uses the in-game text layout function; Spike T-S2 finds it), so authors see real wrapping.
- **Filters**: only ids this campaign uses; ids used but empty in every layer ("missing"); ids
  overridden here; ids present in English but empty in this language ("untranslated").
- **Add string** takes the next free id (the smallest unused ordinal above the ids the campaign
  uses), never renumbering existing ones. **Import…** pastes a list (one string per line) into
  consecutive free ids — useful for translators working in a text editor.
- **Languages**: one tab per `[strings]` language line; *Add language* creates the file (from English
  as a template, entries marked untranslated) and adds the `[strings]` line via the campaign editor's
  writer ([08]).

## 4. Where edits go

| Scope | File | Notes |
|---|---|---|
| Campaign | the path named by `[strings] <LANG> =` in the campaign `.cfg` (e.g. `campgns/Tyraels_realms_cfg/text_eng.dat`) | created next to the campaign's config folder if the line is missing |
| Level | `<levels folder>/map%05d.<lang>.dat` | a level-local override; picked up by Save As / Playtest as a sidecar (plan 01) |

Writes are atomic (same helper as the other writers); the file is padded to the loader's 16-byte
minimum; trailing empty entries are trimmed but never internal ones (ids are positions).

## 5. Map Editor integration

- **Script ▸ Level Text…** (Map Editor menu) opens the editor at Level scope for the open map.
- The **Objective / Message helper** gains *"Store in the string file"* (in addition to today's inline
  `QUICK_OBJECTIVE`): it allocates the next free id in the level's string file, writes the text there
  and inserts `DISPLAY_OBJECTIVE(n,…)`. Long text and translation then work like every other string.
- **Verify Map** reports ids the level's script uses that no layer defines.

## 6. Slices

| Slice | Content | Tests |
|---|---|---|
| **T-S1 spike** | Reverse code-page conversion: is there an encoder? Which code pages the supported languages use; add tables to `bflib_text` (kfx_platform) if missing. | encode∘decode identity over all 256 bytes per page; documented lossy cases |
| **T-S2 spike** | The game's text layout function and the objective / information panel width, for the preview; string length limits. | notes |
| **X1** | `strings_file` (parse / serialize, byte-preserving) + `strings_stack`; read-only viewer with layers and badges. | byte-identical round trip on every shipped `.dat`; stack fixtures |
| **X2** | Editing at Campaign scope: edit box, word wrap, Insert line break, Add string, Reset, Save. | edit → write → the real loader (`get_string`) returns it (ftest) |
| **X3** | Usage scan, filters (used / missing / untranslated), validation (too long, unrepresentable characters, ids beyond `STRINGS_MAX`). | scan fixtures; validator cases |
| **X4** | Level scope + Map Editor: *Level Text…*, message-helper "store in file", Verify line. | round trip; helper inserts a valid `DISPLAY_OBJECTIVE` |
| **X5** | Preview with the game's layout; language management (*Add language*, untranslated marking); Import. | preview width equals the panel |

X1 → X2 is the first release for campaign authors. X4 needs the Level target (foundation W1–W4, [08] K5b).

## 7. Interactions

- **[08]** hosts a *Text* page and adds the `[strings]` lines; `NAME_ID` in level entries is picked from
  this editor's list; `NAME_TEXT` (literal) stays supported.
- **[04]–[07]:** every `NameTextID` / `TooltipTextID` field shows the **resolved text beside the id**
  (via `strings_stack`) and a button "edit this string" that opens this editor on that id.
- **Plan 01 (sidecars):** `map%05d.<lang>.dat` is already carried by Save As and Playtest.
- **Plan 02 (Lua):** none.

## 8. Non-goals

Base-game strings (`gtext_*.dat` in `fxdata`, read-only here), `translation.toml`, subtitles/speech
audio, font or code-page tooling beyond conversion, automatic translation.

## 9. Risks

| Risk | Mitigation |
|---|---|
| Lossy re-encoding of characters that the language's code page cannot represent | Only edited entries are re-encoded; unrepresentable characters are flagged before save and never silently replaced |
| Renumbering breaks references | Ids are positions and are never renumbered; deleting a string blanks it (inherit) |
| A shared campaign string file (several campaigns point at one file) | Same sharing check as the config folders ([08] §4) |
| Language fallback surprises (campaign has no file for the player's language) | The viewer states which file is used per language, including the English fallback |
