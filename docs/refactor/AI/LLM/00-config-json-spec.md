# Config content JSON surface — specification (draft)

Status: **draft spec, nothing built.** This is W6 of
[fx plan 10](../../editor/fx-plans/10-config-content-model-and-writers.md), written down ahead of the code
as decided there (§11.2: "design JSON now, build when the first tool needs it"). It defines how an
external tool — an LLM agent, a script, a CI check — reads, plans and applies changes to KeeperFX
configuration content through the same library the editors use (`kfx_config`: `ConfigStack`,
`ConfigSchema`, `ConfigContentWriter`, `WriteBatch`), with no game running and no engine state.

The map side of the same goal (map content as data) is in the phase-3 map-serialization docs; this
document covers **configuration files only** (`.cfg`), plus the extension points for the campaign and
strings work (plans 08, 09).

## 1. Goals and non-goals

**Goals**

1. Everything an editor can do to a config file is expressible as one JSON `ChangeSet`.
2. An agent can discover what exists (`describe`), see the current effective state and where each value
   comes from (`read`), preview an edit without touching disk (`plan`), and commit it atomically
   (`apply`).
3. Output is **deterministic** (stable ordering, no timestamps, no absolute paths) so diffs, golden
   fixtures and agent transcripts are exact.
4. Mistakes are reported, not hidden: everything the real loader would silently clamp, ignore or misread
   comes back as a diagnostic *before* anything is written.
5. Safe by construction: an agent names **layers and keys**, never file paths; nothing outside the target's
   directories can be written.

**Non-goals (this version)**

- Creating new content kinds (new creatures, new traps as data) — v1 of the editors is view/edit only
  (plan 03 decision 3), and `plan`/`apply` follow the same rule by default (§8.3).
- TOML-based kinds (`effects`, `slabset`, `columnset`): a slot is reserved (`format: "toml"`), no shapes.
- Live game control (the in-game TCP JSON API in `kfx_script/src/api.c` is a separate channel for a running
  game; §9 says how the two relate).
- Maps. Terrain, things, script files are a different content family.

## 2. Vocabulary

| Term | Meaning |
|---|---|
| **kind** | A configuration file type: `trapdoor`, `rules`, `objects`, `terrain`, `magic`, `creaturemodel`. Later: `creature`, `crstates`, `cubes`, `lenses`, `keepcompp`. |
| **layer** | `base` (game `fxdata/`), `campaign` (the campaign's `CONFIGS_LOCATION`), `level` (`map%05d.<file>` beside the level). Creature models use the parallel creature directories. Mods are not modelled yet. |
| **target** | Which game data tree, campaign and level a request is about (§3). |
| **section id** | The canonical block id: `trap2`, `door1`, `common`, `game`, `research`. `trap02` and `trap2` are the same id (the loader matches by number). |
| **field** | One key of a section. It has one *value text* per occurrence in a file; the schema says how to read the text. |
| **value text** | The raw text after the key, inline `;` comment removed (`"TNT64 TNT32"`, `"LINE_OF_SIGHT_90"`, `"25"`). Numbers and names are **never** converted by the surface — the schema gives the grammar. |
| **ChangeSet** | An ordered list of edits (§6). |

## 3. Targets

Requests never contain file paths. A target is resolved by the host (a CLI wrapper, the game, a test) from
its own configuration into a `ConfigTarget` (`base_dir`, `base_crtr_dir`, `campaign_cfg_dir`,
`campaign_crtr_dir`, `level_dir`, `level_number`).

```json
{ "target": { "campaign": "keeporig", "level": 1 } }
```

| Field | Meaning |
|---|---|
| `campaign` | Campaign or mappack name as the host knows it. Omitted: only the `base` layer exists. |
| `level` | Level number. Omitted: no `level` layer. |

A host may refuse to resolve a target (unknown campaign) — error `target_unresolved` (§10). Host-specific
extras (a sandbox root, a read-only flag) are host configuration, not request fields.

## 4. Envelope

Every request and response is one JSON object.

```json
{ "v": 1, "op": "read", "target": { "campaign": "keeporig", "level": 1 }, "kind": "trapdoor", ... }
```

```json
{ "v": 1, "ok": true, "op": "read", ... }
{ "v": 1, "ok": false, "error": { "code": "unknown_kind", "message": "..." } }
```

- `v` is the spec version (this document: `1`). A host rejects a request with a higher `v`
  (`unsupported_version`) and answers a request without `v` as `1`.
- Responses always carry `ok`. On `ok: false` there is exactly one `error`; **diagnostics are separate**
  and appear inside successful and planned responses (§7).
- Key order inside objects is fixed by this spec's examples; arrays keep the order given here (file order
  for content, schema order for fields). Consumers must not rely on object key order, producers must
  emit it stably.

