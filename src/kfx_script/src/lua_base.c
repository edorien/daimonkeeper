#include "pre_inc.h"

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "lua_api.h"
#include "lua_sandbox.h"

#include "bflib_basics.h"
#include "bflib_fileio.h"
#include "config_keeperfx.h"
#include "game_legacy.h"
#include "globals.h"
#include "gui_msgs.h"

#include "config_creature.h"
#include "config_terrain.h"
#include "config_magic.h"
#include "config_effects.h"
#include "config_objects.h"
#include "config_trapdoor.h"
#include "config_crtrstates.h"

#include "compat_report.h"
#include "state_versions.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

struct lua_State *Lvl_script = NULL;


static int lua_disabled_os_function(lua_State *L)
{
    const char *func_name = lua_tostring(L, lua_upvalueindex(1));
    WARNLOG("os.%s is disabled because it is not synchronized in multiplayer; use GetGameTimestamp", func_name);
    return luaL_error(L, "os.%s is disabled because it is not synchronized in multiplayer; use GetGameTimestamp", func_name);
}


static void disable_lua_functions(lua_State *L)
{
    if (kfx_sim_state.game_kind != GKind_MultiGame) {
        return;
    }
    const char *disabled_os_functions[] = {"time", "date", "clock"};

    lua_getglobal(L, "os");
    if (lua_istable(L, -1)) {
        for (int64_t i = 0; i < sizeof(disabled_os_functions) / sizeof(disabled_os_functions[0]); i++) {
            lua_pushstring(L, disabled_os_functions[i]);
            lua_pushcclosure(L, lua_disabled_os_function, 1);
            lua_setfield(L, -2, disabled_os_functions[i]);
        }
    }
    lua_pop(L, 1);
}


/**
 * A Lua error that's a call to a function that doesn't exist -- most likely a
 * KeeperFX API function this build doesn't have -- goes into the compat report
 * (compat_report.h) and a COMPAT: log line. Understands LuaJIT's wording
 * ("attempt to call global 'X' (a nil value)") and Lua 5.4's ("attempt to call
 * a nil value (global 'X')"); "global" may also be "method" or "field".
 * @return true if it was one.
 */
TbBool lua_report_missing_function(const char *message)
{
    if (message == NULL)
        return false;
    const char *call = strstr(message, "attempt to call ");
    if (call == NULL)
        return false;
    static const char *const scopes[] = {"global '", "method '", "field '"};
    const char *name = NULL;
    for (size_t i = 0; (i < sizeof(scopes) / sizeof(scopes[0])) && (name == NULL); i++)
    {
        const char *at = strstr(call, scopes[i]);
        if (at != NULL)
            name = at + strlen(scopes[i]);
    }
    if ((name == NULL) || (strstr(call, "nil value") == NULL))
        return false;
    char what[COMPAT_WHAT_LEN];
    size_t n = 0;
    while ((name[n] != '\0') && (name[n] != '\'') && (n + 1 < sizeof(what)))
    {
        what[n] = name[n];
        n++;
    }
    what[n] = '\0';
    // "<chunk>:<line>: attempt ...": the chunk (file) and line it happened on.
    char where[COMPAT_WHERE_LEN] = "";
    uint64_t line = 0;
    const char *colon = NULL;
    for (const char *p = message; p < call; p++)
        if ((p[0] == ':') && (p[1] >= '0') && (p[1] <= '9'))
            colon = p;
    if (colon != NULL)
    {
        snprintf(where, sizeof(where), "%.*s", (int)(colon - message), message);
        line = strtoull(colon + 1, NULL, 10);
    }
    compat_report_add(CompatIssue_LuaFunction, what, where, line);
    JUSTMSG("COMPAT: Lua called '%s' (%s line %" PRIu64 "), which this build doesn't have", what, where, line);
    return true;
}

TbBool CheckLua(lua_State *L, int64_t result, const char* func)
{
    if (result != LUA_OK) {
        // Coerce error to string using tostring()
        if (!lua_isstring(L, -1)) {
            lua_getglobal(L, "tostring"); // push tostring
            lua_pushvalue(L, -2);         // push error object
            lua_call(L, 1, 1);            // call tostring(err)
            lua_remove(L, -2);            // remove original error
        }

        const char *message = lua_tostring(L, -1);
        ERRORLOG("Lua error in %s: %s", func, message ? message : "Unknown error");
        lua_report_missing_function(message);
        lua_pop(L, 1); // pop error string

        if (exit_on_lua_error) {
            ERRORLOG("Exiting due to Lua error");
            exit(EXIT_FAILURE);
        }
        return false;
    }
    return true;
}


