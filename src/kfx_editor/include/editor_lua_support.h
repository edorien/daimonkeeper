/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_lua_support.h
 *     Header file for editor_lua_support.cpp.
 * @par Purpose:
 *     docs/refactor/editor/fx-plans/02-lua-scripts.md L2 -- the pure (no
 *     ImGui, no live session) parts of Lua script editing: the API names
 *     declared by the engine's stub files, the new-script template, and
 *     `require` lookup.
 */
/******************************************************************************/
#ifndef DK_EDITOR_LUA_SUPPORT_H
#define DK_EDITOR_LUA_SUPPORT_H

#include <set>
#include <string>
#include <vector>

// Global functions and constants declared by the `.lua` stub files under
// `dir` (recursively): `function Name(`, `function Class.method(` /
// `Class:method(` (reported as `Class`), and top-level `Name =`.
std::set<std::string> editor_lua_scan_api(const std::string &dir);

// Same scan over text already in memory (a module or the level's own script).
void editor_lua_scan_api_text(const std::string &text, std::set<std::string> &out);

// The same, for the engine's own fxdata/lua folder, scanned once per call to
// editor_lua_api_names_reload() (first use loads it).
const std::set<std::string> &editor_lua_api_names();
void editor_lua_api_names_reload();

// Starting text for "Add Lua script".
std::string editor_lua_template();

// The module named by a `require "x"` / `require("x")` / `require 'x'` on
// `line` if `column` (byte offset) is inside that call; empty otherwise.
std::string editor_lua_require_at(const std::string &line, size_t column);

// Where `require "name"` would find the module: the first existing
// `<root>/<name with '.' as '/'>.lua` over the roots, in the engine's order
// (level folder, campaign lua folder, fxdata lua folder). Empty if none.
std::string editor_lua_resolve_module(const std::string &name, const std::vector<std::string> &roots);

// Where a copy of module `name` goes so `require` finds it first: the level
// folder (the engine searches it before the campaign and fxdata folders).
std::string editor_lua_copy_target(const std::string &level_dir, const std::string &name);

// Copies `src` to `dst`, creating folders; never overwrites (returns false and
// leaves `dst` alone if it exists, or on any I/O error).
bool editor_lua_copy_file(const std::string &src, const std::string &dst);

// Writes `text` to `path` through a temporary file so a failed write cannot
// truncate the original. Bytes are written as given.
bool editor_lua_write_file(const std::string &path, const std::string &text);

// True if both paths name the same file (after normalisation).
bool editor_lua_same_file(const std::string &a, const std::string &b);

// The engine's own search roots for the level in `level_dir`.
std::vector<std::string> editor_lua_search_roots(const std::string &level_dir);

#endif
