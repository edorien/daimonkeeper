# Contributing fixes back to upstream (dkfans/keeperfx)

The reverse of [upstream-merge-workflow.md](upstream-merge-workflow.md). That
document brings upstream's commits into this fork; this one takes bugs from
upstream's issue tracker, fixes them here first, where a test can prove the
bug and the fix, and then offers the same fix to upstream as a pull request.

Upstream has no unit-test harness, so the fork's Catch2 suite and `src/ftests/`
are what make a fix here worth more than a patch written blind: every PR says
how the bug was reproduced and shown fixed, even though the test itself stays
in the fork.

The route has four steps:

1. pick bugs from the triage docs
2. reproduce each with a test (Catch2 preferred, an ftest otherwise) and fix it in the fork
3. port the fix to a branch of upstream's tree
4. draft the PR for review: overview, testing, AI disclosure

Nothing goes to GitHub until the user has reviewed the draft: the code, the
message, and whether to send it at all. Pushing the branch and opening the PR
are separate steps, done only on request.

The [`issue-fix-upstream`](../../.claude/workflows/issue-fix-upstream.js)
workflow automates steps 1-4 and stops before any push. Run it with
`args: {count: 3, date: "YYYY-MM-DD"}` to let it pick, or
`{issues: [1234, 5678], date: ...}` for chosen issues. It works through the
same steps as below, so this document is also the reference for reading its
results.

## 0. Setup (once)

- **Triage docs:** `docs/plans/issues/`, local and git-ignored. Every open
  upstream issue was classified there (defect, feature, other) and checked
  against the fork's code. Each issue that applies to the fork has its own
  file with a code pointer. `_data/` holds the raw data (`issues.json`, the
  triage results), `status.json` (what has been fixed, and where it went
  upstream) and `render.py`, which regenerates the indexes after a status
  change. `upstream-prs.md` holds the PR drafts and their outcomes.
  `fork-findings.md` lists bugs found in our own code along the way.
- **Upstream checkout:** `~/projects/keeperfx`, a separate clone of
  dkfans/keeperfx, never this repo. Its `origin` is dkfans with the push URL
  disabled. Its `fork` remote is `edorien/daimonkeeper`, GitHub's fork of
  dkfans/keeperfx, which PR branches are pushed to.
- **Upstream build check:** `KFX_OS=windows BUILD_DIR=out/windows ./build-cmake.sh`
  in that clone, i.e. mingw i686, the target upstream's CI builds. Its native
  Linux build expects system `-dev` packages this machine doesn't have.

## 1. Pick

Take bugs from `docs/plans/issues/defects/README.md`, skipping anything
already in `_data/status.json` (fixed, or parked; see step 2) or listed as "left out" in `upstream-prs.md`.
Check `gh pr list --repo dkfans/keeperfx --state open` first: four of the
first batch's candidates already had an open PR from someone else (#5243,
#5127, #4974, #4303).

Good candidates:

- the triage file points at code, and the bug is in logic a test can drive
- the right behaviour isn't a design question
- the fix is small and local
- upstream's code has the same bug

Leave out, and say why in `upstream-prs.md`:

- semantics changes upstream should settle first (#2675's hand-rule
  allow/deny meaning)