## 5. Data shapes

### 5.1 Schema (`describe`)

Reflects `ConfigSchema`. Built from the engine's field tables, so it cannot drift from the loaders (plan 10
§7.4–7.5).

```json
{
  "kind": "trapdoor",
  "file": "trapdoor.cfg",
  "sections": [
    {
      "basename": "trap",
      "numbered": true,
      "list_replaces": false,
      "fields": [
        { "key": "NAME",  "state": "known", "parts": [ { "kind": "text" } ] },
        { "key": "SYMBOLSPRITES", "state": "known",
          "parts": [ { "kind": "icon" }, { "kind": "icon" } ] },
        { "key": "ACTIVATIONLEVEL", "state": "known",
          "parts": [ { "kind": "custom", "min": 0, "max": 9, "default": 0 } ] },
        { "key": "TRIGGERTYPE", "state": "known",
          "parts": [ { "kind": "enum", "min": 0, "max": 255, "default": 0,
                       "enum_names": ["LINE_OF_SIGHT_90", "PRESSURE_TRIGGERED"] } ] },
        { "key": "CRATE", "state": "known",
          "parts": [ { "kind": "enum", "enum_registry": "object" } ] }
      ]
    },
    { "basename": "common", "numbered": false, "fields": [
        { "key": "TrapsCount", "state": "ignored", "note": "the count is derived from the numbered blocks; the loader does not read it",
          "parts": [ { "kind": "number" } ] } ] }
  ]
}
```

Field members (omitted when default): `state` (`known` | `ignored`), `note`, `alias_of` (canonical key this one
duplicates), `whole_string` (the loader takes the rest of the line), and `parts[]` — one entry per
whitespace-separated value: `kind`, `min`, `max`, `default`, `enum_names` (static list, case-insensitive),
`enum_registry` (names come from the section basenames listed in §5.2).

`kind` values: `number`, `enum`, `flags`, `text`, `icon`, `anim`, `string_id`, `effect_ref`, `function`,
`coord`, `custom` (engine-specific grammar, only presence checked), `unspecified` (key known, grammar not
described yet).

Rules the consumer must apply:

- `state: "ignored"` — the key appears in shipped files but this build's loader does not read it. **Never
  offer it as a control; never write it.** Writing it is accepted (it is valid text) with an `ignored_key`
  info diagnostic.
- `alias_of` present — write the **canonical** key (`alias_of`), not this spelling; reads may return
  either.
- `min`/`max` are already clipped by the C data type; a value outside is clamped by the loader and reported
  as `number_range`.
- `list_replaces: true` on a section — the block replaces the whole lower-layer block (`[research]`,
  `[sacrifices]`); see §6.3.

### 5.2 Registries

