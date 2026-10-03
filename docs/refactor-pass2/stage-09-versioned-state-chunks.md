# S09 — Versioned state chunks

**Status:** done 2026-09-28 · **Work items:** prerequisite from `00-analysis.md` §9
· **Depends on:** — · **Enables:** S10, S11, the kfx_model storage step, and
any future field move between state structs · **Risk:** low–medium ·
**Layout change:** introduces the mechanism · **Estimate:** 3–4 days ·
**Decision (2026-09-27):** breaking old saves is acceptable, so there are no
migrations. Old saves are refused cleanly.

## Problem (verified in `kfx_game/src/game_saves.c`)

- **The chunk format already allows versioning, but nothing uses it.** Saves
  are chunked: `struct FileChunkHeader { len, id, ver }`. Every state chunk
  (`SGC_GameOrig`, `SGC_KfxSimState`, `SGC_KfxNetState`,
  `SGC_KfxGameState`, `SGC_KfxFrontendState`) is written with `ver = 0`.
- **Loading a state chunk with a different size logs "Incompatible … chunk"
  and skips it.** The load then fails (`GLoad_Failed`: not every
  `SGF_SavedGame` flag was set). By then, the chunks read *before* the
  skipped one have already been copied into the live state structs, and the
  player only sees a generic load failure. So any layout change silently
  makes every existing save unloadable, with no explanation and possibly
  half-overwritten state.
- **The same raw structs travel in network resync** (`net_resync.cpp`) and
  level reset. Resync is between identical binaries (multiplayer requires
  matching versions), so it needs no migration, only the version constants
  kept in step.

## Goal

Make "move a field between state structs" routine: a version number per
state struct, bumped on every layout change, and a **clean refusal** for
saves (and continue-replays) from an older layout, instead of today's
partial overwrite and generic failure.

## Design

1. **Version constants.** Each state header gets one: `#define
   KFX_SIM_STATE_VER 1`, `KFX_NET_STATE_VER`, `KFX_GAME_STATE_VER`,
   `KFX_FRONTEND_STATE_VER`, `KFX_LIGHTS_VER` (lights are their own resync
   blob today).
   - The writer puts them in `hdr.ver`.
   - A `_Static_assert(sizeof(struct KfxSimState) == KFX_SIM_STATE_SIZE_V1, …)`
     next to each constant makes a layout change without a version bump fail
     to compile. The expected size is recorded per build target (Windows
     x86 and Linux x64 differ), so the assert compares against a table
     keyed by `sizeof(void*)`.
2. **Loader policy, per chunk.**
   - Same version and size: accept.
   - Anything else: **refuse the whole save** with a player-visible message
     ("This save is from a different KeeperFX build and can't be loaded").
   - There are no migrations (decided 2026-09-27: breaking old saves is
     acceptable). If that policy ever changes, the version numbers give a
     place to hang a migration table later; nothing here needs redoing.
3. **Release notes.** Every release containing a version bump says "saved
   games from earlier versions can't be loaded". Keep one line per bump in
   `docs/refactor-pass2/README.md`'s decisions section, so the release
   manager can find them.
4. **Two-phase load.** Read and validate every chunk into temporary buffers
   first. Copy into the live state
   structs only once all of them are accepted. A refused save then leaves
   the running game untouched.
5. **Resync:** send the version constants in the resync header, and reject
   a mismatch (it can only happen between different builds, which
   multiplayer already forbids). There is no migration on this path.
6. **Replays** (`-packetload`):
   - *start* replays (`SGF_PacketStart`) re-simulate from level start and
     store no state structs;
   - *continue* replays (`SGF_PacketContinue`) embed the same state chunks
     as a save (`save_packet_chunks`), so they go through the same version
     check and refusal path.

## Steps

1. Add the version constants, the size table and static asserts, and have
   the writer set `hdr.ver`.
2. Loader: the two-phase read, the version check, and the refusal path
   with its UI message (reuse `create_error_box`). Also refuse in the
   save-list UI: show old saves greyed out with "older version", using
   the version in the chunk header, without trying to load them.
3. Resync header versions.
4. Test harness in `kfx_game/tests/`:
   - a Catch2 test that a save written by the current build loads;
   - a test that a save with a bumped `ver` in one chunk header is
     refused, **and that no live state struct was modified** (compare a
     checksum of all state structs before and after), which covers the
     two-phase load.