- bugs whose cause needs the reporter's map (#4837)
- fixes that would change AI strength or content behaviour beyond the bug
  (#235)

## 2. Reproduce and fix in the fork

Test first, on `refactor-renderer`:

- **Catch2** (`src/kfx_<lib>/tests/`, docs/refactor/testing/00-overview.md):
  preferred for anything a fixture can set up: config parsing, sim and AI
  decisions, helpers. The test must **fail without the fix, for the reported
  reason**, and pass with it. Check both, and record both in the commit or
  the report.
- **ftest** (`src/ftests/`): when only a running level shows the bug. It
  needs a separate `-DKFX_FUNCTESTING=ON` tree with data staged from
  `core_files` (src/ftests/README.md, testing-harness.md §7), always runs
  `-headless`, and never runs on the live display.
- **No test possible** (rendering-only, or a path that opens a desktop
  dialog): say so. The PR's Testing section then says how the fix was checked
  instead, and an in-game check by the user before sending is worth more than
  usual. The status flower (#3127) and the cursor light (#5301) were like
  this, and #5301's first version needed a follow-up once it was played.

Then fix it. Keep comments rare: one line where the code would mislead
without it, never a restatement of the code, and no issue numbers or history
in code comments (those belong in the commit message). Run the library's whole `*_utest` and
`python3 scripts/check_layering.py --strict`, and commit with an
`(upstream #N)` reference. Other sessions share this checkout, so stage only
your own files.

Record the outcome in `docs/plans/issues/_data/status.json` and run
`python3 docs/plans/issues/_data/render.py`. The render updates three places.
The top-level README gets a Work done table. Each folder's README (for example
`defects/README.md`, the sorted list) gets the Status column: fixed, partial or
related, plus the upstream PR link or "draft", with fixed issues listed last.
The issue's own file gets a Status line and a Work done section.

When a bug is investigated but not fixed, write down what was learned. This
covers a no-go at assessment, a test that wouldn't reproduce it, a fix that
failed review, or a fix that stayed in the fork. Append a dated section to
`docs/plans/issues/_data/notes/<issue>.md` covering:

- where the code is, in the fork and upstream (`file:line`)
- the cause, confirmed or ruled out, and the dead ends
- the test approach, and whether it reproduced the bug
- the fix plan, and what happened to it
- what would unblock it

Then park the issue in `status.json` with one of four statuses:

- `unconfirmed`: no defect was confirmed in the code, because the triage's
  mechanism didn't hold.
- `design`: the code matches the report, but the fix is a design or policy
  choice for upstream.
- `blocked`: the bug is plausible or partly confirmed, but needs a
  reproduction or a located cause.
- `upstream-pr`: someone else's open upstream PR covers it.

Give it a `since` date and a one-line note saying what would unpark it.
Parked issues show `⏸` in their folder README and are listed in the
top-level README's Parked table. Step 1 skips them unless their blocker
has demonstrably changed: a new reproduction, an upstream decision, or the
covering PR closed without being merged. The render puts the notes
themselves into an "Investigation notes" section of the issue's file. The next attempt
reads them first instead of starting over. The issue files are regenerated on
every render, so notes go in `_data/notes/`, never into the issue file
itself.

## 3. Port to upstream

In `~/projects/keeperfx`:

```bash
git fetch origin
git checkout -b upstream/<slug> origin/master
# apply the fix by meaning, in upstream's flat src/
cmake --build out/windows --target keeperfx -j"$(nproc)"
```

- **Paths:** the mapping runs the other way from
  upstream-merge-workflow.md §2. Find the function by symbol in upstream's
  flat `src/`, not by our file path. `python3 scripts/move_ledger.py lookup
  <symbol>` gives the fork side of a moved function.
- **Types and names differ:** upstream's `ThingModel` is int16 where ours is
  int64, it uses `long`/`int` where we use `int64_t`, and it uses `game.`
  where we use per-library state structs. A size or width bug can therefore
  mean something different there. #5475's `memcmp` compared half the victim
  list upstream and less than one victim here.
- **Upstream's code may have moved on.** For #5301, upstream switches the
  local cursor light on every frame from its renderer, which the fork doesn't.
  The port needed a second change the fork fix didn't. Read the upstream code
  around the fix, not just the lines you changed.
- **No test in the port:** upstream has no harness for it. The PR describes
  the fork's test instead.
- **One branch per fix, one commit per branch,** message in upstream's
  style. **No AI co-author trailer** in upstream commits (upstream's policy,
  set out in their Discord). Disclosure goes in the PR text.

## 4. Draft the PR

Add the draft to `docs/plans/issues/upstream-prs.md`: branch, commit, title,
body, and a risk note for the reviewer. The body has three sections:

- **Overview:** what was wrong, the cause in upstream's code (function
  names), and what the change does. End with "Fixes #N." or "Related to #N.".
- **Testing:** how the bug was reproduced in the fork (the test's name and
  what it asserts, failing before the fix and passing after), that the port
  builds with the mingw i686 CMake build, and plainly whether it was run in
  game.
- **AI disclosure:** "Found and written with AI assistance (Claude Code). The
  AI drafted the root-cause analysis, the test and the patch; I reviewed the
  change and this description before opening the PR." Only send it once that
  last sentence is true.

## 5. After review: push and open

Only for the drafts the user approves:

```bash
git -C ~/projects/keeperfx push -u fork upstream/<slug>
gh pr create --repo dkfans/keeperfx --base master --head edorien:upstream/<slug> \
  --title "<title>" --body-file <body.md>
```

Record the PR number in `status.json` and on the draft's heading, then
re-render. Keep the branch until the PR is merged or closed. A fix to an open
PR is a new commit on its branch, not a force-push, and is pushed only after
the user's go-ahead, as with #5479's follow-up. Update the
description to match (what the new commit does, and the Testing line once it's
been played). `gh pr edit` fails on this repo with a "Projects (classic)"
deprecation error. Use the REST API instead:
`gh api -X PATCH repos/dkfans/keeperfx/pulls/<n> -F body=@<body.md>`.

When upstream merges one of these PRs, the next upstream merge brings it back
into the fork as a no-op, because the fork already has the fix. The merge's
ledger should note that, so the duplicate isn't mistaken for a conflict to
resolve.

## Worked example: the first batch (2026-10-18)

Nine PRs, dkfans/keeperfx #5475-#5483. Six came from the triage's own
findings, and three were bugs found in the code while checking issues (the
spiral bounds, the vein loop, the pretty-map offset). The fork commits,
branches and PR links are in `docs/plans/issues/upstream-prs.md` and
`_data/status.json`. That batch predates this procedure's test-first rule:
most of its fixes were checked by reading the code. Use it as a reference
for the porting and drafting, not the testing.
