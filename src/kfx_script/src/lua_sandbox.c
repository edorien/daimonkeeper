/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file lua_sandbox.c
 *     What Lua code from a map, a campaign or a mod can reach. See lua_sandbox.h.
 */
/******************************************************************************/
#include "pre_inc.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <luajit.h>

#include "lua_sandbox.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Source only: lua_load() refuses a binary chunk (and a source chunk, for "b") with an error. */
#define LUA_SANDBOX_LOAD_MODE "t"

/** Where the text of the scripts the game loaded is kept: chunk name -> text, in the Lua registry. */
#define CHUNK_TEXT_REGISTRY "kfx_chunk_text"
#define CHUNK_COUNT_REGISTRY "kfx_chunk_count"
#define CHUNK_BYTES_REGISTRY "kfx_chunk_bytes"
#define CHUNK_BLOCKS_REGISTRY "kfx_script_blocks"
/** No script is bigger than this, and all together they are kept up to this much (more is loaded but not kept,
 *  so a function in it can't be saved with the game: the author is told when the function is registered). */
#define CHUNK_FILE_MAX (16 * 1024 * 1024)
#define CHUNK_TOTAL_MAX (64 * 1024 * 1024)

static lua_Number registry_number(lua_State *L, const char *key)
{
    lua_getfield(L, LUA_REGISTRYINDEX, key);
    lua_Number n = lua_tonumber(L, -1);
    lua_pop(L, 1);
    return n;
}

static void registry_set_number(lua_State *L, const char *key, lua_Number n)
{
    lua_pushnumber(L, n);
    lua_setfield(L, LUA_REGISTRYINDEX, key);
}

/** Keeps `text` as the source of the chunk called `chunkname`. */
static void register_chunk_text(lua_State *L, const char *chunkname, const char *text, size_t len)
{
    const lua_Number bytes = registry_number(L, CHUNK_BYTES_REGISTRY);
    if (bytes + (lua_Number)len > (lua_Number)CHUNK_TOTAL_MAX)
        return;
    lua_getfield(L, LUA_REGISTRYINDEX, CHUNK_TEXT_REGISTRY);
    if (!lua_istable(L, -1))
    {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, CHUNK_TEXT_REGISTRY);
    }
    lua_getfield(L, -1, chunkname);
    const int replacing = lua_isstring(L, -1);
    const size_t old_len = replacing ? lua_objlen(L, -1) : 0;
    lua_pop(L, 1);
    lua_pushlstring(L, text, len);
    lua_setfield(L, -2, chunkname);
    lua_pop(L, 1); // the table
    registry_set_number(L, CHUNK_BYTES_REGISTRY, bytes + (lua_Number)len - (lua_Number)old_len);
    if (!replacing)
        registry_set_number(L, CHUNK_COUNT_REGISTRY, registry_number(L, CHUNK_COUNT_REGISTRY) + 1);
}

/** chunk_text(name): the kept source of a loaded script, or nil. */
static int cf_chunk_text(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    lua_getfield(L, LUA_REGISTRYINDEX, CHUNK_TEXT_REGISTRY);
    if (!lua_istable(L, -1))
    {
        lua_pushnil(L);
        return 1;
    }
    lua_getfield(L, -1, name);
    return 1;
}

/** chunk_names(): the names of the loaded scripts whose source is kept, as an array. */
static int cf_chunk_names(lua_State *L)
{
    lua_newtable(L);
    lua_getfield(L, LUA_REGISTRYINDEX, CHUNK_TEXT_REGISTRY);
    if (lua_istable(L, -1))
    {
        int n = 0;
        lua_pushnil(L);
        while (lua_next(L, -2) != 0)
        {
            lua_pop(L, 1); // the text
            lua_pushvalue(L, -1);
            lua_rawseti(L, -4, ++n);
        }
    }
    lua_pop(L, 1);
    return 1;
}

