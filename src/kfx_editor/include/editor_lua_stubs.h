/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_lua_stubs.h
 *     Header file for editor_lua_stubs.cpp.
 * @par Purpose:
 *     docs/refactor/editor/fx-plans/02-lua-scripts.md L4 -- the engine's Lua
 *     API as documented by its EmmyLua stub files (`---@param`, `---@return`,
 *     doc comments), for the Commands window's Lua page.
 */
/******************************************************************************/
#ifndef DK_EDITOR_LUA_STUBS_H
#define DK_EDITOR_LUA_STUBS_H

#include <string>
#include <vector>

struct LuaParamDoc
{
    std::string name;
    std::string type;
    std::string description;
    bool optional = false;
};

struct LuaFunctionDoc
{
    std::string name;
    std::string group;   // source file, e.g. "players"
    std::string doc;     // doc comment lines joined by a space
    std::vector<LuaParamDoc> params;
    std::string returns; // "type description" of the first @return, or empty
};

// Functions declared in `text` (one stub file); `group` is stored on each.
std::vector<LuaFunctionDoc> editor_lua_parse_stub_text(const std::string &text, const std::string &group);

// Global functions from the `bindings` and `triggers` folders under `dir`
// (the engine's fxdata/lua), sorted by group then name. Class methods
// (`Class:method`) and underscore-prefixed names are left out.
std::vector<LuaFunctionDoc> editor_lua_load_stubs(const std::string &dir);
// The same for the engine's own fxdata/lua folder, loaded once.
const std::vector<LuaFunctionDoc> &editor_lua_stub_catalog();

// "Name(player, creature, level?)".
std::string editor_lua_signature(const LuaFunctionDoc &f);
// What Insert puts in the script: "Name(player, creature)" -- optional
// parameters are left out.
std::string editor_lua_call_template(const LuaFunctionDoc &f);

// L6 snippets: the trigger registrations (`RegisterXxxEvent(action, ...)`)
// among `catalog`, and the block for one: a handler skeleton plus the
// registration line naming it. Built from the stub signatures so they stay
// correct when the API changes.
std::vector<const LuaFunctionDoc *> editor_lua_event_functions(const std::vector<LuaFunctionDoc> &catalog);
std::string editor_lua_event_snippet(const LuaFunctionDoc &f);

#endif
