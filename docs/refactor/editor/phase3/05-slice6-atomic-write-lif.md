# Phase 3, slice 6 — atomic write + `.lif`

Status: **done.** Two self-contained items, both flagged as known gaps since slice 1, tackled
together per the user's own request.

## Atomic write

- `deps` unaffected — no new dependency, just a new `kfx_platform` primitive.
- `LbFileSaveAtomic(fname, buffer, len)` (`bflib_dernc.h`/`.c`, alongside `LbFileSaveAt()`, its
  direct sibling): writes to `<fname>.tmp` via the existing `LbFileSaveAt()`, then replaces the
  real destination with it. ISO C `rename()` on Windows (unlike POSIX) fails outright if the
  destination already exists, so this calls `LbFileDelete()` on the destination first when
  present — not perfectly atomic (a crash in the narrow window between that delete and the
  rename could leave neither file present), but strictly safer than the in-place write it
  replaces, which could leave a half-written file even on an ordinary failure (partial
  `LbFileWrite`, disk full) with no fallback at all.
- `MapContentWriter`'s `save_text()` helper (`map_content_writer.cpp`) now calls this instead of
  the old direct `LbFileSaveAt()` — every file every writer produces (`.slb`/`.own`/`.inf`/`.txt`/
  `.tngfx`/`.lgtfx`/`.aptfx`/`.lof`/`.lif`, classic `.tng`/`.lgt`/`.apt`) goes through the one
  helper, so this one change covers all of them.
- Catch2: `bflib_dernc_test.cpp` gained three cases — a fresh write round-trips, an existing
  file's content is genuinely replaced, and no `.tmp` sibling is left behind after success.

## `.lif`

- Investigated before writing anything: real shipped classic content (`core_files/campgns/
  lqizgood/map00210.lif`, no `.lof` alongside it at all) confirmed `.lif` — not `.lof` — is what
  real Free Play discovery for classic maps actually relies on. `.lof` turns out to be a
  KFX-editor-specific addition; `.lif` is the original, real mechanism.
- The slice-2 tracking doc's original "shared, multi-entry registry file, read-existing-then-
  merge" framing for why this was deferred turned out to be avoidable:
  `find_and_load_lif_files()` (`lvl_filesdk1.c`) globs **every** `*.lif` file in the directory and
  accumulates their entries — a per-level `map%05u.lif` with just that level's own one-line entry
  works identically to one large shared file, with no merge logic needed at all.
  `MapContentWriter::write_lif()` (concrete on the base class, same "format-independent" reasoning
  as `write_level_info()`/`.lof`) writes exactly that: `"<lvnum>, <name>\r\n"`, byte-for-byte
  matching the real shipped file's own format (verified directly against it, not just inferred
  from the parser). No-op when the level has no name yet — an unnamed entry would fail
  `level_lif_entry_parse()`'s own validation, so nothing is written rather than writing a
  malformed line.
- Wired into `write()`'s normal orchestration (every Save/Save As/Playtest gets `.lif` now, same
  as `.lof`) and into `write_level_info_only()` (so Level Settings' Apply action keeps `.lif` in
  sync with a renamed level too, not just `.lof`).
- Catch2: two new cases in `map_content_roundtrip_test.cpp` — a named level's `.lif` matches the
  real shipped format exactly (byte-for-byte, read directly off disk since `.lif` is write-only
  from `MapContentReader`'s own side, never read back into a `MapContent`), and an unnamed level
  writes no `.lif` file at all.
- ftest: `ftest_editor_save_reload`'s existing `TEST_LEVEL_NAME` save already exercises this
  through the real engine — confirmed live after the run: `core_files/campgns/keeporig/
  map90001.lif` contains `"90001, Editor Save Reload Test\r\n"`, byte-for-byte matching the real
  shipped format, with no stray `.tmp` file left behind.

## Verification

- `python3 scripts/check_layering.py --strict` — no new violations.
- `keeperfx`/`keeperfx_hvlog` (native Linux) build clean.
- `kfx_platform_utest`/`kfx_sim_utest` — full suites pass with no regressions (694/2087
  assertions — up from 685/2080 by exactly the new test cases added, nothing else changed).
- Full ftest sweep — 22/22, no regressions; real `.lif` files confirmed written correctly by the
  actual engine run, not just in isolated Catch2 fixtures.
