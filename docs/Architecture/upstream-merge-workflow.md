# Merging upstream (dkfans/keeperfx) into this fork

This fork's `src/` is reorganized into layered `src/kfx_*/` libraries
(architecture.md), while upstream (`origin` remote, dkfans/keeperfx `master`)
stays a flat, pre-refactor tree. A plain `git merge` between these two shapes
produces mostly add/add and modify/delete conflicts rather than clean
line-level ones — rename detection can't help, since the split is a real
move-and-rewrite, not a rename. This document is the repeatable procedure for
doing that merge safely, refined against the 2026-09 merge of 16 upstream
commits (33e94e51b..c375b20a4) into `refactor-renderer`.

Upstream has no unit-test harness at all; this fork does
(`docs/refactor/testing/`, Catch2 + `src/ftests/`). The workflow below is
**coverage-first**: before applying the merge, add tests for areas the
incoming commits touch but this fork doesn't yet cover, so the merge lands
provably not-worse than before and any regression is caught immediately
rather than discovered live.

## 1. Establish the range

Find the last merge's own merge-base (its second parent, on the upstream
side) rather than assuming the previous merge commit's own SHA:

```bash
git log --oneline --merges -5   # find the last "Merge origin/master..." commit
git log -1 --format="%H %P" <that merge commit>   # its parents
```

The second parent is upstream's tip at that time — use it as the range start.
Comparing straight against this fork's own tree is misleading once the trees
have structurally diverged; the **true** diff of what's new is:

```bash
git fetch origin master
git log --oneline <last-merge-base>..origin/master     # commit list
git diff --stat <last-merge-base>..origin/master        # real flat-layout diff
```

## 2. Map upstream paths to current `kfx_*` paths

For every file the true diff touches, find its current home:

```bash
find src -iname '<basename>'
```

Most files keep their basename (occasionally `.c` → `.cpp`). Build a table:
upstream path → current path → owning `kfx_*` library. Two upstream-path
categories need special handling:

