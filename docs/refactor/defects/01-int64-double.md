# 01 — 64-bit integers and doubles everywhere

Status: implemented on branch `wide-int64` (worktree `dk-fx-v2-wide`), **not merged**. Builds and passes on Linux
(x86-64) and the mingw-i686 cross-build; unit suites and the headless functional-test sweep match the pre-change
baseline (see §5). Read §6 (risks) before merging: this is a very large, mostly mechanical change to a game whose
simulation depends on exact integer behaviour, and it changes the save-game and multiplayer formats.

Follows [00-lp64-layout-audit.md](00-lp64-layout-audit.md), whose fix (`long`→`int32_t`) made behaviour match the
32-bit Windows build. This step goes further, by request: one integer type and one float type for the whole game, so
that no platform-dependent width or wrap point exists anywhere, and no pointers hide in serialized state.

## 1. The rule

| Kind | Type |
|---|---|
| every integer (`int`, `short`, `long`, `unsigned`, `int16_t`, `int32_t`, `uint16_t`, `uint32_t`, `ThingIndex`, `GameTurn`, …) | `int64_t` / `uint64_t` |
| every floating-point value | `double` (`float`, `0.5f`, `sqrtf`… removed) |
| unchanged | `char`, `unsigned char`/`uint8_t` (bytes, flags, text), `bool`/`TbBool`, `size_t`/`ptrdiff_t` (sizes), `long long`, `long double` |

Unsigned 16-bit types (`unsigned short`, `uint16_t`, `ThingIndex`, `RoomIndex`, …) became **signed** `int64_t`, not
`uint64_t`: they promoted to (signed) `int` in expressions, so `a - b` was already signed; `uint64_t` would have made
every such subtraction wrap to 2⁶⁴. `unsigned int`/`uint32_t` became `uint64_t`.

Fixed-point simulation maths stays **integer** (positions, velocities, `>> 8`, `/ 256`): converting it to `double`
would make results depend on FMA contraction, x87 vs SSE and libm differences between builds — the classic cause of
Windows-vs-Linux multiplayer desyncs. Only genuine `float`s became `double`.

Third-party calls convert explicitly (`static_cast`/temporaries): SDL out-parameters (`int*`, `float*`), ImGui widgets
(wrappers in `kfx_platform/include/kfx_imgui.h` copy to an `int`/`float` temporary and back), libcurl (`long`),
qsort/SDL/spng/centijson/Lua callback signatures (keep `int`), `main`/`WinMain`, Win32 `DWORD`.

## 2. Explicit exceptions (the allow-list, enforced by the lint)

These stay a fixed narrow width because the *width is the definition*, not an implementation detail:

* **File formats of the original game:** `.tng/.apt/.lgt` (`Legacy*` structs), `.clm` columns (`LegacyColumn` +
  `column_from_legacy/to_legacy`), `creature.tab` (`KeeperSpriteDisk`), sprite `.tab` (`sprite_entry`), sound banks
  and RIFF headers, RNC header, FLI headers, land-view window offsets, the 16-bit `.dat` column indices.
* **Packed overlays:** `ScriptValue` (`shorts[16]`/`longs[8]` are byte ranges of one 32-byte union, see
  `script_message_value_layout_test.cpp`), the coordinate unions (`Coord3d.x.stl.pos/num` are now 8/16-bit
  bit-fields of the same 64-bit word as `val`, reproducing the original byte layout and the unsigned-16-bit `num`).
* **32-bit algorithms:** RNG state (`uint32_t` seeds; 32-bit LCG + rotate), `TbBigChecksum` (rotate-by-5 accumulate),
  wibble-table seed (signed 32-bit wrap reproduces the original table), the 32-bit shadow-cache bit masks,
  CPUID registers, ARGB pixel writes, ImGui colours (`ImU32`).
* **32-bit hardware emulation:** `bflib_render_gpoly.c` and `bflib_render_trig.c` (the world polygon and triangle rasterisers) emulate x86 32-bit
  `adc`/`sbc`/`rol` on packed `TexCoord` words; widening them corrupts every world texture (found by eye in a live
  game, not by tests or ftests). Both files keep their original types. The sound-bank reader had two more file-format reads into widened locals (`head_offset` at EOF−4, the RIFF form type), which silently disabled all sound effects while music (a different path) still played.