/**
 * Loads a script file as source and keeps its text. The whole file is read first, so what is compiled and what is
 * kept are the same bytes. A UTF-8 byte order mark is skipped and a first line starting with '#' is blanked, as
 * luaL_loadfile does (the line count stays right).
 * @return the lua_load status; LUA_ERRFILE with a message on the stack if the file can't be read.
 */
static int load_source_file(lua_State *L, const char *filename, const char *chunkname)
{
    FILE *f = fopen(filename, "rb");
    if (f == NULL)
    {
        lua_pushfstring(L, "cannot open %s", filename);
        return LUA_ERRFILE;
    }
    if (fseek(f, 0, SEEK_END) != 0)
    {
        fclose(f);
        lua_pushfstring(L, "cannot read %s", filename);
        return LUA_ERRFILE;
    }
    const long size = ftell(f);
    if ((size < 0) || (size > CHUNK_FILE_MAX) || (fseek(f, 0, SEEK_SET) != 0))
    {
        fclose(f);
        lua_pushfstring(L, "cannot read %s (too large)", filename);
        return LUA_ERRFILE;
    }
    char *buf = (char *)malloc((size_t)size + 1);
    if (buf == NULL)
    {
        fclose(f);
        lua_pushfstring(L, "not enough memory to read %s", filename);
        return LUA_ERRFILE;
    }
    const size_t got = fread(buf, 1, (size_t)size, f);
    fclose(f);
    char *text = buf;
    size_t len = got;
    if ((len >= 3) && ((unsigned char)text[0] == 0xEF) && ((unsigned char)text[1] == 0xBB) && ((unsigned char)text[2] == 0xBF))
    {
        text += 3;
        len -= 3;
    }
    if ((len > 0) && (text[0] == '#'))
    {
        for (size_t i = 0; (i < len) && (text[i] != '\n'); i++)
            text[i] = ' ';
    }
    const int status = luaL_loadbufferx(L, text, len, chunkname, LUA_SANDBOX_LOAD_MODE);
    if (status == 0)
        register_chunk_text(L, chunkname, text, len);
    free(buf);
    return status;
}

int lua_sandbox_dofile(lua_State *L, const char *filename)
{
    char chunkname[4200];
    if (strlen(filename) + 2 > sizeof(chunkname))
    {
        lua_pushfstring(L, "file name too long");
        return LUA_ERRFILE;
    }
    snprintf(chunkname, sizeof(chunkname), "@%s", filename);
    int status = load_source_file(L, filename, chunkname);
    if (status != 0)
        return status;
    return lua_pcall(L, 0, LUA_MULTRET, 0);
}

int lua_sandbox_dostring(lua_State *L, const char *code)
{
    int status = luaL_loadbufferx(L, code, strlen(code), code, LUA_SANDBOX_LOAD_MODE);
    if (status != 0)
        return status;
    return lua_pcall(L, 0, LUA_MULTRET, 0);
}

int lua_sandbox_dostring_script(lua_State *L, const char *code)
{
    const lua_Number n = registry_number(L, CHUNK_BLOCKS_REGISTRY) + 1;
    registry_set_number(L, CHUNK_BLOCKS_REGISTRY, n);
    char chunkname[64];
    snprintf(chunkname, sizeof(chunkname), "=script-block:%d", (int)n);
    const size_t len = strlen(code);
    int status = luaL_loadbufferx(L, code, len, chunkname, LUA_SANDBOX_LOAD_MODE);
    if (status != 0)
        return status;
    register_chunk_text(L, chunkname, code, len);
    return lua_pcall(L, 0, LUA_MULTRET, 0);
}