- **A file this fork already deleted** during its own earlier refactor (e.g.
  `keeperfx.hpp`, removed in this fork's Stage 13.1) has no single successor.
  `git merge` reports these as modify/delete conflicts. Resolve by grepping
  each individual symbol the upstream hunk touches (not the filename) to find
  where it lives now.
- **A file whose logic moved into a *different* file** (e.g. an upstream
  function now implemented elsewhere as part of an earlier stage of this
  fork's own refactor) — same treatment: find the real current home by
  symbol, not by path.

## 3. Cluster the commits

Group commits by shared file/subsystem, not one at a time. Several commits in
a range often touch the same function in sequence (a fix, then a follow-up
fix to the same fix); handle each cluster together so a later commit's tests
aren't written against an already-obsolete intermediate state.

## 4. Assess coverage per cluster, add tests where it's missing

For each cluster, decide what's actually practical — not a blanket rule:

- **Pure/near-pure function, one library's own state** → a focused Catch2
  `TEST_CASE` in that library's `tests/` dir (`docs/refactor/testing/
  stage-02-testability-and-fakes.md`'s Pattern A: memset-reset fixture over
  the library's `extern` state struct). Write it against **current**
  (pre-merge) behavior, confirm it passes, then flip the expectations to the
  new intended behavior once the merge actually lands — the test should prove
  the behavior really changed, not just that new code compiles.
- **`static` function, or embedded deep inside a large stateful function**
  (e.g. a helper folded into a per-tick creature-update loop) → don't make it
  non-static just to unit-test it (stage-02's explicit non-goal: "not a
  rewrite for testability"). Use a targeted `src/ftests/` functional test if
  there's a clean go/no-go outcome in a real level, otherwise a manual-QA
  checklist item.
- **Network I/O** → check for an existing long-running ftest exercising the
  same code path (e.g. `net_enet_loopback_host`/`_join`) before inventing a
  new mechanism.
- **`kfx_script` (Lua) / `kfx_apploop`** → no Catch2 harness exists for
  either yet (deliberately deferred, needs its own fixture design). Use an
  ftest for Lua-driven behavior instead of forcing a first Catch2 test here
  as a side effect of an unrelated merge.
- **Trivial/cosmetic fixes** (alignment, font size, an icon) → no automated
  test; call it out as a manual-verification item in the merge commit instead
  of forcing coverage that wouldn't catch a real regression anyway.

Land these tests **on top of the pre-merge tree**, as their own commit,
before touching the merge itself — this is the regression net the merge will
be judged against.

## 5. Perform the merge

```bash
git merge origin/master --no-edit
```

Expect most touched files to conflict. Resolve **file by file**, guided by
the path-mapping table from step 2: for each conflict, pull up the real
upstream diff for that specific commit (`git show <sha> -- <upstream path>`)
and re-apply it to the current file **by function name**, not by line number
— line numbers never line up across the reorganization. A few recurring
patterns from the 2026-09 merge:

- **A cross-layer call upstream made directly, that this fork routes through
  a callback struct.** If the current (lower-ranked) file already calls the
  same thing via `sim_feedback->foo(...)` / `render_overlay->foo(...)`
  elsewhere, keep that shape — don't adopt upstream's direct call. If the
  callback member doesn't exist yet for a genuinely new upstream call, add it
  (new struct member + no-op default in the config-layer `.c` + real wiring
  in `main.cpp`'s `setup_game()`), mirroring the existing members around it.
- **A state field the two sides moved to different homes.** If upstream
  moves something into its own `struct Game`, but this fork's `struct Game`
  is a near-empty placeholder (per architecture.md §6), the field almost
  certainly belongs in one of the per-library `extern` state structs instead
  — pick the lowest-ranked library among its real consumers, matching how
  the rest of that state struct is organized.
- **A whole-file "our side is empty" conflict.** Before assuming the content
  is missing, `grep` for each function's current definition across
  `src/kfx_*/`. If every function in the block already exists elsewhere
  (this fork relocated it during its own earlier refactor), the block is
  dead duplicate content — drop it, keeping the empty side.

## 6. Verify

```bash
python3 scripts/check_layering.py --strict
python3 scripts/check_layering_symbols.py --strict
KFX_OS=linux ./build-cmake.sh           # keeperfx
KFX_OS=linux ./build-cmake.sh keeperfx_hvlog
cmake --build out/linux_tests --target kfx_platform_utest kfx_config_utest \
  kfx_pathfinding_utest kfx_sim_utest kfx_render_utest kfx_net_utest \
  kfx_game_utest kfx_frontend_utest kfx_script_utest kfx_apploop_utest -j"$(nproc)"
ctest --test-dir out/linux_tests --output-on-failure
```

A clean full build is the real test of whether every conflict resolution was
correct — the compiler and linker will surface anything a conflict-marker
diff alone can miss. Two classes of error are common and not really about
the conflicted files themselves:

- **Unconflicted-but-stale references.** A file with zero conflict markers
  can still reference a global upstream renamed elsewhere in the *same*
  commit, if that particular line's surrounding context happened to
  auto-merge cleanly. `grep -rn` for the old name across the whole tree after
  resolving markers, not just in files `git status` flagged as conflicted —
  and re-check after every fix, since fixing one file sometimes surfaces the
  same stale name in a sibling file the build hasn't reached yet.
- **Test files with the same staleness.** `src/kfx_*/tests/*_test.cpp` files
  reference production fields directly (`player->some_field`); if a merge
  relocates that field, every test touching it needs the same fix as
  production code. The compiler only reports these once `ctest`'s build
  target actually compiles that file, so don't treat a clean `keeperfx`
  build as sufficient — build and run the test suite too.

Once green, update the coverage-first tests from step 4 to their post-merge
expected values (if step 4 wrote a "before" assertion) and re-run.

## 7. Commit and record gaps

One merge commit, `git commit` (not `--no-edit` at this point — write a real
message). Document: which upstream PRs it contains, what got new automated
coverage and where, and which fixes are manual-QA-only with no automated
check — so a future contributor can tell "no test" was a decision, not an
oversight. If verification surfaces a pre-existing, unrelated gap (e.g. a
test file already broken by an *earlier* merge, needing more than a
mechanical fix), don't scope-creep it into this merge — flag it separately
and note it in the merge commit as a known, tracked gap.

## Worked example

The 2026-09 merge of 33e94e51b..c375b20a4 (16 commits) is the reference
case this document was extracted from — see its merge commit message for the
concrete per-cluster coverage decisions, conflict resolutions, and the
follow-up it spun off (`src/kfx_net/tests/net_checksums_test.cpp`, broken by
the *previous* merge, needing a real fixture redesign beyond this workflow's
mechanical-rename cases).

## Handling a GPU / renderer-branch commit inside the range (2026-09-19 merge)

Upstream sometimes lands large renderer-backend work (the OpenGL commits, `opengl-renderer-review.md`)
that this fork deliberately rejected. Do not `git merge origin/master` in one go for such a range:

1. Work in a **separate worktree/branch** (`git worktree add ../<dir> -b merge-upstream-<date>`), not the
   shared dirty tree. Keep a per-commit ledger (`docs/merge-checks/upstream-merge-<date>.md`).
2. Classify every commit in the range by files touched (`git show --numstat`). Merge **non-GPU commits in
   batches** by SHA (`git merge <sha> --no-commit --no-ff`, resolve, commit) and **each GPU commit alone**.
3. A GPU commit is recorded with `git merge -s ours --no-commit <sha>` (keeps ancestry, imports nothing),
   *then* hand-port only hunks that are demonstrable software-path bug fixes. Read its non-GL hunks in full.
   Put doubtful ones (visual redesigns, state-machine hardening) in the ledger as flagged candidates.
   `-s ours` fails silently ("Merge with strategy ours failed", producing a one-parent commit) if anything is
   staged — do it on a clean index and check `git log -1 --format=%P` shows two parents.
4. Some later "non-GPU" commits only repair the GPU commit's own regressions or gate GPU-only functions
   (`08816fbef`, `b8344493e`): record them `-s ours` too, after checking our tree really lacks the bug.
5. Author does not decide: a commit by the GPU author can still be pure perf/profiling that changes
   simulation order (`702188136` reorders `update_things`) — review the logic, not the label.

Mechanics that saved time:
* For a file this fork deleted/relocated, `git diff <sha>~1 <sha> -- <old path> | sed 's#a/old#a/new#;s#b/old#b/new#' | patch -p1 -F3 -l`
  applies onto the new path; then read every "fuzz 3" hunk — some land in the wrong function.
* Never `git add <dir>` while conflicts remain in it (it marks conflicted files resolved). Use
  `git grep -n '^<<<<<<<'` as the source of truth; `git checkout HEAD -- <file>` if you staged one by accident.
* When upstream calls a higher layer (kfx_net from kfx_sim/kfx_render), add a lower-layer helper reading
  `PlayerInfo::user_id` (`get_user_player_number`) or a callback member — not a new upward include.
* Positional callback-struct initialisers in `main.cpp`/`*_callbacks.c`: insert the new member at the same
  position in the struct, the no-op table, and `setup_game()`.
* Coverage-first still applies per fix: write the test, prove it fails (`git stash push <src>`), apply the fix.
  Watch for tests that pass "by accident" after a semantic change (`checksums_different` tests that used the
  magic `action = 1`, which is `PckA_QuitToMainMenu`).