* **Wire/interop:** `Packet`, `GameVersionPacket`, `ScreenPacket`, `PacketHistoryHeader` (explicit widths), the
  network service-provider message fields (`wire_u32()` in `bflib_netsp.cpp`).
* **Library boundaries:** see §1.

`scripts/check_fixed_width.py --strict` (CI) is a ratchet over `scripts/fixed_width_baseline.json`: 673 lines in 88
files remain, all of the kinds above (Lua `int f(lua_State*)` signatures dominate); a file may go down, never up.

## 3. Pointers in saved/synced state

The five raw blobs (`KfxSimState`, `KfxNetState`, `KfxGameState`, `KfxFrontendState`, `IntralevelData`) now contain
no pointers, `long double` or floats (verified with `pahole -E`):

* `Computer2.dungeon` → `dungeon_idx` (index into `kfx_sim_state.dungeon[]`, `-1` = none); accessors
  `computer_dungeon()` / `computer_set_dungeon()`. (This also fixes a latent crash: a resync copied the host's
  process-local address to the client.)
* GUI-box pointers (`gui_cheat_box_1..4`) moved out to non-serialized `kfx_frontend_local` / `kfx_game_local`;
  `packet_save_fp` (`FILE*`) to `kfx_net_local`; the three write-only sound-path pointers were deleted;
  `long double process_turn_time` → `double`.
* The earlier "capture and restore around import" workaround (F5 in doc 00) is gone; tests now assert that an import
  leaves the process-local pointers alone.

`KfxRenderState` (holds `Thing*`/`Map*`) is neither saved nor synced and was left alone.

## 4. Results

| | Before | After |
|---|---|---|
| project types whose layout differs between mingw-i686 and x86-64 | 105 (53 after doc 00) | none in any serialized struct: every integer is an explicit 64-bit type, no pointers |
| `KfxSimState` | 43.6 MB | 139.6 MB |
| `KfxNetState` / `KfxGameState` / `KfxFrontendState` / `IntralevelData` | 1.17 MB / 236 KB / 1.23 MB / 67 KB | 2.94 MB / 440 KB / 1.23 MB / 82 KB |
| `Thing` / `CreatureControl` / `PlayerInfo` / `Dungeon` | 2,239 / 10,949 / 4,736 / 67,452 B | 8,657 / 20,147 / 8,554 / 163,512 B |

`save_layout_test.cpp` and `wire_layout_test.cpp` pin these sizes: they are now identical on every platform, and a
stray narrow type or pointer changes a number.

## 5. Verification

* Linux `-Werror`: `keeperfx`, `keeperfx_hvlog`, all 11 unit suites (config 31,417 assertions, sim 3,312, …) pass.
* mingw-i686 cross-build of `keeperfx` and `keeperfx_hvlog` compiles.
* `FUNCTESTING` and `BFDEBUG_LEVEL=20` syntax sweeps of every translation unit are clean.
* Headless functional tests (`-ftests -exitonfailedtest -headless`, original game data): the wide build and a build
  of the commit before this whole effort pass the **identical** set of 28 tests (movement/pathing, creatures and
  rooms, multiplayer resync, GUI packet parity, editor undo/save/reload/strokes…). Both stall on the same test
  (`editor_brush`) afterwards, so the last few tests are unverified either way.

Bugs found only by running the game (unit tests did not catch them) — the reason to keep the ftest sweep in the loop:

1. `set_coords…` / `slide_thing_against_wall_at` and ~120 other places use 32-bit hex masks (`x & 0xFFFFFF00`). A
   `0xFFFFFF00` literal is a positive `unsigned int`, so on a 64-bit operand it also clears the sign and the upper
   half: a position of −16 became 2³² and every falling particle was "teleported". All such literals are now written
   `((int64_t)(int32_t)0x…)` (sign-extended). Same class: `& 0xFFFFFFFE` on a `size_t` pointer in the FLI recorder
   (truncated a 64-bit pointer since before this work) → `& ~(size_t)1`.