Names defined by other files, by registry: `trap`, `door`, `slab`, `room`, `object`, `shot`, `spell`,
`power`, `special` (from each numbered block's `Name`), later `creature`, `instance`, `effect`, ….
`describe` may include them so an agent can pick valid names:

```json
{ "registries": { "door": ["WOODEN", "BRACE", "STEEL", "MAGIC", "SECRET"], "object": ["..."] } }
```

Registry names are upper-case in output; input compares case-insensitively.

### 5.3 Content and effective state (`read`)

Two views, chosen by `view`:

`"view": "layer"` — the **sparse content of one layer** (what is in that file):

```json
{
  "kind": "trapdoor", "layer": "level", "present": true, "partial": true,
  "sections": [
    { "id": "trap1", "basename": "trap", "index": 1,
      "fields": [ { "key": "Health", "values": ["91"] }, { "key": "Shots", "values": ["82"] } ] }
  ]
}
```

`"view": "effective"` (default) — the **merged result** with provenance, for the sections/keys asked
for (`sections`: list of ids, or omitted for all; `keys`: optional filter):

```json
{
  "kind": "trapdoor",
  "layers": { "base": true, "campaign": true, "level": true },
  "sections": [
    { "id": "trap1",
      "fields": [
        { "key": "Health", "values": ["91"], "source": "campaign" },
        { "key": "Shots",  "values": ["82"], "source": "level", "beneath": ["92"] }
      ] }
  ]
}
```

- `source` is the highest layer that set the key; `beneath` is the merged value without that layer and is
  absent when nothing lower sets it. Together they are what makes "no override needed" decisions and
  "what does this layer change" answers possible.
- Multi-value keys are merged position by position (`SymbolSprites = other` over `big med` reads
  `"other med"`), exactly as the loader assigns them.
- List sections carry one `values` entry per line.
- A key nobody sets is **absent**, not defaulted; the schema's `default` says what the loader then uses.
- Large reads are paged: `limit` (default 200 sections) and `after` (a section id); the response carries
  `next` when more remain.

### 5.4 Diagnostics

```json
{ "severity": "warning", "code": "number_range",
  "message": "Hidden: 5 is above the maximum 1 and would be lowered to it",
  "section": "trap1", "key": "Hidden", "layer": "level" }
```

`severity`: `info` | `warning` | `error`. `code` is stable and machine-readable:

| Code | Severity | Meaning |
|---|---|---|
| `number_range` | warning | Number outside the field's range; the loader clamps it. |
| `not_a_number` | warning | A number was expected; the loader reads 0 and warns. |
| `unknown_name` | warning | Name not in the field's list or registry. |
| `too_few_values` | warning | Fewer values than the key has parts (the rest keep the lower layer's). |
| `missing_value` | warning | A numeric key with no value. |
| `unknown_key` | warning | Not a key of that section. The line is still written (raw editing), the loader ignores it. |
| `unknown_section` | warning | Not a block of this file kind. |
| `ignored_key` | info | Key the loader does not read in this build. |
| `duplicate_block_ignored` | warning | (Planned, from plan 10 §8d.) A block repeats an earlier block's exact name; the loader never reads it. |
| `malformed_change` | error | A change lacks a section or a key; **nothing** is applied. |

Diagnostics never block a write by themselves (plan 10 §4.5); the caller decides. Errors in this list mean
the request itself is invalid.

### 5.5 File effects (`plan` / `apply`)

```json
{ "layer": "level", "file": "map00001.trapdoor.cfg", "action": "create",
  "diff": "--- /dev/null\n+++ map00001.trapdoor.cfg\n@@ -0,0 +1,5 @@\n+; KeeperFX Partial ... -- written by the map editor.\n+\n+[trap1]\n+Name = BOULDER\n+Shots = 82\n" }
```

`action`: `create` | `patch` | `delete` | `none`. `file` is a **file name relative to the layer
directory**, never an absolute or relative path. `diff` is a unified diff (LF line endings in the diff
regardless of the file's own endings; a line ending change is marked `\ No newline` style). `none` means
the file is byte-identical after the change (a no-op or an override equal to the lower layer).

## 6. ChangeSet

```json
{
  "layer": "level",
  "changes": [
    { "op": "set",   "section": "trap1", "key": "Health", "value": "91" },
    { "op": "set",   "section": "trap1", "key": "SymbolSprites", "value": "other" },
    { "op": "reset", "section": "trap1", "key": "Shots" },
    { "op": "reset_section", "section": "trap4" },
    { "op": "replace_list", "section": "research", "key": "Research",
      "values": ["MAGIC POWER_HAND 250", "ROOM TREASURE 100"] }
  ]
}
```

`layer` is the layer being written: `campaign` or `level` (writing `base` needs `allow_base: true` and is
refused by default, §8.3).

### 6.1 Operations

| `op` | Fields | Effect |
|---|---|---|
| `set` | `section`, `key`, `value` | Sets one key. `value` is a single **value text**; multi-value keys are written as their words joined by a blank. |
| `reset` | `section`, `key` | Removes the key from this layer; the lower layers' value shows through. |
| `reset_section` | `section` | Removes every key of the block from this layer. |
| `replace_list` | `section`, `key`, `values[]` | Replaces all lines of a list key in this layer. |

### 6.2 What the writer guarantees (plan 10 §4.4, verified by golden and corpus tests)

- Only the touched lines change. Spacing, `=` or blank separator, key case, inline comments and line endings
  of every other line survive; setting every key of a shipped file to its own value changes nothing.
- `set` rewrites the **last** occurrence of the key in the block (the one the loader keeps).
- A new key goes after the block's last key, spelled as the file already spells it. A new numbered block is
  appended with `Name` copied from the layer beneath.
- **No-op elimination**: a value equal to what shows through from the lower layers is not written; an
  existing line for it is removed. The response reports it as `unchanged`.
- A block left with no keys and no authored comments is removed. A generated file (its first comment carries
  "written by the map editor") left with no keys is **deleted**; an authored file is kept.
- Created files start with the standard partial-file header line plus that marker.
- Changes apply in order; a later change sees the effect of an earlier one. All-or-nothing per request.

### 6.3 List sections

`[research]` and `[sacrifices]` replace the lower layers' block **whole** when a layer contains the block.
Therefore:

- `replace_list` on a key of such a block, in a layer that does not have the block yet, also writes the
  block's other keys from the layer beneath (so `MkGoodHero` survives an edit of `MkCreature`).
- An emptied list block is **kept** (`replace_list` with `values: []`): an empty block clears the lower list.
  It is not a reset. To go back to the lower list, use `reset_section`.

### 6.4 Aliases and ignored keys

Writing an alias key writes that spelling (accepted). Agents should use the schema's canonical key. An
`ignored` key is accepted with an `ignored_key` info diagnostic and has no effect in the game.

## 7. Operations

All operations are pure functions of (target on disk, request). Only `apply` writes.

### 7.1 `describe`

Request: `{ "op": "describe", "kind": "trapdoor", "registries": true }`, or `{ "op": "describe" }` for the
list of kinds (`kinds: [{ "kind", "file", "sections": [basename...], "layers_supported" }]`).
Response: the schema (§5.1) and, if asked, the registries built from the target's files (base first, then
campaign, then level). `target` is optional (registries need it).

### 7.2 `read`

Request: `{ "op": "read", "target": {...}, "kind": "trapdoor", "view": "effective",
"sections": ["trap1"], "keys": ["Health"] }` — for `creaturemodel` add `"name": "imp"`.
Response: §5.3. Includes `layers` (which layer files exist). No file writes.

### 7.3 `plan`

Request: `{ "op": "plan", "target": {...}, "kind": "trapdoor", "changeset": {...} }`.
Response:

```json
{
  "v": 1, "ok": true, "op": "plan",
  "applied": 2, "unchanged": 1,
  "diagnostics": [ { "severity": "warning", "code": "unknown_key", ... } ],
  "files": [ { "layer": "level", "file": "map00001.trapdoor.cfg", "action": "create", "diff": "..." } ],
  "digest": "sha256:9f2c..."
}
```

Runs the real `apply()` on in-memory documents. `digest` is a hash over (target, request, the current bytes of
every file the plan touches); it lets `apply` refuse to run on a tree that changed since the plan (§7.4).

### 7.4 `apply`

Request: `{ "op": "apply", "target": {...}, "kind": "trapdoor", "changeset": {...},
"expect_digest": "sha256:9f2c..." }` (`expect_digest` optional but recommended).
Response: the `plan` response plus `"committed": true`. Semantics:

- Recomputes the plan; if `expect_digest` is given and differs → `stale_plan`, nothing written.
- Writes through `WriteBatch`: every file is staged, then moved into place, and any failure restores the
  previous state. A crash leaves `.kfxold` backups, never a half-written file.
- `apply` with only `none`/no-op effects succeeds with `committed: true` and writes nothing.
- Diagnostics do not block. A host may add a policy (`refuse_on: "warning"`); a refused apply returns
  `blocked_by_diagnostics` and writes nothing.

### 7.5 `validate`

Request: `{ "op": "validate", "target": {...}, "kind": "trapdoor", "layer": "level" }`. Response:
diagnostics for the existing file as the loader would see it (including `duplicate_block_ignored`,
`unknown_key`, `number_range`). Read-only; the JSON equivalent of the editors' problem list.

### 7.6 Multi-kind requests

A campaign edit touches several files. `apply` accepts `"kinds"` in place of `"kind"`:

```json
{ "op": "apply", "target": {...},
  "edits": [ { "kind": "trapdoor", "changeset": {...} }, { "kind": "rules", "changeset": {...} } ] }
```

One `WriteBatch` for all of them: all files land or none does.

## 8. Safety and policy

1. **No paths in requests.** Layers and kinds are enumerations; file names in responses are relative names.
   The host resolves directories once from the target and refuses anything else.
2. **Writes are confined** to the target's `campaign` and `level` directories (and creature directories for
   `creaturemodel`); base files are game data and are protected (`layer_not_writable`).
3. **Content policy (v1):** `set`/`replace_list` may not create new numbered blocks beyond the layer-merged
   count unless `"allow_new_blocks": true` is given (creating content is out of scope for the v1 editors).
   `reset_section` on a block that exists only in that layer deletes an item's overrides only, never the
   base item.
4. **Size limits:** request ≤ 1 MiB, ≤ 500 changes, values ≤ 1024 bytes (the loader's own line limit is
   1024), reads paged. Larger requests return `request_too_large`.
5. **No shell, no code:** values are text written between `=` and a comment; a value containing a
   newline is rejected (`invalid_value`), so a value can never inject a second key line.
6. **Secrets and personal data** are not part of any kind; nothing here reads outside the game's data tree.

## 9. Transport bindings (not part of the core)

The core is a library taking and returning JSON text. Three thin bindings are anticipated; none is decided.

| Binding | Sketch |
|---|---|
| **Command line** | `keeperfx-config <op> [--target-file t.json]`, JSON on stdin, JSON on stdout, exit code 0 on `ok`. Runs without a window or any game data beyond the config directories. Natural fit for CI checks and for an LLM harness that shells out. |
| **MCP / tool-use server** | The five operations as five tools with the JSON Schemas of §11. The tool descriptions carry the guidance of §12. |
| **In-game TCP API** (`kfx_script/src/api.c`, port 5599) | A `config` command family forwarding to the same library for the *running* game's data tree, so an agent connected to a live session can edit level-scope config and trigger a reload. The reload semantics belong to the game side and are out of scope here. |

The editors' Raw view (plan 03 F3) and this surface share `plan`: the editor's "changes on disk" diff is the
`files[].diff` of a plan.

## 10. Errors

| Code | When |
|---|---|
| `unsupported_version` | `v` higher than the host supports. |
| `bad_request` | Not JSON, or a required member is missing (`message` names it). |
| `unknown_op` / `unknown_kind` | Not one of the listed values. |
| `target_unresolved` | The host cannot resolve `campaign`/`level` to directories. |
| `layer_not_writable` | Base, or a layer the target does not have (no campaign directory, no level). |
| `invalid_value` | A value contains a line break or a control character. |
| `request_too_large` | §8.4. |
| `stale_plan` | `expect_digest` does not match the tree now. |
| `blocked_by_diagnostics` | Host policy refused an apply. |
| `write_failed` | The batch could not complete; `message` names the file. **Nothing changed** (the batch restored its backups). |
| `malformed_change` | See §5.4; returned as `error` when any change is malformed. |

## 11. Golden fixtures and tests

W6 delivers `ConfigJson` (encode/decode of the shapes above) and a fixture directory of request/response pairs,
so the surface is testable without a model in the loop.

- `fixtures/describe_trapdoor.json` — schema of one kind (checked against `build_engine_schema()`).
- `fixtures/read_effective_layers.json` — base + campaign + level trees on disk, effective and beneath.
- `fixtures/plan_set_new_layer.json` — a `set` in a layer with no file: `create` with the header and the
  copied `Name`.
- `fixtures/plan_noop_equal_to_base.json` — the no-op rule: `unchanged: 1`, action `none`.
- `fixtures/plan_reset_last_override.json` — generated file deleted.
- `fixtures/plan_list_replace_seeded.json` — `[sacrifices]` seeding.
- `fixtures/apply_atomic_failure.json` — a write that fails midway restores everything.
- `fixtures/diagnostics_catalogue.json` — one request per diagnostic code.

Tests: (1) round trip `decode(encode(x)) == x` for every shape; (2) each fixture pair executed against a scratch
directory built from the fixture's `tree`; (3) the schema fixture regenerated from the engine tables must equal
the checked-in file (fails when a loader gains a key); (4) fuzz: random ChangeSets never produce a file that
`ConfigDocument` cannot round trip and never a second key line from one value.

JSON Schemas for the requests and responses are generated from the same C++ structures (one source of truth)
and checked in next to the fixtures; the MCP binding publishes them as tool input schemas.

## 12. Guidance an agent needs (tool description text)

The surface is only useful if the model uses it safely. The tool descriptions should say, in short form:

1. Call `describe` once per kind, then `read` before you `set`: values have **units and ranges** and some keys
   do nothing (`state: ignored`).
2. Ask for the **narrowest layer**: edits for one level belong in `level`, edits for one campaign in
   `campaign`. Do not copy base values into an override; the writer drops them and reports `unchanged`.
3. Use `plan` first; read the `diff` and the diagnostics; then `apply` with the plan's `digest`.
4. Write **value texts**, not JSON numbers: `"value": "91"`, `"value": "TNT64 TNT32"`. Names are the game's
   own names; check them against `registries`.
5. Prefer `unknown_key`/`unknown_name` warnings as errors: they mean the game will ignore or clamp the value.
6. Never invent section ids: existing items are `trap0`…`trapN-1`; creating items is not available unless the
   host allows it.

## 13. Extension points

- **Strings and campaign (plans 08, 09):** the same envelope, kinds `campaign` and `strings`, and different
  content shapes: the campaign as a typed document (`[common]`, `[mapNNNNN]`, list keys) and the strings file
  as an ordered array of entries with a language code. `ChangeSet` gets `set_entry` / `insert_entry` for
  strings; everything else (plan/apply/digest/atomic batch) is unchanged.
- **Curated schema overlays** (creature, spell, special keys today `unspecified`): the schema shape already
  carries grammar per key; overlays only fill it in.
- **Help text:** an optional `help` member per section and field, lifted from the comments of the base files
  (plan 10 §4.2), so `describe` can teach an agent what a key does. Not part of v1 fixtures.
- **Mods** (after-base / after-campaign / after-map lists): a `mods` array on the target when modelled.
- **TOML kinds:** `format: "toml"` on the schema and a `TomlConfigWriter` child; the ChangeSet is unchanged.

## 14. Open questions

1. **Digest scope.** Hash the touched files only (proposed) or the whole target directory? The former allows
   unrelated edits during a plan; the latter is stricter.
2. **Diff format.** Unified diff (proposed) versus structured line edits; unified is friendlier for humans and
   models, structured is easier to re-apply. Could ship both.
3. **`set` with multi-value keys.** Whole-line text (proposed, mirrors the file) versus a `values[]` array per
   part. The latter would make partial-position edits explicit instead of relying on the loader's overlay
   rule.
4. **Where does the CLI live?** A tools target in the CMake build (`tools/`), or a hidden `keeperfx` mode?
5. **Reload hook.** For the in-game binding, does `apply` trigger a config reload, or is that a separate
   `reload` call the agent makes deliberately?
6. **Policy defaults** (`allow_new_blocks`, `refuse_on`) — per host, or per request with host ceilings?