void close_lua_script()
{
    if(Lvl_script)
        lua_close(Lvl_script);
    Lvl_script = NULL;
}


/**
 * Sets where `require` looks for Lua modules. Only the game's own folders, never LuaJIT's default path (which
 * includes the current directory). With `trusted_only` just fxdata/lua: while the game's own scripts load,
 * a campaign or level file of the same module name must not be able to stand in for one of them.
 */
static int setLuaPath(lua_State* L, TbBool trusted_only)
{
    #define PATH_LENGTH 4096
    char fx_data_path[PATH_LENGTH];
    prepare_file_path_buf(fx_data_path, sizeof(fx_data_path), FGrp_FxData, "lua/?.lua");
    char campaignpath[PATH_LENGTH];
    prepare_file_path_buf(campaignpath, sizeof(campaignpath), FGrp_CmpgConfig, "lua/?.lua");
    char levelpath[PATH_LENGTH];
    prepare_file_path_buf(levelpath, sizeof(levelpath), FGrp_CmpgLvls, "?.lua");

    char new_path[PATH_LENGTH * 3 + 4];
    if (trusted_only)
        snprintf(new_path, sizeof(new_path), "%s", fx_data_path);
    else
        snprintf(new_path, sizeof(new_path), "%s;%s;%s", levelpath, campaignpath, fx_data_path);

    lua_getglobal(L, "package");
    lua_pushstring(L, new_path);
    lua_setfield(L, -2, "path");
    lua_pop(L, 1); // the package table
    return 0;
}


// Function to execute a piece of Lua code, and on failure, print the error message to the console
TbBool execute_lua_code_from_console(const char* code)
{
    if (Lvl_script == NULL) {
        ERRORLOG("Lua state is not initialized");
        return false;
    }

    if (code == NULL) {
        message_add(MsgType_Blank, 0, "no Lua code provided");
        return false;
    }

    int64_t result = lua_sandbox_dostring(Lvl_script, code);
    if (result != LUA_OK) {
        const char *message = lua_tostring(Lvl_script, -1);
        ERRORLOG("Failed to execute Lua code: %s", message ? message : "Unknown error");

        // Pass the error message to message_add
        message_add(MsgType_Blank, 0, "lua error, see log");

        lua_pop(Lvl_script, 2); // Remove error message and traceback
        return false;
    }

    return true;
}

TbBool execute_lua_code_from_script(const char* code)
{
    if (Lvl_script == NULL) {
        ERRORLOG("Lua state is not initialized");
        return false;
    }

    if (code == NULL) {
        return false;
    }

    int64_t result = lua_sandbox_dostring_script(Lvl_script, code);
    if (result != LUA_OK) {
        const char *message = lua_tostring(Lvl_script, -1);
        ERRORLOG("Failed to execute Lua code: %s", message ? message : "Unknown error");

        lua_pop(Lvl_script, 2); // Remove error message and traceback
        return false;
    }

    return true;
}
void lua_set_random_seed(uint64_t seed)
{
    if (Lvl_script == NULL) {
        ERRORLOG("Lua state is not initialized");
        return;
    }

    lua_getglobal(Lvl_script, "math");
    if (lua_istable(Lvl_script, -1)) {
        lua_getfield(Lvl_script, -1, "randomseed");
        if (lua_isfunction(Lvl_script, -1)) {
            lua_pushinteger(Lvl_script, seed);
            if (!CheckLua(Lvl_script, lua_pcall(Lvl_script, 1, 0, 0), "math.randomseed")) {
                ERRORLOG("Failed to call math.randomseed");
            }
        } else {
            ERRORLOG("failed to find math.randomseed function");
            lua_pop(Lvl_script, 1); // Pop nil
        }
    } else {
        ERRORLOG("failed to find math table");
    }
    lua_pop(Lvl_script, 1); // Pop math table or nil
}


