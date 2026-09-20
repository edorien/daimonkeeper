/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_lua_validate.h
 *     Header file for editor_lua_validate.cpp.
 * @par Purpose:
 *     docs/refactor/editor/fx-plans/02-lua-scripts.md L3 -- checks a level's
 *     Lua script without running it: syntax (LuaJIT parse in a throwaway
 *     state), calls to functions nothing declares (warning), and the classic
 *     script commands passed to RunDKScriptCommand("...").
 */
/******************************************************************************/
#ifndef DK_EDITOR_LUA_VALIDATE_H
#define DK_EDITOR_LUA_VALIDATE_H

#include "editor_script_validate.h" // ScriptIssue, ScriptCommandLookup

#include <functional>
#include <set>
#include <string>
#include <vector>

// Returns the text of the module a `require "name"` resolves to, or empty.
typedef std::function<std::string(const std::string &)> LuaModuleText;

// `api` is the set of names declared by the engine's stub files
// (editor_lua_api_names()). Issues are sorted by line (0-based).
std::vector<ScriptIssue> editor_lua_validate(const std::string &text, const std::set<std::string> &api,
    const ScriptCommandLookup &command_lookup, const LuaModuleText &module_text);

// The same against the engine's own tables, and required modules found
// through the engine's search order for the level in `level_dir`.
std::vector<ScriptIssue> editor_lua_validate_engine(const std::string &text, const std::string &level_dir);

#endif