int lua_sandbox_module_name_ok(const char *name)
{
    if ((name == NULL) || (name[0] == '\0'))
        return 0;
    for (const char *p = name; *p != '\0'; p++)
    {
        const char c = *p;
        if (c == '.')
        {
            // no leading, trailing or doubled separator: that is how a ".." would be spelled
            if ((p == name) || (p[1] == '\0') || (p[1] == '.'))
                return 0;
            continue;
        }
        if (((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')) || ((c >= '0') && (c <= '9')) || (c == '_') || (c == '-'))
            continue;
        return 0;
    }
    return 1;
}

/**
 * The loader `require` uses for Lua modules: package.path (upvalue 1 is the package table) searched for
 * the module name, the file loaded as source only. Replaces the standard one, which also loads bytecode
 * and accepts any name.
 */
static int sandbox_searcher_lua(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    if (!lua_sandbox_module_name_ok(name))
    {
        lua_pushfstring(L, "\n\tmodule name '%s' not allowed (letters, digits, '_', '-' and '.' only)", name);
        return 1;
    }
    lua_getfield(L, lua_upvalueindex(1), "path");
    const char *path = lua_tostring(L, -1);
    if (path == NULL)
        return luaL_error(L, "'package.path' must be a string");

    char mod[256];
    const size_t name_len = strlen(name);
    if (name_len >= sizeof(mod))
    {
        lua_pushfstring(L, "\n\tmodule name too long");
        return 1;
    }
    memcpy(mod, name, name_len + 1);
    for (char *p = mod; *p != '\0'; p++)
    {
        if (*p == '.')
            *p = '/';
    }

    luaL_Buffer tried;
    luaL_buffinit(L, &tried);
    const char *cur = path;
    while (*cur != '\0')
    {
        const char *end = strchr(cur, ';');
        size_t len = (end != NULL) ? (size_t)(end - cur) : strlen(cur);
        if (len > 0)
        {
            char file[4096];
            size_t out = 0;
            for (size_t i = 0; (i < len) && (out + sizeof(mod) < sizeof(file)); i++)
            {
                if (cur[i] == '?')
                {
                    memcpy(file + out, mod, strlen(mod));
                    out += strlen(mod);
                }
                else
                {
                    file[out++] = cur[i];
                }
            }
            file[out] = '\0';
            char chunkname[4200 + 2];
            snprintf(chunkname, sizeof(chunkname), "@%s", file);
            int status = load_source_file(L, file, chunkname);
            if (status == 0)
                return 1; // the module's main chunk; require() calls it
            if (status != LUA_ERRFILE)
            {
                // found, but not loadable as source (a syntax error, or bytecode)
                return luaL_error(L, "error loading module '%s' from file '%s':\n\t%s", name, file, lua_tostring(L, -1));
            }
            lua_pop(L, 1); // the "cannot open" message
            lua_pushfstring(L, "\n\tno file '%s'", file);
            luaL_addvalue(&tried);
        }
        if (end == NULL)
            break;
        cur = end + 1;
    }
    luaL_pushresult(&tried);
    return 1;
}

void lua_sandbox_install_loaders(lua_State *L)
{
    // For the game's own scripts only (core/function_source.lua keeps them in locals); removed again by the lock-down.
    lua_pushcfunction(L, cf_chunk_text);
    lua_setglobal(L, "__kfx_chunk_text");
    lua_pushcfunction(L, cf_chunk_names);
    lua_setglobal(L, "__kfx_chunk_names");

    lua_getglobal(L, "package");
    if (!lua_istable(L, -1))
    {
        lua_pop(L, 1);
        return;
    }
    lua_getfield(L, -1, "loaders");
    if (lua_istable(L, -1))
    {
        lua_pushvalue(L, -2); // the package table, as the searcher's upvalue
        lua_pushcclosure(L, sandbox_searcher_lua, 1);
        lua_rawseti(L, -2, 2);
        // [1] is package.preload, [2] the Lua file loader; [3] (C modules) and [4] (all-in-one) go
        lua_pushnil(L);
        lua_rawseti(L, -2, 3);
        lua_pushnil(L);
        lua_rawseti(L, -2, 4);
    }
    lua_pop(L, 2); // loaders, package
}

/** The os functions untrusted scripts may keep: clocks and dates only. */
static const char *const kept_os_functions[] = {"time", "date", "clock", "difftime", NULL};

/** package.preload entries that may stay (table.new is not one: it allocates what it is told to, in one call). */
static const char *const kept_preload[] = {"table.clear", NULL};

/** The standard libraries gone altogether, as globals and as loaded modules. */
static const char *const removed_libraries[] = {"io", "debug", "ffi", "jit", NULL};

static int in_list(const char *name, const char *const *list)
{
    for (; *list != NULL; list++)
    {
        if (strcmp(name, *list) == 0)
            return 1;
    }
    return 0;
}

/** Deletes every string-keyed field of the table on top of the stack that is not in `keep` (NULL: none). */
static void keep_only(lua_State *L, const char *const *keep)
{
    lua_pushnil(L);
    while (lua_next(L, -2) != 0)
    {
        lua_pop(L, 1); // the value; the key stays for lua_next
        if ((lua_type(L, -1) == LUA_TSTRING) && (keep != NULL) && in_list(lua_tostring(L, -1), keep))
            continue;
        // Set t[key] = nil without disturbing the traversal: the key is copied first.
        lua_pushvalue(L, -1);
        lua_pushnil(L);
        lua_rawset(L, -4);
    }
}

/** t[name] = nil, for the table on top of the stack. */
static void remove_field(lua_State *L, const char *name)
{
    lua_pushnil(L);
    lua_setfield(L, -2, name);
}

/**
 * load() and loadstring() as the untrusted tier sees them: same arguments, but the mode is forced to
 * source only, whatever the caller passes. Upvalue 1 is the original load().
 */
static int sandbox_load(lua_State *L)
{
    luaL_checkany(L, 1);
    int nargs = lua_gettop(L);
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_pushvalue(L, 1);                       // chunk (a string or a reader function)
    if (nargs >= 2)
        lua_pushvalue(L, 2);                   // chunk name
    else
        lua_pushnil(L);
    lua_pushstring(L, LUA_SANDBOX_LOAD_MODE);  // mode
    int pushed = 3;
    if (nargs >= 4)
    {
        lua_pushvalue(L, 4);                   // environment
        pushed = 4;
    }
    lua_call(L, pushed, LUA_MULTRET);
    return lua_gettop(L) - nargs;
}

void lua_sandbox_lock_down(lua_State *L)
{
    const int top = lua_gettop(L);

    // Base library: wrap the loaders, drop the file loaders.
    lua_getglobal(L, "load");
    if (lua_isfunction(L, -1))
    {
        lua_pushvalue(L, -1);
        lua_pushcclosure(L, sandbox_load, 1);
        lua_setglobal(L, "load");
        lua_pushcclosure(L, sandbox_load, 1); // consumes the original, now also behind loadstring
        lua_setglobal(L, "loadstring");
    }
    else
    {
        lua_pop(L, 1);
    }
    lua_pushnil(L);
    lua_setglobal(L, "dofile");
    lua_pushnil(L);
    lua_setglobal(L, "loadfile");
    lua_pushnil(L);
    lua_setglobal(L, "__kfx_chunk_text");
    lua_pushnil(L);
    lua_setglobal(L, "__kfx_chunk_names");

    // Whole libraries.
    for (const char *const *lib = removed_libraries; *lib != NULL; lib++)
    {
        lua_pushnil(L);
        lua_setglobal(L, *lib);
    }

    // os: clocks only.
    lua_getglobal(L, "os");
    if (lua_istable(L, -1))
        keep_only(L, kept_os_functions);
    lua_pop(L, 1);

    // string.dump would hand out bytecode.
    lua_getglobal(L, "string");
    if (lua_istable(L, -1))
        remove_field(L, "dump");
    lua_pop(L, 1);

    // package: no native libraries, no probing of the file system, and nothing but plain Lua modules to load.
    lua_getglobal(L, "package");
    if (lua_istable(L, -1))
    {
        remove_field(L, "loadlib");
        remove_field(L, "searchpath");
        remove_field(L, "searchers");
        lua_pushstring(L, "");
        lua_setfield(L, -2, "cpath");

        lua_getfield(L, -1, "preload");
        if (lua_istable(L, -1))
            keep_only(L, kept_preload);
        lua_pop(L, 1);

        lua_getfield(L, -1, "loaded");
        if (lua_istable(L, -1))
        {
            for (const char *const *lib = removed_libraries; *lib != NULL; lib++)
                remove_field(L, *lib);
            // modules a require "jit.util" and the like would have left behind
            lua_pushnil(L);
            while (lua_next(L, -2) != 0)
            {
                lua_pop(L, 1);
                if ((lua_type(L, -1) == LUA_TSTRING) && (strncmp(lua_tostring(L, -1), "jit.", 4) == 0))
                {
                    lua_pushvalue(L, -1);
                    lua_pushnil(L);
                    lua_rawset(L, -4);
                }
            }
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);

    lua_settop(L, top);
}

int lua_sandbox_seal_core(lua_State *L)
{
    lua_getglobal(L, "require");
    lua_pushstring(L, "core.serialisation");
    int status = lua_pcall(L, 1, 1, 0);
    if (status != 0)
        return status;
    if (!lua_istable(L, -1)) // an old or damaged core.serialisation: report it, don't raise outside a protected call
    {
        lua_pop(L, 1);
        lua_pushstring(L, "core.serialisation did not load as a module");
        return LUA_ERRRUN;
    }
    lua_getfield(L, -1, "seal");
    if (!lua_isfunction(L, -1))
    {
        lua_pop(L, 2);
        lua_pushstring(L, "core.serialisation has no seal()");
        return LUA_ERRRUN;
    }
    status = lua_pcall(L, 0, 0, 0);
    if (status != 0)
        return status;
    lua_pop(L, 1); // the module
    return 0;
}

/*********************************************************************************************************************
 * Resource limits (security pass S04)
 *********************************************************************************************************************/

static int64_t budget_limit = LUA_SANDBOX_TURN_BUDGET;
static int64_t budget_used = 0;
static int64_t memory_limit_kib = LUA_SANDBOX_MEMORY_KIB;
static int stopped = 0;

int lua_sandbox_was_stopped(void)
{
    return stopped;
}

void lua_sandbox_set_budget(int64_t instruction_budget)
{
    budget_limit = instruction_budget;
}

void lua_sandbox_new_turn(lua_State *L)
{
    (void)L;
    budget_used = 0;
    stopped = 0;
    budget_limit = LUA_SANDBOX_TURN_BUDGET;
}

static void stop_script(lua_State *L, const char *why)
{
    stopped = 1;
    luaL_error(L, "%s", why);
}

static void budget_hook(lua_State *L, lua_Debug *ar)
{
    (void)ar;
    budget_used += LUA_SANDBOX_HOOK_COUNT;
    if (budget_used > budget_limit)
        stop_script(L, "script stopped: it ran more instructions than a game turn allows");
    if (lua_gc(L, LUA_GCCOUNT, 0) > memory_limit_kib)
    {
        lua_gc(L, LUA_GCCOLLECT, 0);
        if (lua_gc(L, LUA_GCCOUNT, 0) > memory_limit_kib)
            stop_script(L, "script stopped: it uses too much memory");
    }
}

/** Charges work done by one C call to the budget (the hook only sees VM instructions). */
static void charge(lua_State *L, double work)
{
    budget_used += (int64_t)work;
    if (budget_used > budget_limit)
        stop_script(L, "script stopped: it ran more instructions than a game turn allows");
}

/** collectgarbage: "count" works; the rest (a forced full collection in a loop is time spent where no hook sees it)
 *  does nothing and says 0. */
static int sandbox_collectgarbage(lua_State *L)
{
    const char *opt = luaL_optstring(L, 1, "collect");
    if (strcmp(opt, "count") == 0)
    {
        lua_pushnumber(L, (lua_Number)lua_gc(L, LUA_GCCOUNT, 0) + (lua_Number)lua_gc(L, LUA_GCCOUNTB, 0) / 1024.0);
        return 1;
    }
    lua_pushinteger(L, 0);
    return 1;
}

#define STRING_REP_MAX (64 * 1024 * 1024)

/** string.rep with a ceiling on the result: one call must not take gigabytes. Upvalue 1 is the original. */
static int sandbox_string_rep(lua_State *L)
{
    size_t len = 0;
    luaL_checklstring(L, 1, &len);
    const lua_Number n = luaL_checknumber(L, 2);
    const lua_Number sep_len = lua_isnoneornil(L, 3) ? 0 : (lua_Number)lua_objlen(L, 3);
    if ((n > 0) && (((lua_Number)len + sep_len) * n > (lua_Number)STRING_REP_MAX))
        return luaL_error(L, "string.rep: the result would be larger than %d bytes", STRING_REP_MAX);
    charge(L, ((lua_Number)len + sep_len) * (n > 0 ? n : 0) / 64.0);
    const int nargs = lua_gettop(L);
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 1);
    lua_call(L, nargs, 1);
    return 1;
}

/**
 * How many quantifiers (* + - ?) a Lua pattern has, outside character sets and escapes: what makes matching cost
 * about (subject length) ^ (quantifiers) when it fails.
 */
static int pattern_quantifiers(const char *p, size_t n)
{
    int k = 0;
    size_t i = 0;
    if ((n > 0) && (p[0] == '^'))
        i = 1;
    while (i < n)
    {
        const char c = p[i];
        if (c == '%')
        {
            i += ((i + 1 < n) && (p[i + 1] == 'b')) ? 4 : 2;
            continue;
        }
        if (c == '[')
        {
            i++;
            if ((i < n) && (p[i] == '^'))
                i++;
            if ((i < n) && (p[i] == ']'))
                i++;
            while ((i < n) && (p[i] != ']'))
            {
                if (p[i] == '%')
                    i++;
                i++;
            }
            i++;
            continue;
        }
        if ((c == '*') || (c == '+') || (c == '-') || (c == '?'))
            k++;
        i++;
    }
    return k;
}

/** Matching may cost up to this many steps in the worst case (subject length ^ quantifiers) before it is refused. */
#define PATTERN_WORST_CASE 5e8

/** string.find/match/gmatch/gsub: a pattern with many quantifiers on a long subject can take minutes in one C call,
 *  which no hook interrupts; refuse that combination. Upvalue 1 is the original, upvalue 2 the index of the
 *  "plain" argument or 0. */
static int sandbox_pattern_function(lua_State *L)
{
    size_t slen = 0;
    size_t plen = 0;
    luaL_checklstring(L, 1, &slen);
    const char *pat = luaL_checklstring(L, 2, &plen);
    const int plain_arg = (int)lua_tointeger(L, lua_upvalueindex(2));
    const int plain = (plain_arg > 0) && lua_toboolean(L, plain_arg);
    if (!plain)
    {
        const int k = pattern_quantifiers(pat, plen);
        if ((k >= 2) && (pow((double)slen + 1.0, (double)k) > PATTERN_WORST_CASE))
            return luaL_error(L, "pattern too expensive for a subject of %d characters (%d quantifiers)", (int)slen, k);
        charge(L, (double)slen / 16.0);
    }
    const int nargs = lua_gettop(L);
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 1);
    lua_call(L, nargs, LUA_MULTRET);
    return lua_gettop(L);
}

/** table.insert / table.remove with a position shift the elements after it; that is work the hook can't see. */
static int sandbox_table_shift(lua_State *L)
{
    if (lua_istable(L, 1))
        charge(L, (double)lua_objlen(L, 1) / 4.0);
    const int nargs = lua_gettop(L);
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 1);
    lua_call(L, nargs, LUA_MULTRET);
    return lua_gettop(L);
}

static int cf_stopped(lua_State *L)
{
    lua_pushboolean(L, stopped);
    return 1;
}

/** Replaces `lib.name` with a closure over the original (and `extra`, if given as an integer >= 0). */
static void wrap_field(lua_State *L, const char *lib, const char *name, lua_CFunction wrapper, int extra)
{
    lua_getglobal(L, lib);
    if (!lua_istable(L, -1))
    {
        lua_pop(L, 1);
        return;
    }
    lua_getfield(L, -1, name);
    if (lua_isfunction(L, -1))
    {
        int nup = 1;
        if (extra >= 0)
        {
            lua_pushinteger(L, extra);
            nup = 2;
        }
        lua_pushcclosure(L, wrapper, nup);
        lua_setfield(L, -2, name);
    }
    else
    {
        lua_pop(L, 1);
    }
    lua_pop(L, 1); // lib
}

/** pcall, xpcall and the coroutine functions, written in Lua so that a script can still yield across a pcall: once a
 *  script was stopped, the error comes out of them again instead of being returned to the script. */
static const char *const catch_wrappers =
    "local pcall_o, xpcall_o, create, resume_o, stopped, error, select = ...\n"
    "local function pass(ok, ...)\n"
    "  if (not ok) and stopped() then error((...), 0) end\n"
    "  return ok, ...\n"
    "end\n"
    "local function unwrap(ok, ...)\n"
    "  if not ok then error((...), 0) end\n"
    "  return ...\n"
    "end\n"
    "pcall = function(...) return pass(pcall_o(...)) end\n"
    "xpcall = function(f, handler, ...) return pass(xpcall_o(f, handler, ...)) end\n"
    "coroutine.resume = function(co, ...) return pass(resume_o(co, ...)) end\n"
    "coroutine.wrap = function(f)\n"
    "  local co = create(f)\n"
    "  return function(...) return unwrap(resume_o(co, ...)) end\n"
    "end\n";

void lua_sandbox_limit_resources(lua_State *L, int64_t instruction_budget, int64_t memory_kib)
{
    const int top = lua_gettop(L);
    budget_limit = instruction_budget;
    memory_limit_kib = memory_kib;
    budget_used = 0;
    stopped = 0;

    lua_pushcfunction(L, sandbox_collectgarbage);
    lua_setglobal(L, "collectgarbage");
    wrap_field(L, "string", "rep", sandbox_string_rep, -1);
    wrap_field(L, "string", "find", sandbox_pattern_function, 4);
    wrap_field(L, "string", "match", sandbox_pattern_function, 0);
    wrap_field(L, "string", "gmatch", sandbox_pattern_function, 0);
    wrap_field(L, "string", "gsub", sandbox_pattern_function, 0);
    wrap_field(L, "table", "insert", sandbox_table_shift, -1);
    wrap_field(L, "table", "remove", sandbox_table_shift, -1);

    if (luaL_loadbuffer(L, catch_wrappers, strlen(catch_wrappers), "=sandbox-catch") == 0)
    {
        lua_getglobal(L, "pcall");
        lua_getglobal(L, "xpcall");
        lua_getglobal(L, "coroutine");
        lua_getfield(L, -1, "create");
        lua_getfield(L, -2, "resume");
        lua_remove(L, -3); // the coroutine table
        lua_pushcfunction(L, cf_stopped);
        lua_getglobal(L, "error");
        lua_getglobal(L, "select");
        lua_pcall(L, 7, 0, 0);
    }
    lua_settop(L, top);

    // The count hook only runs in the interpreter: a loop that LuaJIT compiled to machine code would pass it by. So
    // the compiler is off for this state (scripts here are small; the same bytecode also counts the same everywhere).
    luaJIT_setmode(L, 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_OFF);
    lua_sethook(L, budget_hook, LUA_MASKCOUNT, LUA_SANDBOX_HOOK_COUNT);
}

#ifdef __cplusplus
}
#endif