/* @comment
 *     The loading items of open_lua_script and open_lua_script_for_mod need to be consistent.
 * @note
 *     We do not setLuaPath for mod, so can load only a single lua file without dependencies.
 */
static TbBool open_lua_script_for_mod(lua_State* L, LevelNumber lvnum, const struct ModConfigItem *mod_item)
{
    TbBool result = false;
    const struct ModExistState *mod_state = &mod_item->state;
    char* fname = NULL;
    char mod_dir[256] = {0};
    sprintf(mod_dir, "%s/%s", MODS_DIR_NAME, mod_item->name);

    // If the mod name contains characters that the identifier does not support, ignore it.
    if (strpbrk(mod_item->name, " -") != NULL)
        return false;

    if (mod_state->fx_data)
    {
        fname = prepare_file_fmtpath_mod(mod_dir, FGrp_FxData, "lua/init.lua");
        if (LbFileExists(fname))
        {
            if (CheckLua(L, lua_sandbox_dofile(L, fname), "mod_fxdata_lua_file"))
            {
                result = true;
            }
            else
            {
                ERRORLOG("failed to load mod fxdata lua script for %s", mod_item->name);
            }
        }
    }

    if (mod_state->cmpg_config)
    {
        fname = prepare_file_fmtpath_mod(mod_dir, FGrp_CmpgConfig, "lua/init.lua");
        if (LbFileExists(fname))
        {
            if (CheckLua(L, lua_sandbox_dofile(L, fname), "mod_campaign_lua_file"))
            {
                result = true;
            }
            else
            {
                ERRORLOG("failed to load mod campaign lua script for %s", mod_item->name);
            }
        }
    }

    if (mod_state->cmpg_lvls)
    {
        int64_t fgroup = get_level_fgroup(lvnum);
        fname = prepare_file_fmtpath_mod(mod_dir, fgroup, "map%05" PRIu64 ".lua", (uint64_t)lvnum);
        if (LbFileExists(fname))
        {
            if(CheckLua(L, lua_sandbox_dofile(L, fname), "mod_level_script_loading"))
            {
                result = true;
            }
            else
            {
                ERRORLOG("failed to load mod level lua script for %s", mod_item->name);
            }
        }
    }

    return result;
}

static void open_lua_script_for_mod_list(lua_State* L, LevelNumber lvnum, const struct ModConfigItem *mod_items, int64_t mod_cnt)
{
    for (int64_t i=0; i<mod_cnt; i++)
    {
        const struct ModConfigItem *mod_item = mod_items + i;
        if (open_lua_script_for_mod(L, lvnum, mod_item))
        {
            // If lua_on_start_for_mod_list has performance issues, a variable can be used to record the state here.
        }
    }
}

static void open_lua_script_for_mod_all(lua_State* L, LevelNumber lvnum)
{
    if (mods_conf.after_base_cnt > 0)
    {
        open_lua_script_for_mod_list(L, lvnum, mods_conf.after_base_item, mods_conf.after_base_cnt);
    }

    if (mods_conf.after_campaign_cnt > 0)
    {
        open_lua_script_for_mod_list(L, lvnum, mods_conf.after_campaign_item, mods_conf.after_campaign_cnt);
    }

    if (mods_conf.after_map_cnt > 0)
    {
        open_lua_script_for_mod_list(L, lvnum, mods_conf.after_map_item, mods_conf.after_map_cnt);
    }
}


/* @comment
 *     The loading items of open_lua_script and open_lua_script_for_mod need to be consistent.
 */
