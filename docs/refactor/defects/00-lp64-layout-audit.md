# LP64 / 32-bit-assumption audit

Status: audit done 2026-09-20, branch `refactor-renderer`. Fixed items are listed with their commit; everything
else is a recommendation. **Follow-up the same day: §8 removed `long`/`unsigned long` from the whole tree**, which
resolves F7, F9, F10, F11 and most of F8/F15 — read §8 first; §2/§3 describe the state before it.

KeeperFX was written for 32-bit Windows (mingw i686, ILP32: `long`, `unsigned long`, `size_t`, pointers = 4 bytes).
This fork also builds natively on 64-bit Linux (LP64: those are 8 bytes). Commit `cd612b3dd` fixed a real crash
caused by exactly that (`struct ScriptValue`). This document is the audit of the rest of `src/` for code whose
behaviour silently depends on the 32-bit widths.

Contents: [1 Scope and method](#1-scope-and-method) · [2 What differs between the ABIs](#2-what-differs-between-the-abis) ·
[3 Findings](#3-findings) · [4 Verified safe](#4-verified-safe-do-not-re-audit) ·
[5 Multiplayer / save-game portability](#5-multiplayer--save-game-portability) · [6 Prioritised fix plan](#6-prioritised-fix-plan) ·
[7 Reproducing the probes](#7-reproducing-the-probes) · [8 Tree-wide fixed widths](#8-tree-wide-fixed-widths-follow-up)

---

## 1. Scope and method

Scope: all of `src/` — the eleven `src/kfx_*` libraries, `src/ftests`, `src/main.cpp`, `src/native_entry.cpp`.
Third-party code under `deps/` is out of scope.

Reasoning from source alone is unreliable for this class of bug (the ScriptValue crash looked fine on paper), so
every conclusion below rests on a probe where one was feasible. The container has `gcc -m32` (links **and runs**
32-bit binaries), `i686-w64-mingw32-g++` (the actual reference toolchain, compile-only), `pahole` and `gdb`.

| Probe | What it answers |
|---|---|
| **Record-layout diff.** Compile one translation unit that includes every state/wire/save header with `g++ -m64`, `g++ -m32` and `i686-w64-mingw32-g++`, using `-fdump-lang-class` (sizes/alignments of all 871 types) and `pahole -E` on `-g -fno-eliminate-unused-debug-types` builds (field offsets). | Which structs change size/layout between ILP32 and LP64, and *which member* is responsible. `-m32` and mingw-i686 agree on every serialized/wired struct (they only differ in `PowerConfigStats`, i.e. inside `KfxConfigState`, which is never serialized), so `-m32` is a faithful stand-in for Windows. |
| **Differential execution.** Build the same C source as `-m32` and `-m64`, run both, compare output/hashes. Used for `bflib_math.c` (RNG, sqrt, diagonal length, arctan, `angles_to_vector`, `LbMathOperation`, `get_2d_distance*`), the checksum macro, and `strtol`/`atol`. | Whether a function produces the same numbers. |
| **`-Wformat` sweep.** Add `format(printf)` attributes to the 13 variadic wrappers that had none, then syntax-check all 742 non-test translation units of `keeperfx` + `keeperfx_hvlog`, and again at `BFDEBUG_LEVEL=20`. | Every `%ld`/`%lu`/`%d` vs argument-width mismatch, including code behind `#if BFDEBUG_LEVEL`. |
| **Targeted greps + reading** for `long`/`unsigned long` in unions and raw-blob structs, `sizeof`-based checks, `LbFileRead/Write`/`LbFileLoadAt` of structs, `strtol`/`atol`, pointer casts, `1<<n` shifts, `lua_Integer`. | Everything the probes cannot reach. |
| **Regression tests.** Each fix ships a Catch2 test; where feasible it was confirmed to fail without the fix. Golden values for RNG/layout tests were generated with `gcc -m32`. | Keeps the fixes fixed. |

Known limits (be honest about them):

* **No end-to-end 32-vs-64-bit simulation run.** The 32-bit toolchain cannot link the whole game (no 32-bit SDL3 /
  OpenAL / LuaJIT here), so a Windows-vs-Linux *gameplay* determinism check was not possible. Item F15 in §3 is
  therefore a residual risk, not a finding. §6 proposes how to close it.
* `long` arithmetic in the simulation (thousands of `long` locals and parameters in `kfx_sim`/`kfx_pathfinding`) was sampled, not
  proven. Hot paths that can overflow 32 bits (squared distances, fixed-point multiplies) were read individually;
  see §4.
* Behaviour on aarch64 (LP64 but strict alignment) was not tested.

---

## 2. What differs between the ABIs

Sizes in bytes from the record-layout diff (LP64 = this fork's Linux build, ILP32 = mingw i686). All of these are
`#pragma pack(1)`/packed unless noted, so every `long`/pointer grows the struct and shifts every later field.

| Type | ILP32 | LP64 | Saved / synced as a blob? | Cause (member) |
|---|---:|---:|---|---|
| `struct Packet` | 35 | 35 | wire, replay | — (all fixed width) |
| `struct PacketSaveHead` | 124 | 124 | replay/save header | — |
| `struct CatalogueEntry` | 412 | 412 | save `INFO` chunk | — |
| `struct GameVersionPacket`, `ScreenPacket`, `DesyncChecksums` | 16 / 9 / 60 | same | wire | — |
| `struct HighScore` | 72 | 72 (was 80) | high-score file | `long score` → **fixed (F4)** |
| `struct FileChunkHeader` | **12** | **24** | every save/replay chunk header | 3 × `unsigned long` |
| `struct IntralevelData` | 67,173 | 67,333 | save chunk + continue file | `long campaign_flags[5][8]` (+160) |
| `struct KfxSimState` | 43,540,503 | 43,654,627 (+114,124) | save, resync | `Dungeon` ×9 (+76,356: `HandRule hand_rules[128][8]` +8,192 and `LevelStats`, ~70 `unsigned long`, +292 each), `Computer2` ×9 (+15,552), `CreatureControl` ×1024 (+16,384), `ComputerTask` ×100 (+4,400), `PlayerInfo` ×9, `CreatureBattle` ×192, `GoldLookup`, `StructureList`, `GuiMessage`, `TextScrollWindow`, `GameSeconds` |
| `struct KfxNetState` | 1,171,247 | 1,171,255 (+8) | save, resync | `TbFileHandle packet_save_fp` (FILE*), `long double process_turn_time` (12 → 16) |
| `struct KfxGameState` | 235,872 | 273,808 (+37,936) | save, resync | `LevelScript` (+37,920: every `unsigned long …_num`, `Condition.rvalue`, trigger/party fields), `SoundSettings` (+12), `GuiBox *gui_cheat_box_2` (+4) |
| `struct KfxFrontendState` | 1,229,064 | 1,229,088 (+24) | save, resync | `gui_cheat_box_1/3/4`, `level_names_data`, `end_level_names_data` (5 pointers) |
| `struct KfxConfigState` | 6,242,844 | 6,482,048 | **no** (rebuilt from config files) | trap/door/creature/magic config tables, 2 × `long` |
| `struct KfxRenderState` | 77 | 85 | **no** | `Thing *thing_pointed_at`, `Map *me_pointed_at` |
| `struct Game` | 1 | 1 | save, resync | — (placeholder) |
| `struct Thing`, `Room`, `Column`, `Map`, `SlabMap` | equal | equal | inside `KfxSimState` | fixed width — the hot sim data is clean |

105 project types differ in total (list in §7). The rest of the difference is either pointer-bearing UI/renderer
structs (`GuiButton`, `TbSprite`, `PurpleDrawItem`, …) that are never serialized, or config tables.

Explicit size/layout checks that exist today: `hdr.len != sizeof(...)` per save chunk (`game_saves.c:335…`),
`CONTINUE_GAME_FILE_SIZE` (`game_saves.c:124,793`), `LbFileLengthRnc() != count*sizeof(HighScore)` (`highscores.c`),
`frame_size != sizeof(struct Packet)` (`net_exchange_common.c:105`), `len != sizeof(...)` in
`resync_import_game_state`/`resync_import_frontend_state`, and a hard-coded `filesize <= 1382437` in
`is_primitive_save_version` (`game_saves.c:126`). There were **no** `static_assert`s on any layout in non-test code.
`FileChunkHeader.ver` exists, but the state chunks (`OLDS`, `KSIM`, `KNET`, `KGAM`, `KFRO`, `ILVL`, `LUA `) are all written
with `ver = 0` and it has never been bumped (only `INFO`/`PHDR` carry real versions).

---

## 3. Findings

Severity: **crash** · **data corruption** (includes data loss) · **desync** (simulation/behaviour differs between
platforms) · **cosmetic** · **benign**. "Fixed" gives the commit on this branch.

| # | Sev. | Location | What assumes 32 bits | Evidence | Recommended fix | Fixed? |
|---|---|---|---|---|---|---|
| **0** | crash | `kfx_game/include/lvl_script.h:134` `struct ScriptValue` | Union laid out by byte offset (`longs[0]` then `chars[4]`/`chars[6]`); an 8-byte `long` made the fields overlap, `QUICK_MESSAGE` icon `None` turned index 2 into `2 \| 6<<48`, out-of-bounds write in `message_add()`. | SIGSEGV reproduced; `script_message_value_layout_test.cpp`. | `int32_t longs[8]` / `uint32_t ulongs[8]`. | **Yes** `cd612b3dd` |
| F1 | desync | `kfx_platform/src/bflib_math.c:755` `LbRandomSeries` (used by every `GAME/THING/AI/PLAYER/UNSYNC/SOUND_RANDOM`) | Range and result were `unsigned long`. Callers do `RANDOM(n) - k`, `(RANDOM(20) - 10) / 2`, or pass a negative int as range: these wrap at 2³² on Windows, at 2⁶⁴ on Linux. | Diff run, seed 3: `(long)((rnd(20)-10)/2)` = `0x7FFFFFFB` (`-m32`) vs `0xFFFFFFFB` (`-m64`); negative range `-1e9`, seed 5: 465226246 vs 3760193542. 247 of 399 sampled seeds differed. About 50 call sites subtract from or divide the result (`local_camera.c:262`, `magic_powers.c:1334`, `thing_shots.c:1373`, `thing_creature.c:3605`, …). | `uint32_t` range/result. | **Yes** `42dcfc605` |
| F2 | desync | `kfx_game/src/lvl_script.c:251` and 9 more `strtol`/`atol` sites in `lvl_script*.c` | Script numbers parsed with `strtol`, which saturates at `LONG_MAX` = 2³¹−1 on Win32 but 2⁶³−1 on LP64; the value is then truncated into a 32-bit `ScriptValue` slot. | Probe: `3000000000` → 2147483647 (`-m32`) vs `-1294967296` (`-m64`, after the int32 store); `0xFFFFFFFF` → 2147483647 vs `-1`. Reproduced through `IF(PLAYER0,MONEY > 3000000000)`: `Condition.rvalue`. | `script_strtol()`/`script_atol()` clamp to int32. | **Yes** `5ff4c3dcd` |
| F3 | cosmetic (UB) | 13 variadic wrappers without a format attribute: `prepare_file_fmtpath*`, `get_*_file_path_fmt`, `load_data_file_to_buffer` (`config.h`), `message_add_fmt`, `targeted_message_add` (`gui_msgs.h`, `console_cmd.c`), `show_onscreen_msg`, `LbTextDrawResizedFmt`, `set_gui_tooltip_box_fmt` | `%ld`/`%lu` given an `int`/`LevelNumber`/`GameTurn` reads 8 bytes from the vararg slot; it only worked because the upper half happened to be zero. Includes level-file paths (`map%05lu.zip`, `map%05lu.<cfg>` in `config.c`, `config_crtrmodel.c`, `custom_sprites.c`). Also one `%d` with **no argument** (`console_cmd.c:955`) and filenames used as format strings. | Sweep: ~45 mismatches in 8 files (14 lines in `console_cmd.c`, the `%02ld` timers in `gui_msgs.c`/`front_lvlstats.c`, the level-file paths, …) plus 3 format-security uses in 2 files; `BFDEBUG_LEVEL=20` sweep found one more (`bflib_dernc.c:440`). | Add `KFX_PRINTF_FORMAT`; fix callers. The attribute is the regression guard (a mismatch is now a `-Werror` failure in both variants). | **Yes** `596370544`, `1002133ee` |
| F4 | data corruption (loss) | `kfx_config/include/config_campaigns.h` `struct HighScore`, `highscores.c:34-47,97` | Table is dumped raw to the campaign's high-score file and loaded only if `file length == count*sizeof`. `long score` made an entry 80 bytes vs the original game's 72. | Layout diff (72 vs 80); a 72-byte file from Windows/the original game is rejected as "bad" and **overwritten** with the default Bullfrog table. Test pins `sizeof == 72`. | `int32_t score`. | **Yes** `0b412de15` (an existing 80-byte Linux table is regenerated once) |
| F5 | crash | `kfx_frontend_state.h:59-61,83-84`, `kfx_game_state.h:96` | Raw-blob state structs (saved, loaded, resynced) hold pointers into the *writing* process: `gui_cheat_box_1..4` → static `gui_boxes[]`, `level_names_data`. `gui_box_is_not_valid()` dereferences them. Static-array addresses are fixed in a non-PIE Windows exe but randomized per run in a PIE Linux build. | Read of `gui_boxmenu.c:385`; unit tests fail without the fix. Needs a cheat box open at save/resync time to trigger. | Keep the live pointers across every import. | **Yes** `1affa5824` |
| F6 | crash (latent) | `kfx_net_state.h:135` `TbFileHandle packet_save_fp`; imported by `game_saves.c` (`SGC_KfxNetState`) and `net_resync.cpp:485` `memcpy(&kfx_net_state, …)` | Same defect class as F5 but for a `FILE*` and the recording-session fields (`packet_save_enable`, `packet_fopened`, `packet_fname`, `packet_file_pos`). `open_packet_file_for_load()` (`packets_misc.c:123`) stores the freshly opened handle in `kfx_net_state` and *then* calls `load_game_chunks()`, which for a replay-continue file overwrites it with the recording process's `FILE*`; the next `LbFilePosition(kfx_net_state.packet_save_fp)` uses a stale handle. A resync copies the host's recording flags and handle to a client. | Code reading only (needs a replay-continue or a recording host to trigger). | Treat the recording-session fields as process-local: capture/restore around both imports. Needs a decision on which fields are process-local (`packet_save_enable` may be meant to persist across a continue). | No — semantics need an owner |
| F7 | data corruption (cross-platform incompatibility) | `kfx_game/include/game_saves.h:80` `struct FileChunkHeader` | Three `unsigned long` fields → 12-byte header on Win32, 24 on LP64. | Layout diff. A Windows save read on Linux (or vice versa) mis-frames from the very first header; `default:` in `load_game_chunks` skips by a garbage `hdr.len` and the load ends `GLoad_Failed` — a clean failure, but it cannot succeed. | `uint32_t len,id,ver`, bump the save version. Pointless on its own — see F8/F9 — and breaks every existing Linux save, so bundle with the portable-save project. | No |
| F8 | data corruption (cross-platform incompatibility) / desync | `KfxSimState`, `KfxNetState`, `KfxGameState`, `KfxFrontendState` blobs; `net_resync.cpp:368-491` | Raw `memcpy` of state structs that contain `long`/`unsigned long`/pointers (see §2). Sim/net sizes are not part of the resync framing (only game/frontend lengths are). | Sizes differ by 114,124 / 8 / 37,936 / 24 bytes. Saves: each chunk is size-checked and rejected (`"Incompatible KfxSimState chunk"`), load fails cleanly. Resync between a 32- and a 64-bit peer: `resync_import_game_state` rejects the wrong length *before* any state is mutated (two-phase import), so the resync fails cleanly — but a mid-game desync between such peers can never be recovered. | See §6 P3 (make the blobs fixed-width) and P2 (refuse mismatched peers up front). | No |
| F9 | data corruption (cross-platform incompatibility) | `kfx_game/include/game_merge.h:101` `long campaign_flags[5][8]` | `IntralevelData` is written into every save (`ILVL` chunk) and into `fx1contn.sav` (`CONTINUE_GAME_FILE_SIZE = name + level + sizeof(IntralevelData)`, `game_saves.c:124,793`). | 67,173 vs 67,333 bytes. A Windows `fx1contn.sav` makes Linux report "no continue game". The values are already saturated to 32 bits (`saturate_set_signed(x, 32)`, `lvl_script_value.c:760`), so `int32_t` loses nothing. | `int32_t campaign_flags`. **Not done**: it changes the LP64 save/continue format (existing Linux saves would report "Incompatible IntralevelData chunk"), so it belongs in the portable-save change, not a drive-by. | No |
| F10 | cosmetic | `kfx_platform/src/bflib_math.c:674-683` `bitScanReverse`/`LbSqrL` | `__builtin_clz((unsigned long)s)` truncates to 32 bits; the seed lookup `lbSqrTable[]` only covers 32-bit values. | `LbSqrL(2^32)` = 8192 (should be 65536), `LbSqrL(2^40)` = 5792 (1048576). Below 2³² the function is identical to `-m32`. No known caller passes ≥ 2³² (the ILP32 build could not); on LP64 sums of squares in `thing_objects.c:1534` could. | Clamp to `UINT32_MAX` or use a 64-bit scan + larger table. | No |
| F11 | desync (out-of-range config values only) | `kfx_config/src/config.c:948,954` (`dt_long`, `dt_ulong` setters) | Range check `value < LONG_MIN \|\| value > LONG_MAX` (and `ULONG_MAX`) is vacuous on LP64; on ILP32 an out-of-range value is rejected with a warning and the field keeps its default. Field types come from a `_Generic` on the real declaration (`config.h:160`), so the store width itself is right. | Code reading. | Range-check against `INT32_MIN/INT32_MAX/UINT32_MAX` for `dt_long`/`dt_ulong` (same treatment as F2). | No |
| F12 | benign | Lua bindings (`kfx_script/src/lua_api*.c`, `LuaLensEffect.cpp`) | `lua_Integer` is `ptrdiff_t` in LuaJIT (`luaconf.h:106`): 32-bit on Win32, 64-bit on Linux. A `uint32_t`/`unsigned long` pushed to Lua reads negative on Win32 once it exceeds 2³¹, positive on Linux; an out-of-range Lua number stored into an `int32` wraps differently. | Code reading; no `lua_pushinteger` of an unsigned 32-bit value found in the API surface (gold is signed `int32_t`). | Convert explicitly at the API boundary if a counter that can pass 2³¹ is ever exposed. | No |
| F13 | benign (UB) | `game_saves.c:126` `is_primitive_save_version` | `(char*)&kfx_sim_state.loaded_level_number - (char*)&game` subtracts pointers into unrelated objects. | Result is arbitrary but every real save is ≫ 1.38 MB, so the outcome is always "not primitive". | Delete the check or replace with a real magic. | No |
| F14 | benign | `kfx_net_state.h:162` `long double process_turn_time` | 12 bytes on i386, 16 on x86-64; timing only, not part of any checksum. | Layout diff. | `double`. | No |
| F15 | desync (unproven) | `long` arithmetic in `kfx_sim`/`kfx_pathfinding`/`kfx_game` | Where an intermediate exceeds 2³¹ it wraps on Win32 and does not on LP64 (squared distances of far-apart objects, fixed-point multiplies). | Sampled: `ariadne.c:1396,1478` are pre-shifted (`>>5`) and bounded; `power_hand.c:809`, `thing_creature.c:8291` (`INT32_MAX-(dx²+dy²)` maximizer) only overflow beyond ~181 subtiles, outside their search radius; `dungeon_stats.c` scores are clamped. Nothing confirmed. | Differential gameplay run (§6 P5). | No |

Also fixed while in the area: `-Wformat-security` uses of `prepare_file_fmtpath`/`load_data_file_to_buffer` with a
filename as the format string (`front_landview.c:1076`, `front_simple.c:259,269`) — part of F3.

---

## 4. Verified safe (do not re-audit)

Each line says how it was verified.

**Wire / replay / lobby formats — identical bytes on both ABIs** (layout diff; pinned by
`kfx_net/tests/wire_layout_test.cpp` with sizes from `-m32`): `struct Packet` (35 B, fixed-width members, all
offsets checked), `PacketEx`, `PacketSaveHead` (124), `CatalogueEntry` (412), `GameVersionPacket` (4 × int32),
`ScreenPacket`, `DesyncChecksums`, `PacketHistoryHeader` (`uchar`+2×`uint`), `ConfigInfo` (`netconf`, two `char[]`).

**Core typedefs are fixed-width** (probe): `MapCoord`, `MapCoordDelta`, `MapSubtlCoord`, `GameTurn`,
`GameTurnDelta`, `TbBigChecksum`, `TbMapLocation`, `GoldAmount`, `HitPoints`, `TbClockMSec`, `SubtlCodedCoords`,
`LevelNumber`, `NetUserId` are all 4 bytes; `ThingIndex` 2; `TbBool` 1. `struct Thing`, `Room`, `Column`, `Map`,
`SlabMap`, `Coord3d` have no `long`/pointer.

**Sim hashing and RNG — bit-identical**, checked by running `-m32` and `-m64`:
* `CHECKSUM_ADD` (`net_checksums.c:41`, `bflib_dernc.c:576`): the rotate is on a `uint32_t`, the XOR operand
  `(ulong)value` is truncated on store; 100k mixed `int32/int16/uint8/ulong/long` values give the same checksum
  (`d93146cb`) on both. `calculate_file_checksum`, `compute_things_list_checksum`, the per-thing checksum: same.
* `LbRandomSeries` after F1; seeds are `uint32_t`; `init_seeds()`'s `unsigned long calender_time * 9311 + 9319` is
  modular so the low 32 bits match; `net_game.c:822` derives the AI/player seeds in `uint32_t`.
* `LbSqrL` (< 2³²), `LbDiagonalLength`, `LbArcTanAngle`, `angles_to_vector`, `LbMathOperation` (all 18 ops on
  random int32 pairs), `LbSinL/CosL`, `get_2d_distance`, `get_2d_distance_squared`: equal output hashes over 200k
  random inputs each.
* `dungeon_stats.c` `compute_*_score`: inputs are clamped before any multiply; no overflow possible.

**On-disk data formats read with explicit widths:** `lvl_filesdk1.c` `Legacy*` structs (as noted),
`map_content_reader.cpp` (`read_u16le/u32le`), sprite `.tab` (`spritesheet.cpp`: `uint32 offset, u8/u16 w,h`,
`pack(1)`), sound bank `.dat` (`bflib_sndlib.cpp`: `uint8[14], uint32 …`, `SoundSFXID` = `unsigned char`), RNC
(`bflib_dernc.c`: `uint16/uint32` header, `ntohl`), colour/alpha tables (`vidmode.c` `LbFileLoadAt` of
`TbColorTables`/`TbAlphaTables`/`TbRGBColorTable` — equal size), `net_config_info`.

**Unions:** `GuiVariant` (`bflib_guibtns.h:86`: `long lval`/`int32_t *lptr`/`void *ptr`/`char *str` — all members
are read back through the member they were written through; the union is pointer-sized on each ABI), `TbDItmU`
(in-memory draw list, never serialized; array element size comes from `sizeof`), `PartyTrigger`'s
`location`/`countdown` union (offsets of later fields shift but nothing indexes it by byte offset; the packed
struct is only accessed by member), `ScriptContext` (in-memory), `Coord3d`/`MapCoord` unions (fixed-width).

**Script value passing:** `ScriptValue.longs/ulongs` are 32-bit since `cd612b3dd`; `TbMapLocation` is `uint32_t`
and stored through `longs[]`; `script_process_value()` takes `long` from `longs[]`, which sign-extends exactly like
ILP32 does; `ScriptLine.np[]` is `long` but F2 now bounds it to int32 before it is stored.

**Other pointer↔int hazards:** the tree compiles `-Werror` on LP64, which rejects `(int)ptr`/`(ptr)int` casts of
different size; the only `intptr_t` uses are ImGui texture ids and `gui_tooltips.c:279` (`(void*)(uintptr_t)skind`,
round-trips). `Computer2.dungeon` (a pointer *inside* `KfxSimState`) is rebuilt by
`restore_computer_player_after_load()` (`main_game.c:228`), so the stale value from a save is harmless.
`KfxRenderState` (holds `Thing*`/`Map*`) is neither saved nor resynced.

**Formats/logging behind `BFDEBUG_LEVEL`:** all `SYNCDBG/NAVIDBG/…` argument lists compile and match their formats
at level 0, 10 and 20 after F3.

---

## 5. Multiplayer / save-game portability

Which artefacts are byte-identical between a 32-bit Windows build and this fork's 64-bit Linux build, and what
protects against mixing:

| Artefact | Portable today? | Guard | Behaviour when mixed |
|---|---|---|---|
| Gameplay packets (`Packet`, history bundles, checksums) | **Yes** | frame size == `sizeof(struct Packet)` | n/a |
| Lobby / login / version | **Yes** (fixed-width) | `net_versions_match()` compares `VER_MAJOR.MINOR.RELEASE.BUILD` only | **No bitness/ABI check**: a Win32 and a Linux64 client of the same version join the same game happily |
| Simulation results (RNG, checksums, script parse) | Yes after F1/F2; unproven beyond that (F15) | desync detector: per-turn `TbBigChecksum` over things/rooms/players/seeds | a divergence shows up as a desync report, not before |
| Resync (`net_resync.cpp`) | **No** | explicit u32 lengths for game/frontend/Lua blobs; sim/net blobs are `sizeof`-framed (no length on the wire); `resync_import_*` verify sizes before mutating anything | resync **fails cleanly** (state untouched) — a cross-ABI game that desyncs cannot be recovered |
| Savegames (`fx1g*.sav`), `fx1contn.sav` | **No** (F7, F8, F9) | per-chunk `hdr.len != sizeof` → `"Incompatible … chunk"` warning + skip; `GLoad_Failed` if any required chunk missing; `INFO` chunk `CatalogueEntry` is portable so the slot list shows the save | load **fails cleanly**; the slot appears in the list but cannot be loaded. the state chunks' `hdr.ver` is always 0 |
| Replay / packet files | Header yes (`PacketSaveHead`, fixed-width) / progress chunks no | `PACKET_SAVE_HEAD_VER`, `chunk_version_ok()` | replay from level start works across ABIs *if the sim is deterministic across them*; continue-from-save replays do not |
| High-score files | **Yes since F4** | length check | (before F4) rejected and overwritten |
| Config, campaign, map, sprite, sound, RNC files | **Yes** | explicit fixed-width/LE readers | n/a |
| `KfxConfigState`, `KfxRenderState` | n/a — never serialized | — | — |

Net: the *protocol* is portable, the *state snapshots* are not, and nothing stops a mixed-platform lobby from
forming. Cross-platform play is therefore currently "works until the first desync, then cannot resync".

---

## 6. Prioritised fix plan

**Done in this pass** (each its own commit, each with a test): F1, F2, F3, F4, F5, plus the `wire_layout_test`
guard. Full suites (`kfx_{apploop,config,editor,frontend,game,net,pathfinding,platform,render,script,sim}_utest`) pass;
`keeperfx` and `keeperfx_hvlog` build `-Werror`; `scripts/check_layering.py --strict` is clean.

**P1 — cheap, do next**
1. **F6**: decide which `KfxNetState` recording fields are process-local and preserve them across the
   savegame, replay-continue and resync imports (add a test to `net_resync_test.cpp`; factor the `memcpy` out of
   `receive_resync_game` so it is testable).
2. **F11**: `int32`/`uint32` range checks for `dt_long`/`dt_ulong` (a 5-line change plus a config test).
3. **F10**: clamp `LbSqrL`'s argument; add a test at 2³² and 2⁴⁰.
4. Add `static_assert(sizeof(...))` next to each wire/save struct that is meant to be portable (`Packet`,
   `PacketSaveHead`, `CatalogueEntry`, `GameVersionPacket`, `HighScore`, `FileChunkHeader` once fixed) — the
   unit tests catch it too, but a header-local assert fires in every build, including Windows.

**P2 — stop mixed-ABI games from forming (small, high value)**
Extend the lobby handshake with an ABI/layout fingerprint (e.g. `sizeof(long)` plus the sizes of the four state
blobs, hashed) and refuse, or at least warn loudly on, a mismatch. `GameVersionPacket` is 4 × int32 and is
compared by `net_versions_match()`; the simplest change is a fifth field. Also put the sim/net blob sizes into
the resync header so a mismatch reports "peer built for a different ABI" instead of a size error deep inside an
import.

**P3 — portable save games / resync (large, mechanical)**
Make every serialized struct fixed-width, in this order of payoff per line changed:
`FileChunkHeader` (F7) and `IntralevelData.campaign_flags` (F9) → `LevelScript` counters/`Condition.rvalue`/
trigger fields (+37,920 B; already 32-bit by intent, see F2) → `HandRule` and `LevelStats` (`unsigned long` ×~70 → `uint32_t`;
+76 KB via `Dungeon` ×9) → `Computer2`/`ComputerTask`/`ComputerProcess`/`CreatureControl`/`PlayerInfo`/`Camera`
`long` fields → `Computer2.dungeon` pointer (store a player index) → `KfxNetState.packet_save_fp`/`long double`.
Ship it as one save-format change: bump the state chunks' `ver` (currently 0) so an old save produces
"saved by an incompatible build" rather than "Incompatible KfxSimState chunk", and note that existing 64-bit
Linux saves cannot be loaded afterwards. After each step re-run the record-layout diff (§7) — the goal state is
the empty diff for `KfxSimState/KfxNetState/KfxGameState/KfxFrontendState/IntralevelData/FileChunkHeader`.
Watch `unsigned long` fields that *rely* on 32-bit wrap (none found, but the type change makes them explicit).

**P4 — F12/F13/F14** opportunistically.

**P5 — close F15 with a differential simulation run.** Two options, cheapest first:
(a) build `kfx_sim`, `kfx_pathfinding` and `kfx_platform` (not the SDL/audio parts) with
`i686-w64-mingw32` or `-m32`, drive the existing `src/ftests` scenarios through the headless entry point
in both builds and compare the per-turn `DesyncChecksums` log lines (`update_turn_checksums`,
`MULTIPLAYER_LOG`); (b) run a real Windows-host / Linux-client lobby on a soak level with
`-ftests`/`-exitonfailedtest` and read `checksums_different()`. Any first-divergent turn points straight at the
offending `long` expression.

---

## 7. Reproducing the probes

The probe sources live only in the audit session's scratch directory; this is everything needed to redo them.

Layout diff (from a configured tree, `out/ut-editor` here). `FLAGS` = the `-I…`/`-D…` flags of any kfx C++ TU
(`ninja -t commands kfx_game_utest`):

```bash
# p.cpp: #include game_legacy.h kfx_{sim,net,game,config,render,frontend}_state.h game_merge.h game_saves.h
#        save_catalogue.h packet_data.h packets.h lvl_script.h bflib_guibtns.h bflib_vidraw.h thing_data.h
#        creature_control.h   and print sizeof/offsetof of the types of interest
g++ -m64 -std=gnu++20 -fpermissive -w -fsyntax-only -fdump-lang-class $FLAGS p.cpp   # → a-p.cpp.001l.class
g++ -m32 …                                                                             # same, 32-bit
i686-w64-mingw32-g++ …                                                                 # the real reference ABI
# per-member offsets:
g++ -m64 -g -O0 -fno-eliminate-unused-debug-types … -o g64 && pahole -E -C KfxSimState g64
```

Differential run: compile the same `.c` (plus `bflib_math.c`) with `gcc -m32` and `gcc -m64`, run both, `diff` the
output. `-m32` binaries run natively here.

`-Wformat` sweep: give the format attribute to any variadic wrapper that lacks one (in the tree now), then
```bash
ninja -t commands keeperfx keeperfx_hvlog | grep ' -c .*/src/' \
  | sed -E 's# -MD -MT \S+ -MF \S+##; s# -o \S+##; s# -Werror##; s#^(\S*/(c\+\+|cc)) #\1 -fsyntax-only -Wformat #' \
  | xargs -P 28 -I{} sh -c '{} 2>&1' | grep warning
# and with -DBFDEBUG_LEVEL=20 substituted for the existing -DBFDEBUG_LEVEL=n to reach code behind #if BFDEBUG_LEVEL
```

The 105 project types whose size still differs between LP64 and mingw-i686 after this pass (none of which is on
the wire): Ariadne, Camera, CampaignsList, ColumnConfig, Columns, CompoundCoordFilterParam, CompoundTngFilterParam,
Computer2, ComputerCheck, ComputerDig, ComputerPlayerConfig, ComputerProcess, ComputerTask, ComputerType,
Condition, ConfigFileData, ConfigReloadCallbacks, Configs, CreatureBattle, CreatureConfig, CreatureControl,
CreatureJobConfig, CreatureModelConfig, CreditsItem, DebugMessage, DisplayStruct, DoorConfigStats, DraggingBox,
Dungeon, EnginePoint, FileChunkHeader, GameCampaign, GoldLookup, GraphicsWindow, GuiBox, GuiBoxOption, GuiButton,
GuiButtonInit, GuiMenu, GuiMessage, GuiVariant, HandRule, InstanceInfo, IntralevelData, KfxConfigState,
KfxFrontendState, KfxGameState, KfxNetState, KfxRenderState, KfxSimState, LevelInformation, LevelScript,
LevelStats, LocalState, LongNamedCommand, LongPoint, MagicConfig, NamedCommand, NamedField, NamedFieldSet,
Navigation, NetFrame, NetSP, NetState, Party, PartyMember, PartyTrigger, Path, Persons, PlayerInfo, Proportion,
PurpleDrawItem, ReceiveCallbacks, S3DSample, ScreenModeInfo, ScriptContext, ScriptVariableDetails, SoundCoord3d,
SoundEmitter, SoundReceiver, SoundSettings, StructureList, THate, TaskFunctions, TbBytePitch, TbDItmFlic,
TbDItmSprite, TbDItmText, TbDItmU, TbHugeSprite, TbLoadFiles, TbLoadFilesV2, TbLog, TbNetworkPlayerEntry,
TbNetworkSessionNameEntry, TbSetupSprite, TbSourceBuffer, TbSprite, TextScrollWindow, ToolTipBox, TrapConfigStats,
TrapDoorConfig, TunnelDistance, TunnellerTrigger, VideoScaleCallbacks.

---

## 8. Tree-wide fixed widths (follow-up)

Rather than fix the serialized structs one by one, every `long` / `unsigned long` in `src/` (≈9,700 uses, 560
files) became `int32_t` / `uint32_t` — the width they always had on the Windows build — and `L`/`UL` literal
suffixes were dropped. On Windows nothing changes (same width); on Linux behaviour now equals Windows. `int64_t`
was rejected on purpose: it would change layouts and wrap behaviour on *both* platforms.

Method: a comment/string-aware rewrite (code only; `long long`/`long double` untouched; `%ld`/`%lu`/`%lx` in
format strings → `%d`/`%u`/`%x`), then the compiler as checker — the `-Werror` build with the printf attributes from
F3 flags every leftover format or signature mismatch. Fallout was small: missing `<stdint.h>` in ~6 headers, the
`ulong` typedef (clashed with glibc; removed, uses → `uint32_t`), `PRIuSIZE`, an overflowing `-sizeof`, a
`LONG_MAX`, the `curl_easy_setopt` boundary (needs `long`; left as explicit `(long)` casts), and one `.05L`
long-double literal the literal rewrite had mangled (caught by the build, reverted). The Windows cross-build
(`out/windows`, mingw i686) was the second check and caught Win32-only fallout: `DWORD` is `unsigned long`, so the
three `%lx` exception-code prints (`native_entry.cpp`, `bflib_crash.c`, `PlatformWindows.cpp`) keep `long` — they
are in the lint allow-list.

`strtol()`/`atol()` return `long` and saturate at a platform-dependent limit, so they were the same bug in a
different spelling (F2 generalised): all 20 call sites now use `LbStrToI32()` / `LbAtoI32()` (`bflib_basics.h`),
which clamp to int32; `script_strtol/atol` delegate to them.

Enforcement: `scripts/check_fixed_width.py --strict` (CI, next to the layering check) rejects `long`,
`unsigned long`, `L`/`UL` suffixes and raw `strtol`/`atol` in code, with a four-entry allow-list for third-party/
Win32 API boundaries.

Results (all verified):

| Check | Result |
|---|---|
| Linux `-Werror`, `keeperfx` + `keeperfx_hvlog` + all unit tests | builds; all 11 suites pass |
| Windows mingw-i686 `keeperfx` + `keeperfx_hvlog` | builds |
| `FUNCTESTING` and `BFDEBUG_LEVEL=20` syntax sweep of every TU | clean |
| `-m32` vs `-m64` differential: RNG, geometry, `LbMathOperation`, `angles_to_vector` (300k inputs each) | identical hashes |
| Layout diff vs mingw-i686: types that still differ | **105 → 53** (the 53 are pointer-bearing UI/renderer structs and the process-pointer blobs below) |
| `FileChunkHeader`, `IntralevelData`, `LevelScript`, `Dungeon`, `PlayerInfo`, `CreatureControl`, `PartyTrigger`, `TunnellerTrigger`, `Condition` | now equal to the 32-bit sizes (pinned by `kfx_game/tests/save_layout_test.cpp`) |

What is left in the four state blobs (all still `+` on 64-bit): `KfxSimState` +36 (`Computer2.dungeon`, 9×),
`KfxGameState` +16 (`gui_cheat_box_2`, three never-read `char*` sound paths in `SoundSettings`),
`KfxFrontendState` +24 (5 pointers), `KfxNetState` +8 (`FILE* packet_save_fp`, `long double process_turn_time`).
The `SoundSettings` paths are written once (`sounds.c:411`) and never read, so stale values are harmless (benign).
Saves/resync between 32- and 64-bit builds still cannot interoperate until those pointers become indices/removed
and the `long double` becomes `double` — now a handful of fields instead of hundreds — and existing 64-bit Linux
saves no longer load (their chunks have the old sizes; the state chunks' `ver` is still 0).

Status of earlier findings: **F7 fixed, F9 fixed, F10 moot** (`LbSqrL` now takes `int32_t`, so ≥ 2³² cannot
occur), **F11 moot** (no `long` fields remain; `int32_t`→`dt_int` range-checks at INT_MIN/MAX), **F15 largely
closed** (the `long` wrap/promotion differences are gone; residual is `size_t`/pointer arithmetic and `long long`,
which are 64-bit on both build types only when declared so). F6, F8 (pointer remainder), F12–F14 stand. Still
worth doing: P2 (ABI check in the lobby handshake) and P5 (differential gameplay run), and bumping the state
chunks' `ver` so an old save gives a clear message.
