# Lua sandboxing (upstream #5389) -- evaluation, 2026-10-12

**Result: not merged.** The commit's sandbox is a first step that still leaves a trivial native-code escape, and
this fork's Lua state is wider open than upstream's was. Only the independent part of the commit was taken
(`RANDOM` in config names drawn from the synchronised seed). A proper sandbox needs its own design -- sketched at
the end -- because the fork's own bundled Lua depends on exactly the facilities a sandbox has to remove.

## Who runs Lua, and whose it is

`script_open_lua_script()` (`kfx_script/src/lua_base.c`) builds one `lua_State` per level with `luaL_openlibs()`
and then runs, in that state: the engine's `fxdata/lua/init.lua`, the campaign's `lua/init.lua`, the level's
`mapNNNNN.lua`, and every enabled mod's `init.lua` / `mapNNNNN.lua`. Everything except the first is **downloaded
content**: a map pack, a campaign or a mod is code from whoever made it, run at the player's privileges the
moment the level loads. In a network game every client runs the host's level script (mods and map files are
checked by checksum, then trusted). The console's `lua` command runs a chat line in the same state. Saved games
also carry Lua: `fxdata/lua/core/serialisation.lua` dumps Lua functions with `string.dump` and loads them back
with `load(dumped, nil, "b")`.

## What a script can do today (this tree, `luaL_openlibs`, LuaJIT)

Probe: [`lua-sandbox/probe.lua`](lua-sandbox/probe.lua), run with the LuaJIT the CMake deps build. "plain" is
this fork now; "upstream" is the same probe under #5389's `open_lua_standard_libraries()` +
`disable_lua_functions()` (harness: [`lua-sandbox/upstream_sandbox.c`](lua-sandbox/upstream_sandbox.c), their code
verbatim).

| capability | plain (this fork) | upstream #5389 |
|---|---|---|
| `ffi` (call any libc/OS function, read and write any memory) | **yes** | no (not opened) |
| `jit` library | yes | no |
| `os.execute`, `io.popen` (run any command) | **yes** | no |
| `os.getenv`, `os.rename`, `os.tmpname`, `os.exit` | yes | no |
| `io.open` -- read or write any file the user can | **yes** | **yes** (kept on purpose, "TODO - remove/replace") |
| `os.remove` -- delete any file | **yes** | **yes** (kept, same TODO) |
| `package.loadlib` | yes | no |
| `require` of a native library found through `package.cpath` (`./?.so`, `.\?.dll` -- relative to the game folder) | **yes** | **yes** |
| `dofile`, `loadfile`, `load` of arbitrary files / **bytecode** (`string.dump`, `load(.., "b")`) | **yes** | **yes** |
| `debug` library (registry, upvalues, `setmetatable` on basic types) | yes | yes |
| time/instruction/memory limits | none | none |

**The escape that survives #5389**, proven with a harmless library
([`make_poc.py`](lua-sandbox/make_poc.py)): a script writes an ELF/DLL to `./evil.so` with the allowed `io.open`,
then `require "evil"` loads it through `package.cpath`; the library's `luaopen_evil` ran inside the host process.
Also open without any native file: LuaJIT does not verify bytecode, so a crafted `load(..., "b")` chunk corrupts
memory -- the same text-only/binary distinction decides whether a *savegame* that carries dumped functions is
safe to load. And a level script can overwrite or delete any file the user owns (`io.open(path, "w")`,
`os.remove`): saves, `daimonkeeper.cfg`, other mods.

Upstream's own text calls the whitelist temporary ("TODO - remove/replace these"), and its sibling commit says
the functions that desync are "not removed yet, but will soon". Merging it would take the fork from "wide open"
to "wide open through two doors", and read as a sandbox to the next person looking.

## Why it can't just be a blacklist here

The engine's own bundled Lua needs what a sandbox removes: `serialisation.lua` uses `debug.getregistry()`,
`debug.getinfo`, `string.dump`, `load(.., "b")` (function persistence in saves); `TriggerSystem.lua` uses
`debug.getupvalue`; `external/binser.lua` uses `io.open`. So trusted core code and untrusted content must run in
different tiers, not in one state with some names removed.

## Proposed design (to plan, not done)

1. **Two tiers.** Trusted: `fxdata/lua/` (shipped with the game), loaded first with the full libraries it needs.
   Untrusted: campaign, level and mod scripts, loaded after the trusted core has captured what it needs (`debug`,
   `string.dump`, `io` for binser go into locals/upvalues of core modules), into chunks whose environment is a
   **whitelist table** built from scratch (`setfenv` / `load(..., env)`): `string` (minus `dump`), `table`, `math`
   (with the seeded `random`), `bit`, `pairs`/`ipairs`/`next`/`select`/`type`/`tostring`/`tonumber`/`pcall`/`error`/
   `assert`/`unpack`, the engine API tables, and `os.time/date/clock` only where determinism allows (not in
   multiplayer; `GetGameTimestamp` replaces them -- #5383, taken).
2. **No loaders for untrusted code:** no `io`, `os` (beyond the above), `debug`, `ffi`, `jit`, `package`, `dofile`,
   `loadfile`; `load`/`loadstring` text-only (`mode "t"`); the file loaders in `lua_base.c` use
   `luaL_loadfilex(..., "t")`. A replacement `require` resolves only module names (no path separators, no `..`)
   against the existing three script directories and loads text.
3. **Files a mod may legitimately need** (persisting data) go through an engine-provided API with a per-campaign/
   per-mod directory and size cap -- not `io`.
4. **Saves:** function persistence must stop using binary chunks from the file, or the chunk must be verified:
   the dumped functions are re-created from text (keep the source with the dump) or the load is limited to text.
   Until then a hostile *save file* is as dangerous as a hostile map.
5. **Resource limits:** an instruction-count hook (`lua_sethook` with a count) that aborts a script that runs too
   long per call and per turn, and an allocator with a ceiling (`lua_newstate` with a counting allocator). In
   multiplayer an endless loop in a host's script freezes every client.
6. **Trust of the console and the LLM bridge:** the `lua` console command stays full-power in single player and
   cheats only (done in #5401, taken: refused in network games); the bridge's tools go through the engine API,
   not Lua.
7. **Tests:** a Catch2/ftest harness that opens the untrusted tier and asserts each name above is absent or
   refused, plus the PoC scripts here as regression cases (they must fail to run).

Scope: `kfx_script/src/lua_base.c` (state construction, loaders), `config/fxdata/lua/core/serialisation.lua`,
the save format's Lua chunk, and a decision about mods that already use `io`/`os` (none ship in this tree;
`kfx_parity` lists nothing for them, but third-party mods may -- needs a compatibility note).
