#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <string.h>
#include <stdio.h>
/* verbatim from upstream 38ce66bbd (lua_base.c) */
static void lua_blacklist(lua_State *L, const char *libname, const char *name)
{
    lua_getglobal(L, libname);
    if (!lua_istable(L, -1)) { lua_pop(L, 1); return; }
    lua_pushnil(L); lua_setfield(L, -2, name); lua_pop(L, 1);
}
static void lua_whitelist(lua_State *L, const char *libname, const char *keep)
{
    lua_getglobal(L, libname);
    if (!lua_istable(L, -1)) { lua_pop(L, 1); return; }
    int t = lua_gettop(L);
    lua_pushnil(L);
    while (lua_next(L, t) != 0) {
        if (lua_type(L, -2) == LUA_TSTRING) {
            const char *key = lua_tostring(L, -2);
            if (strcmp(key, keep) != 0) { lua_pushnil(L); lua_setfield(L, t, key); }
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
}
static void disable_lua_functions(lua_State *L)
{
    lua_whitelist(L, "io", "open");
    lua_whitelist(L, "os", "remove");
    lua_blacklist(L, "package", "loadlib");
}
static void open_lua_standard_libraries(lua_State *L)
{
    static const luaL_Reg stdlib[] = {
        {"", luaopen_base}, {LUA_TABLIBNAME, luaopen_table}, {LUA_IOLIBNAME, luaopen_io}, {LUA_OSLIBNAME, luaopen_os},
        {LUA_STRLIBNAME, luaopen_string}, {LUA_MATHLIBNAME, luaopen_math}, {LUA_DBLIBNAME, luaopen_debug},
        {LUA_LOADLIBNAME, luaopen_package}, {LUA_BITLIBNAME, luaopen_bit}, {NULL, NULL}};
    for (const luaL_Reg *lib = stdlib; lib->func != NULL; lib++) {
        lua_pushcfunction(L, lib->func); lua_pushstring(L, lib->name); lua_call(L, 1, 0);
    }
}
int main(int argc, char **argv)
{
    lua_State *L = luaL_newstate();
    open_lua_standard_libraries(L);
    disable_lua_functions(L);
    if (luaL_dofile(L, argv[1])) { fprintf(stderr, "%s\n", lua_tostring(L, -1)); return 1; }
    return 0;
}
