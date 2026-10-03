# S04 — `kfx_content` library

**Status:** done 2026-09-28 · **Work items:** W16 · **Depends on:** — · **Risk:**
very low · **Layout change:** none · **Estimate:** 1 day · **Callback
entries removed:** 0 (none needed)

## Goal

Move the engine-decoupled content layer (`cfgc_*`) out of `kfx_config` into
its own library, `kfx_content`. `kfx_config` goes back to being the game's
config loaders plus the callback interfaces. The editor-only content layer
gets its own boundary and its own test binary.

## Evidence (2026-09-27)

- **What moves:** `cfgc_*` is about 4 kLOC of sources plus 13 headers
  (`cfgc_campaign_check`, `_campaign_edit`, `_campaign_levels`, `_content`,
  `_document`, `_help`, `_schema`, `_schema_shapes` (+ the
  `_schema_engine`/`_creature`/`_campaign`/`_shapes` sources), `_stack`,
  `_strings`, `_validate`, `_writebatch`, `_writer`).
- **Its includes:** only `config_*.h` headers and `bflib_text.h`, all at or
  below kfx_config.
- **Its users:** no `config_*` loader includes a `cfgc_` header. The only
  users are `kfx_editor` (13 files) and `src/ftests`.

## Steps

1. `git mv src/kfx_config/{src,include}/cfgc_* src/kfx_content/{src,include}/`,
   and move `src/kfx_config/tests/cfgc_*_test.cpp` to
   `src/kfx_content/tests/`.
2. Create `src/kfx_content/CMakeLists.txt` by copying
   `kfx_pathfinding/CMakeLists.txt`'s shape (OBJECT library plus variants
   until S02 lands, glob, `kfx_common_opts`, optional Catch2 binary). Add it
   to the root `CMakeLists.txt` library list and to `kfx_common_opts`'s
   include dirs.
3. Add `kfx_content` to `scripts/check_layering.py`'s `LIBRARY_ORDER`
   directly after `kfx_config`. It could sit anywhere below `kfx_editor`;
   ranking it low lets future lower users use it.
4. Update `architecture.md`:
   - §1: the ladder diagram and the rank list;
   - §2.2: remove the content-layer paragraph from kfx_config;
   - add a §2.2b for kfx_content;
   - §2.9a: the content editors now build on kfx_content.

## As built (2026-09-28)

- 40 files moved with `git mv`: 16 sources, 13 headers, the 10 `cfgc_*`
  tests and their one fixture (`campaign_check_snapshot.txt`). The tests get
  their own generated path header (`kfx_content_test_paths.h`,
  `KFX_CONTENT_TEST_REPO_ROOT`/`_FIXTURES_DIR`) and the `[kfx_content]` tag.
- `kfx_content` is a STATIC library like the others (S02 had already removed
  the build variants). Registered in the root `CMakeLists.txt` (include dir,
  `add_subdirectory` right after kfx_config, the coverage target's
  dependencies), `check_layering.py`'s `LIBRARY_ORDER` directly after
  kfx_config, the callback inventory's ranks, `kfx_editor_utest`'s link line,
  and the unit-test target lists in CI (`build-prototype.yml`),
  `build-coverage-core.sh` and `build-package.sh`.
- Library sizes after the move: kfx_config 45 sources / 49 headers,
  kfx_content 16 / 13.
- **Checks:** Linux and Windows builds, both layering checks, 2033 Catch2
  tests (89 of them now in `kfx_content_utest`), and all 24 editor and
  config-content ftests.

## Verification

- Both builds and both layering checks pass.
- `kfx_content_utest` passes, and so do the editor ftests
  (`editor_brush`, the campaign-editor ftests).

## Upstream-merge notes

None: `cfgc_*` is fork-only code. The ledger carries one `file` row.