2. A 16-bit unsigned sentinel produced by `return -1` (`NavColour`, `NAV_COL_UNSET == USHRT_MAX`) → the sentinel is
   now `-1`; `mapblk->col_idx = -lword(...)` relied on int16 wrap → explicit `(int16_t)` cast.
3. Named-field config setters switch on the *C type* of the field (`_Generic`); the `dt_short/dt_int/dt_long…` cases
   had been widened along with everything else and overwrote neighbours; restored, and the unsigned-64 range limit is
   `INT64_MAX`.
4. Raw memory reinterpretations that assumed 32-bit ints: `*(int *)i` over a byte table (`player_utils.c`), a
   Column read at a byte offset (`engine_render.c`), `xor_encoded_address` memcpy'd 4 bytes into an 8-byte variable
   (STUN), network message header words, cpuid register array (stack smash), sprite run-length words, `lighting_bitmask`.
5. `abs()` truncates to `int` in C and is ambiguous for `uint64_t` in C++ → `llabs` / overload; `std::min/max/clamp`
   reject mixed types → mixed-type overloads in `globals.h`.

## 6. Risks and known limits — read before merging

* **The committed branch `refactor-renderer` (commit `e578a377f`, "long → int32_t") crashes the software renderer at
  the first frame of the first functional test** (`draw_gpoly_line`, write 4 GiB outside the framebuffer): code that
  kept a *negative* pixel offset in an `unsigned long` was correct on LP64 (2⁶⁴ wrap = subtraction) and became `+4 GiB`
  when the type shrank to `uint32_t`. Unit tests and syntax sweeps did not see it. It is fixed on this branch (64-bit
  offsets) and absent before `e578a377f`. **Do not ship `e578a377f` without either this branch or a revert.**
* Unsigned-narrowing idioms are the residual risk of any blanket widening: `unsigned short x = -1` (→65535),
  `(uint16_t)(a - b)`, `& 0xFFFF` used as a modulus, comparisons with `65535`. Signed-16 storage of negated values is
  now `-n` instead of `65536 - n`. I searched for functions returning a 16-bit type with `return -1`, for
  `= -1` on 16-bit typedefs and for all ≥0x80000000 literals, and the ftests exercise the hot paths, but this is
  not proof. More ftests (and a Windows-vs-Linux differential run of the desync checksums) are the mitigation.
* **Sizes:** `KfxSimState` is 3.2× larger (140 MB). Savegames, multiplayer resync payloads and memory grow
  accordingly (`Thing` ×12,288 alone is 106 MB). Resync compresses (see `net_resync.cpp`); saves do not.
* **Compatibility is deliberately broken:** old savegames, `fx1contn.sav`, high-score files (`HighScore` is 80 B) and
  packet-recording headers do not load; multiplayer peers must run the same build. The state chunks' `ver` is still 0 —
  bump it so old files give "saved by an incompatible build".
* Performance was not measured (wider ints double cache footprint of the hot arrays; `-O3`, no vectorisation work).
* Not verified on aarch64, and the last ftests after `editor_brush` did not run (same in the baseline).
* Behaviour differences that are intentional: RNG ranges ≤ 0 return 0; `LbRandomSeries` returns `int64_t`, so
  `RANDOM(11) - 5` is ordinary signed arithmetic (the desync class of finding F1 in doc 00 is impossible now).

## 7. How the conversion was done (reproducible)

A comment/string-aware rewriter (type spellings → `int64_t/uint64_t/double`; `%d/%u/%x` → `PRId64/PRIu64/PRIx64`
and the matching scanf macros; integer arguments of printf-style calls wrapped in `(int64_t)`/`(uint64_t)`, star
arguments in `(int)`; `L/UL/f` literal suffixes dropped; protected spellings: `lua_State` callbacks, comparators,
signal handlers, `main`), followed by the compiler as the checker (`-Werror`, printf format attributes from doc 00)
and per-boundary manual fixes, then the layout diff, the unit suites, the mingw build and the ftest sweep. The
rewriter and helper scripts lived in a scratch directory and are not committed; the branch's first commit
("mechanical rewrite") is the machine output, the following commits are the fixes.