TbBool open_lua_script(LevelNumber lvnum)
{
    Lvl_script = luaL_newstate();

    luaL_openlibs(Lvl_script);
    disable_lua_functions(Lvl_script);

    lua_set_random_seed(kfx_sim_state.action_random_seed);

    reg_host_functions(Lvl_script);

    // Trusted tier: the game's own scripts, with the full standard libraries. Source-only module loading
    // from fxdata/lua alone.
    lua_sandbox_install_loaders(Lvl_script);
    setLuaPath(Lvl_script, true);

    char* fname = prepare_file_fmtpath(FGrp_FxData, "lua/init.lua");

    // Load and parse the Lua File
    if ( !LbFileExists(fname) )
    {
        ERRORLOG("file %s missing",fname);
        return false;
    }
    if(!CheckLua(Lvl_script, lua_sandbox_dofile(Lvl_script, fname), "global_lua_file"))
    {
        ERRORLOG("failed to load global lua script");
        close_lua_script();
        return false;
    }

    // Untrusted tier from here on: campaign, level and mod scripts. The game's own scripts have captured
    // what they need; take away what reaches outside the game.
    lua_sandbox_lock_down(Lvl_script);
    setLuaPath(Lvl_script, false);
    // What the untrusted scripts may use up: the scripts of a level run their top-level code now (more allowed than
    // in a game turn), then lua_on_game_tick() starts the per-turn budget. This replaces a few library functions, so
    // it comes before the seal, which fixes which built-in functions a saved game may name.
    lua_sandbox_limit_resources(Lvl_script, LUA_SANDBOX_LOAD_BUDGET, LUA_SANDBOX_MEMORY_KIB);
    if (!CheckLua(Lvl_script, lua_sandbox_seal_core(Lvl_script), "seal_core_serialisation"))
    {
        ERRORLOG("the game's serialisation script could not be sealed; saved games will not keep script data");
    }

    fname = prepare_file_fmtpath(FGrp_CmpgConfig, "lua/init.lua");
    if (LbFileExists(fname))
    {
        if (!CheckLua(Lvl_script, lua_sandbox_dofile(Lvl_script, fname), "campaign_lua_file"))
        {
            ERRORLOG("failed to load campaign lua script");
        }
    }

    int64_t fgroup = get_level_fgroup(lvnum);
    fname = prepare_file_fmtpath(fgroup, "map%05" PRIu64 ".lua", (uint64_t)lvnum);
    // Load and parse the Lua File
    if (LbFileExists(fname) )
    {
        if(!CheckLua(Lvl_script, lua_sandbox_dofile(Lvl_script, fname), "level_script_loading"))
        {
            ERRORLOG("failed to load level lua script");
        }
    }

    open_lua_script_for_mod_all(Lvl_script, lvnum);

    return true;
}


static char* lua_serialized_data = NULL;

const char* lua_get_serialised_data(size_t *len)
{
    // NULL with KFX_LUA_SERIALISE_FAILED when the scripts' data could not be serialised; NULL with 0 when there is
    // no script state to serialise.
    *len = KFX_LUA_SERIALISE_FAILED;
    if (Lvl_script == NULL) {
        *len = 0;
        return NULL;
    }
    lua_getglobal(Lvl_script, "GetSerializedData");
    if (lua_isfunction(Lvl_script, -1))
    {
        JUSTLOG("calling GetSerializedData");
        int64_t result = lua_pcall(Lvl_script, 0, 1, 0);
        JUSTLOG("lua_pcall result: %" PRId64, (int64_t)(result));
        if (!CheckLua(Lvl_script, result, "GetSerializedData")) {
            ERRORLOG("Failed to call GetSerializedData");
            *len = KFX_LUA_SERIALISE_FAILED;
            return NULL;
        }

        JUSTLOG("called GetSerializedData");
        if (!lua_isstring(Lvl_script, -1)) {
            ERRORLOG("Expected 'GetSerializedData' to return a string");
            lua_pop(Lvl_script, 1);
            *len = KFX_LUA_SERIALISE_FAILED;
            return NULL;
        }
        const char *data = lua_tolstring(Lvl_script, -1, len);  // Get the result
        if (data) {
            lua_serialized_data = (char*)malloc(*len);
            if (lua_serialized_data == NULL) {
                lua_pop(Lvl_script, 1);
                *len = KFX_LUA_SERIALISE_FAILED;
                return NULL;
            }
            memcpy(lua_serialized_data, data, *len);
            lua_pop(Lvl_script, 1);  // Pop the result
            return lua_serialized_data;
        }
        *len = KFX_LUA_SERIALISE_FAILED;
        return NULL;
    }
    else
    {
        ERRORLOG("failed to find GetSerializedData lua function");
        lua_pop(Lvl_script, 1);  // Pop nil
        *len = KFX_LUA_SERIALISE_FAILED;
        return NULL;
    }
}