## Verification

- Build; Catch2 for kfx_game (new save tests); the save/load ftests.
- Manual: a save from the previous release is refused with the message,
  the game stays usable, and the save list marks it as from an older
  version.

## As built (2026-09-28)

Code commit `b7d53c482`.

- **One header, `kfx_config/include/state_versions.h`,** holds every
  version and expected size: `KFX_GAME_ORIG`, `KFX_SIM_STATE`,
  `KFX_NET_STATE`, `KFX_GAME_STATE`, `KFX_FRONTEND_STATE`,
  `KFX_INTRALEVEL` and `KFX_LIGHTS` (resync only). It sits in kfx_config so
  kfx_net's resync can see the game and frontend versions too.
  - The bump instructions are at the top of the header.
  - Each struct's size `_Static_assert` sits next to its definition, in its
    own library's `.c`: `kfx_sim_state.c`, `kfx_net_state.c`,
    `kfx_game_state.c`, `kfx_frontend_state.c`, `game_legacy.c`,
    `game_merge.c` and `light_data.c`.
- **One size per struct, not a per-target table.** The sizes were measured
  with both toolchains (the Linux x86-64 symbols and an i686-w64-mingw32
  probe) and are identical. The structs use explicit `int64_t` fields and
  packed layouts, as `save_layout_test.cpp` already assumed. A target that
  ever differs fails its build at the assert. CI builds only mingw and
  Linux, so no MSVC build was checked.
- **Version 1 everywhere.** Every earlier save carries version 0 and is
  refused; the README decisions table has the release-note line. The Lua
  chunk stays unversioned: its contents are Lua's own serialisation, not a
  C struct.
- **Two-phase load, as a validation pre-pass rather than temporary
  buffers.**
  - `validate_save_chunks()` walks every chunk header and checks each
    chunk's version and length, and that the chunk is complete in the file.
  - `load_game_chunks()` runs it first and refuses the whole file before
    anything is applied. That matters: the info chunk alone switches
    campaign and reloads configs.
  - This gives the same guarantee as copying the ~150 MB of state into
    buffers first, without the copy.
  - Continue-replays go through the same function.
  - `last_save_was_refused()`/`last_save_refusal_reason()` let callers tell
    a refusal (game untouched) from a failure.
- **UI.**
  - The save list sets an in-memory `CEF_OtherVersion` flag per slot, using
    the same pre-pass.
  - The ImGui load lists (main menu and in-game) show those saves greyed
    out as "(other version)". The in-game save list labels them the same
    way, and they can still be overwritten.
  - A refused in-game load shows an error box. `create_error_box_text()`
    is new, because the message has no translated string yet.
  - The main-menu load stays in the menu instead of quitting as it does on
    other failures. The classic load button gets the same branch; its
    visuals are unchanged.
- **Resync.** A `struct ResyncVersions` header leads the full resync data,
  and the receiver rejects any mismatch before parsing the rest.
- **Tests.** `kfx_game/tests/save_versions_test.cpp` (6 cases):
  - A save written by the real `save_game_chunks()` validates.
  - Another version, version 0, another size, and a file cut in half are
    each refused.
  - `load_game_chunks()` on a mismatched save leaves every live state
    struct's bytes unchanged. The test renames the info chunk so that a
    one-pass loader would reach the state chunks.
  - With the pre-pass disabled, five of the six fail, including the
    untouched-state check.
- **Docs.** architecture.md §6.2, the upstream-merge workflow's state-field
  item and CLAUDE.md now describe the bump rule.
- **Checks:** Linux and Windows builds (the asserts pass on both), both
  layering checks, all Catch2 suites (2047 cases), and the 70-test ftest
  sweep. All 70 pass, including `ai_seat_identity` (a real save, then
  load) and `net_resync_fake_multiplayer` (a full host-to-client resync
  through the new version header).
- **Still to do by hand:**
  - Load a save made before this stage: it should be listed as "(other
    version)" and not load.
  - Save and load in-game.

## Upstream-merge notes

Upstream has neither these state structs nor this save format. There is
nothing to reconcile, but every merge that brings a new *field* into one of
the structs is a layout change, so the merge bumps the version. Add that
check to `upstream-merge-workflow.md` §5 ("state fields moved to different
homes").
