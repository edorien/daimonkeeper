/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file lua_sandbox.h
 *     What Lua code from a map, a campaign or a mod can reach (security pass S01).
 * @par Purpose:
 *     The game runs two kinds of Lua in one state: its own scripts (fxdata/lua/, shipped with the game, the
 *     trusted tier) and downloaded ones (campaign, level and mod scripts, the untrusted tier). The trusted
 *     tier is loaded first with the full standard libraries. lua_sandbox_lock_down() then takes away from
 *     the state everything that reaches outside the game (files, processes, native code, the debug library,
 *     bytecode loading) before the first untrusted chunk runs.
 * @par Comment:
 *     This is an interim lockdown, not a complete sandbox: untrusted code still shares one set of globals
 *     with the trusted tier and has no time or memory limit (security pass S04). See
 *     docs/security-pass/README.md.
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef LUA_SANDBOX_H
#define LUA_SANDBOX_H

#include <lua.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Replaces the module loaders of `require` with one that loads Lua *source* only (no bytecode, no native
 * libraries) and accepts only plain module names. Call after luaL_openlibs() and before any script runs.
 */
void lua_sandbox_install_loaders(lua_State *L);

/**
 * Removes from the state what untrusted scripts must not reach: io, debug, ffi, jit, dofile, loadfile,
 * string.dump, package.loadlib/cpath/searchpath, the native module loaders, the dangerous os functions;
 * makes load()/loadstring() accept source only. Call once, after the trusted scripts (fxdata/lua/init.lua)
 * have run -- they capture what they need (such as the debug library) in local variables first.
 */
void lua_sandbox_lock_down(lua_State *L);

/** luaL_dofile() for source files only: a file holding bytecode is refused. Same return value and stack effect.
 *  The text of every script file loaded this way (and by `require`) is kept, by chunk name, so that a function
 *  defined in it can be found again when a saved game is loaded (see config/fxdata/lua/core/function_source.lua). */
int lua_sandbox_dofile(lua_State *L, const char *filename);

/** luaL_dostring() for a block of script text from a level script (LUA ... ENDLUA): source only, and the text is
 *  kept like a file's, under a chunk name of its own ("=script-block:N"). */
int lua_sandbox_dostring_script(lua_State *L, const char *code);

/** luaL_dostring() for source only: a string holding bytecode is refused. Same return value and stack effect. */
int lua_sandbox_dostring(lua_State *L, const char *code);

/**
 * After lua_sandbox_lock_down(): lets the game's serialisation script fix, once, which built-in functions a saved
 * game may refer to (core/serialisation.lua seal()). Same return value as lua_pcall, error message on the stack.
 */
int lua_sandbox_seal_core(lua_State *L);

/** Per game turn, scripts may run this many VM instructions (counted by a hook every LUA_SANDBOX_HOOK_COUNT
 *  instructions, so deterministic: every client of a multiplayer game stops a runaway script at the same point). */
#define LUA_SANDBOX_TURN_BUDGET 200000000
/** While the scripts of a level load (top-level code of the level, campaign and mods). */
#define LUA_SANDBOX_LOAD_BUDGET 1000000000
/** The hook's granularity. */
#define LUA_SANDBOX_HOOK_COUNT 1000000
/** All Lua values together may take this much (KiB) before the script that is running is stopped. */
#define LUA_SANDBOX_MEMORY_KIB (768 * 1024)

/**
 * Limits what a script can use up (security pass S04): installs the instruction hook, and the replacements for the
 * few library functions that do unbounded work in one call (string.rep, pattern matching on long subjects with many
 * quantifiers, collectgarbage, table.insert/remove shifting) and for pcall, xpcall and the coroutine functions, which
 * pass a stop on instead of letting a script catch it and carry on. Call after lua_sandbox_lock_down().
 * Over budget or over the memory limit, the running script gets an error that no pcall in a script can swallow; it
 * propagates to the engine's own protected call, which logs it.
 */
void lua_sandbox_limit_resources(lua_State *L, int64_t instruction_budget, int64_t memory_kib);

/** A new game turn: the instruction budget starts again and a script that was stopped may run. */
void lua_sandbox_new_turn(lua_State *L);

/** Sets how many instructions may run before the next lua_sandbox_new_turn() (the load phase has more). */
void lua_sandbox_set_budget(int64_t instruction_budget);

/** Whether a script was stopped since the last new turn. */
int lua_sandbox_was_stopped(void);

/** Whether `name` is acceptable to `require`: letters, digits, '_', '-' and single '.' separators only. */
int lua_sandbox_module_name_ok(const char *name);

#ifdef __cplusplus
}
#endif

#endif