TbBool lua_set_serialised_data(const char *data, size_t len)
{
    if (Lvl_script == NULL) {
        ERRORLOG("Lvl_script not initialised");
        return false;
    }

    lua_getglobal(Lvl_script, "SetSerializedData");
    if (!lua_isfunction(Lvl_script, -1)) {
        ERRORLOG("failed to find SetSerializedData lua function");
        lua_pop(Lvl_script, 1);
        return false;
    }

    lua_pushlstring(Lvl_script, data, len);
    return CheckLua(Lvl_script, lua_pcall(Lvl_script, 1, 0, 0), "SetSerializedData");
}

void cleanup_serialized_data() {
    if (lua_serialized_data != NULL) {
        free(lua_serialized_data);
        lua_serialized_data = NULL;
    }
}

const char* lua_resync_export(size_t *len)
{
    if (Lvl_script == NULL) {
        *len = 0;
        return "";
    }
    return lua_get_serialised_data(len);
}

TbBool lua_resync_import(const char *data, size_t len)
{
    if (Lvl_script == NULL) {
        if (len > 0) {
            ERRORLOG("Lua state is not initialized");
            return false;
        }
        return true;
    }
    return lua_set_serialised_data(data, len);
}

void generate_lua_types_file()
{
    const char *filename = "native_types.lua";
    FILE *out = fopen(filename, "w");
    if (!out) {
        perror("Failed to open output file");
        return;
    }
    fprintf(out, "---@meta native_types\n");
    fprintf(out, "-- file not used by the game, but used for telling the IDE about what exists\n");
    fprintf(out, "-- this file contains fields changeble through cfg files, the one here in fxdata is the default\n");
    fprintf(out, "-- custom maps/campaigns can generate their own version, so all custom types are still recognized by the IDE\n\n");
    fprintf(out, "-- to generate one with the values specific to your map use the !luatypedump on the specific map you'd like the file for\n\n");

    #define GENERATE_ALIAS(alias_name, desc)            \
    do {                                                \
        int64_t count = 0;                                  \
        for (int64_t i = 0; desc[i].name != NULL; ++i) {    \
            if (desc[i].name[0] != '\0') ++count;       \
        }                                               \
        fprintf(out, "---@alias %s ", alias_name);      \
        if (count >= 90) {                              \
            fprintf(out, "string|");                    \
        }                                               \
        int64_t written = 0;                                \
        for (int64_t i = 0; desc[i].name != NULL; ++i) {    \
            if (desc[i].name[0] == '\0') continue;      \
            if (written > 0) fprintf(out, "|");         \
            fprintf(out, "\"%s\"", desc[i].name);       \
            ++written;                                  \
        }                                               \
        fprintf(out, "\n");                             \
    } while (0)

    #define GENERATE_FIELDS(class_name, desc)               \
    do {                                                 \
        fprintf(out, "---@class %s\n", class_name);       \
        for (int64_t i = 0; desc[i].name != NULL; ++i) {      \
            if (desc[i].name[0] == '\0') continue; \
            fprintf(out, "---@field %s integer\n", desc[i].name); \
        }                                                \
        fprintf(out, "\n");                               \
        } while (0)

    // Generate sections
    GENERATE_ALIAS("creature_type", creature_desc);
    GENERATE_ALIAS("room_type", room_desc);
    GENERATE_ALIAS("power_kind", power_desc);
    GENERATE_ALIAS("trap_type", trap_desc);
    GENERATE_ALIAS("door_type", door_desc);
    GENERATE_ALIAS("object_type", object_desc);
    GENERATE_ALIAS("effect_generator_type", effectgen_desc);
    GENERATE_ALIAS("effect_element_type", effectelem_desc);
    GENERATE_ALIAS("effect_type", effect_desc);
    GENERATE_ALIAS("spell_type", spell_desc);
    GENERATE_ALIAS("slab_type", slab_desc);
    GENERATE_ALIAS("creature_state", creatrstate_desc);
    GENERATE_ALIAS("shot_type", shot_desc);
    fprintf(out, "\n");

    GENERATE_FIELDS("roomfields", room_desc);
    GENERATE_FIELDS("creaturefields", creature_desc);
    GENERATE_FIELDS("trapfields", trap_desc);
    GENERATE_FIELDS("doorfields", door_desc);

    // Cleanup
    #undef GENERATE_ALIAS
    #undef GENERATE_FIELDS

    fclose(out);
}


#ifdef __cplusplus
}
#endif
