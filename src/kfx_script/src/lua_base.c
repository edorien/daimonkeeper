#include "pre_inc.h"

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include <math.h>
#include <string.h>

#include "lua_api.h"
#include "lua_sandbox.h"

#include "bflib_basics.h"
#include "bflib_fileio.h"
#include "config_keeperfx.h"
#include "game_legacy.h"
#include "globals.h"
#include "gui_msgs.h"
#include "lens_api.h"
#include "packet_data.h"

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


/******************************************************************************
 * The scripts' clock (os.time, os.date, os.clock)
 *
 * The host's clock differs between the machines of a multiplayer game, and between recording a replay and playing it
 * back: a script that branched on it would make the game diverge. So while the game has to play the same everywhere
 * -- a multiplayer game, a replay being recorded or played back -- the three functions follow the game's synchronised
 * clock instead:
 * - os.time() is the game's timestamp (kfx_sim_state.timestamp: the host's clock when it was taken, the same on every
 *   peer and in the replay; what GetGameTimestamp() reads) plus the whole seconds of game time since the turn it was
 *   taken on. Game time counts LUA_GAME_CLOCK_TURNS_PER_SECOND turns a second, the game's nominal rate, not the
 *   configured turns_per_second, which comes from the command line or config and can change during a game. When the host sends a new
 *   timestamp (now and then, see set_pending_timestamp_packet_action()) the clock is set to it, so it can step.
 * - os.time(t) and os.date(format, time) read and write dates in the game's time zone (kfx_sim_state.timestamp_tz, the
 *   host's), or in UTC when the format starts with '!'. The conversions are done here, not by the C library, so the
 *   result does not depend on the machine's zone or locale ("C" locale names; %Z gives the offset, as %z does).
 * - os.clock() is the game time since turn 0, in seconds (game turns / LUA_GAME_CLOCK_TURNS_PER_SECOND), standing in
 *   for the processor time it measures otherwise.
 * Otherwise (a single-player game that is not being recorded) they are the standard library's. The choice is made on
 * every call, as the game's mode can change while the scripts' state lives.
 ******************************************************************************/

/** The game turns in a second of the scripts' game clock: the game's nominal rate. */
#define LUA_GAME_CLOCK_TURNS_PER_SECOND 20
/** Date table fields beyond this are refused (os.time returns nil), so the arithmetic below cannot overflow. */
#define LUA_GAME_CLOCK_FIELD_MAX 1.0e9
/** Times beyond this many seconds either side of 1970 are refused by os.date. */
#define LUA_GAME_CLOCK_TIME_MAX 1.0e15

static TbBool lua_game_clock_in_use(void)
{
    return (kfx_sim_state.game_kind == GKind_MultiGame) || replay.save_enable || replay.load_enable;
}

static int64_t floor_div64(int64_t a, int64_t b)
{
    int64_t q = a / b;
    if (((a % b) != 0) && ((a < 0) != (b < 0)))
        q--;
    return q;
}

/** Days since 1970-01-01 of a date of the proleptic Gregorian calendar (month 1-12; any day). */
static int64_t days_from_civil(int64_t year, int64_t month, int64_t day)
{
    year -= (month <= 2);
    const int64_t era = floor_div64(year, 400);
    const int64_t yoe = year - era * 400;
    const int64_t doy = (153 * (month + ((month > 2) ? -3 : 9)) + 2) / 5 + day - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

struct LuaGameDate {
    int64_t year;
    int64_t month;  /**< 1-12 */
    int64_t day;    /**< 1-31 */
    int64_t hour;
    int64_t min;
    int64_t sec;
    int64_t wday;   /**< 0-6, 0 is Sunday */
    int64_t yday;   /**< 0-365 */
};

static void game_date_from_time(int64_t seconds, struct LuaGameDate *date)
{
    const int64_t days = floor_div64(seconds, 86400);
    const int64_t secs = seconds - days * 86400;
    date->hour = secs / 3600;
    date->min = (secs / 60) % 60;
    date->sec = secs % 60;
    date->wday = ((days % 7) + 11) % 7; // 1970-01-01 was a Thursday
    const int64_t z = days + 719468;
    const int64_t era = floor_div64(z, 146097);
    const int64_t doe = z - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp = (5 * doy + 2) / 153;
    date->day = doy - (153 * mp + 2) / 5 + 1;
    date->month = (mp < 10) ? (mp + 3) : (mp - 9);
    date->year = yoe + era * 400 + (date->month <= 2);
    date->yday = days - days_from_civil(date->year, 1, 1);
}

/** The time the scripts' os.time() gives while the game clock is in use. */
static int64_t lua_game_clock_now(void)
{
    const GameTurn turn = get_gameturn();
    int64_t seconds = kfx_sim_state.timestamp;
    if (turn > kfx_sim_state.timestamp_turn)
        seconds += (int64_t)((turn - kfx_sim_state.timestamp_turn) / LUA_GAME_CLOCK_TURNS_PER_SECOND);
    return seconds;
}

/** Calls the standard library's function (upvalue 1) with this call's arguments. */
static int lua_os_function_of_host(lua_State *L)
{
    const int nargs = lua_gettop(L);
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 1);
    lua_call(L, nargs, LUA_MULTRET);
    return lua_gettop(L);
}

static int lua_game_os_clock(lua_State *L)
{
    if (!lua_game_clock_in_use())
        return lua_os_function_of_host(L);
    lua_pushnumber(L, (lua_Number)get_gameturn() / LUA_GAME_CLOCK_TURNS_PER_SECOND);
    return 1;
}

/** A date table field as a whole number; false if it is beyond what os.time takes. */
static TbBool date_field(lua_State *L, const char *key, int64_t default_value, int64_t *value)
{
    lua_getfield(L, 1, key);
    if (lua_isnil(L, -1))
    {
        lua_pop(L, 1);
        if (default_value < 0)
            luaL_error(L, "field '%s' missing in date table", key);
        *value = default_value;
        return true;
    }
    if (!lua_isnumber(L, -1))
        luaL_error(L, "field '%s' is not a number in date table", key);
    const lua_Number n = lua_tonumber(L, -1);
    lua_pop(L, 1);
    if (!(n >= -LUA_GAME_CLOCK_FIELD_MAX && n <= LUA_GAME_CLOCK_FIELD_MAX))
        return false;
    *value = (int64_t)floor(n);
    return true;
}

static int lua_game_os_time(lua_State *L)
{
    if (!lua_game_clock_in_use())
        return lua_os_function_of_host(L);
    if (lua_isnoneornil(L, 1))
    {
        lua_pushnumber(L, (lua_Number)lua_game_clock_now());
        return 1;
    }
    luaL_checktype(L, 1, LUA_TTABLE);
    int64_t year;
    int64_t month;
    int64_t day;
    int64_t hour;
    int64_t min;
    int64_t sec;
    if (!date_field(L, "year", -1, &year) || !date_field(L, "month", -1, &month) || !date_field(L, "day", -1, &day) ||
        !date_field(L, "hour", 12, &hour) || !date_field(L, "min", 0, &min) || !date_field(L, "sec", 0, &sec))
    {
        lua_pushnil(L);
        return 1;
    }
    // Out-of-range fields carry over, as mktime() does: month 13 is January of the next year, day 0 the last of the
    // month before.
    const int64_t month0 = month - 1;
    year += floor_div64(month0, 12);
    month = month0 - floor_div64(month0, 12) * 12 + 1;
    const int64_t days = days_from_civil(year, month, 1) + (day - 1);
    const int64_t local_seconds = days * 86400 + hour * 3600 + min * 60 + sec;
    lua_pushnumber(L, (lua_Number)(local_seconds - kfx_sim_state.timestamp_tz));
    return 1;
}

static const char *const game_date_weekdays[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
static const char *const game_date_months[] = {"January", "February", "March", "April", "May", "June", "July", "August",
                                               "September", "October", "November", "December"};

static void add_number(luaL_Buffer *b, const char *format, int64_t value)
{
    char text[32];
    snprintf(text, sizeof(text), format, value);
    luaL_addstring(b, text);
}

static void add_abbreviation(luaL_Buffer *b, const char *name)
{
    luaL_addlstring(b, name, 3);
}

/** Weeks in an ISO 8601 year: 53 when it starts on a Thursday, or is a leap year starting on a Wednesday. */
static int64_t iso_weeks_in_year(int64_t year)
{
    const int64_t p = (year + floor_div64(year, 4) - floor_div64(year, 100) + floor_div64(year, 400)) % 7;
    const int64_t y1 = year - 1;
    const int64_t p1 = (y1 + floor_div64(y1, 4) - floor_div64(y1, 100) + floor_div64(y1, 400)) % 7;
    return ((p + 7) % 7 == 4 || (p1 + 7) % 7 == 3) ? 53 : 52;
}

static void iso_week(const struct LuaGameDate *date, int64_t *iso_year, int64_t *week)
{
    const int64_t iso_wday = (date->wday == 0) ? 7 : date->wday; // Monday 1 .. Sunday 7
    *iso_year = date->year;
    *week = (date->yday + 1 - iso_wday + 10) / 7;
    if (*week < 1)
    {
        *iso_year = date->year - 1;
        *week = iso_weeks_in_year(*iso_year);
    }
    else if (*week > iso_weeks_in_year(date->year))
    {
        *iso_year = date->year + 1;
        *week = 1;
    }
}

/** strftime() of the "C" locale, for a date of the game's clock. */
static void add_game_date(lua_State *L, luaL_Buffer *b, const char *format, const struct LuaGameDate *date, int64_t utc_offset)
{
    for (const char *p = format; *p != '\0'; p++)
    {
        if (*p != '%')
        {
            luaL_addchar(b, *p);
            continue;
        }
        p++;
        const int64_t hour12 = (date->hour % 12 == 0) ? 12 : (date->hour % 12);
        int64_t iso_year;
        int64_t week;
        switch (*p)
        {
        case 'a': add_abbreviation(b, game_date_weekdays[date->wday]); break;
        case 'A': luaL_addstring(b, game_date_weekdays[date->wday]); break;
        case 'b':
        case 'h': add_abbreviation(b, game_date_months[date->month - 1]); break;
        case 'B': luaL_addstring(b, game_date_months[date->month - 1]); break;
        case 'c': add_game_date(L, b, "%a %b %e %H:%M:%S %Y", date, utc_offset); break;
        case 'C': add_number(b, "%02" PRId64, floor_div64(date->year, 100)); break;
        case 'd': add_number(b, "%02" PRId64, date->day); break;
        case 'D':
        case 'x': add_game_date(L, b, "%m/%d/%y", date, utc_offset); break;
        case 'e': add_number(b, "%2" PRId64, date->day); break;
        case 'F': add_game_date(L, b, "%Y-%m-%d", date, utc_offset); break;
        case 'g': iso_week(date, &iso_year, &week); add_number(b, "%02" PRId64, ((iso_year % 100) + 100) % 100); break;
        case 'G': iso_week(date, &iso_year, &week); add_number(b, "%" PRId64, iso_year); break;
        case 'H': add_number(b, "%02" PRId64, date->hour); break;
        case 'I': add_number(b, "%02" PRId64, hour12); break;
        case 'j': add_number(b, "%03" PRId64, date->yday + 1); break;
        case 'm': add_number(b, "%02" PRId64, date->month); break;
        case 'M': add_number(b, "%02" PRId64, date->min); break;
        case 'n': luaL_addchar(b, '\n'); break;
        case 'p': luaL_addstring(b, (date->hour < 12) ? "AM" : "PM"); break;
        case 'r': add_game_date(L, b, "%I:%M:%S %p", date, utc_offset); break;
        case 'R': add_game_date(L, b, "%H:%M", date, utc_offset); break;
        case 'S': add_number(b, "%02" PRId64, date->sec); break;
        case 't': luaL_addchar(b, '\t'); break;
        case 'T':
        case 'X': add_game_date(L, b, "%H:%M:%S", date, utc_offset); break;
        case 'u': add_number(b, "%" PRId64, (date->wday == 0) ? 7 : date->wday); break;
        case 'U': add_number(b, "%02" PRId64, (date->yday + 7 - date->wday) / 7); break;
        case 'V': iso_week(date, &iso_year, &week); add_number(b, "%02" PRId64, week); break;
        case 'w': add_number(b, "%" PRId64, date->wday); break;
        case 'W': add_number(b, "%02" PRId64, (date->yday + 7 - (date->wday + 6) % 7) / 7); break;
        case 'y': add_number(b, "%02" PRId64, ((date->year % 100) + 100) % 100); break;
        case 'Y': add_number(b, "%" PRId64, date->year); break;
        case 'z':
        case 'Z':
            if ((*p == 'Z') && (utc_offset == 0))
            {
                luaL_addstring(b, "UTC");
            }
            else
            {
                const int64_t minutes = ((utc_offset < 0) ? -utc_offset : utc_offset) / 60;
                luaL_addchar(b, (utc_offset < 0) ? '-' : '+');
                add_number(b, "%04" PRId64, (minutes / 60) * 100 + minutes % 60);
            }
            break;
        case '%': luaL_addchar(b, '%'); break;
        case '\0':
            luaL_error(L, "invalid conversion specifier '%%' at the end of an os.date format");
            return;
        default:
            luaL_error(L, "invalid conversion specifier '%%%c' in an os.date format", *p);
            return;
        }
    }
}

static int lua_game_os_date(lua_State *L)
{
    if (!lua_game_clock_in_use())
        return lua_os_function_of_host(L);
    const char *format = luaL_optstring(L, 1, "%c");
    int64_t seconds;
    if (lua_isnoneornil(L, 2))
    {
        seconds = lua_game_clock_now();
    }
    else
    {
        const lua_Number t = luaL_checknumber(L, 2);
        if (!(t >= -LUA_GAME_CLOCK_TIME_MAX && t <= LUA_GAME_CLOCK_TIME_MAX))
            return luaL_error(L, "time out-of-bounds");
        seconds = (int64_t)floor(t);
    }
    int64_t utc_offset = kfx_sim_state.timestamp_tz;
    if (*format == '!')
    {
        utc_offset = 0;
        format++;
    }
    struct LuaGameDate date;
    game_date_from_time(seconds + utc_offset, &date);
    if (strcmp(format, "*t") == 0)
    {
        lua_createtable(L, 0, 9);
        lua_pushinteger(L, date.year);
        lua_setfield(L, -2, "year");
        lua_pushinteger(L, date.month);
        lua_setfield(L, -2, "month");
        lua_pushinteger(L, date.day);
        lua_setfield(L, -2, "day");
        lua_pushinteger(L, date.hour);
        lua_setfield(L, -2, "hour");
        lua_pushinteger(L, date.min);
        lua_setfield(L, -2, "min");
        lua_pushinteger(L, date.sec);
        lua_setfield(L, -2, "sec");
        lua_pushinteger(L, date.wday + 1);
        lua_setfield(L, -2, "wday");
        lua_pushinteger(L, date.yday + 1);
        lua_setfield(L, -2, "yday");
        lua_pushboolean(L, 0);
        lua_setfield(L, -2, "isdst");
        return 1;
    }
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    add_game_date(L, &b, format, &date, utc_offset);
    luaL_pushresult(&b);
    return 1;
}

/** os.time, os.date and os.clock become the game clock's while it is in use, the standard library's otherwise. */
static void synchronise_lua_clock(lua_State *L)
{
    static const struct { const char *name; lua_CFunction function; } clock_functions[] = {
        {"time", lua_game_os_time},
        {"date", lua_game_os_date},
        {"clock", lua_game_os_clock},
    };
    lua_getglobal(L, "os");
    if (lua_istable(L, -1))
    {
        for (size_t i = 0; i < sizeof(clock_functions) / sizeof(clock_functions[0]); i++)
        {
            lua_getfield(L, -1, clock_functions[i].name); // the standard library's, as the upvalue
            lua_pushcclosure(L, clock_functions[i].function, 1);
            lua_setfield(L, -2, clock_functions[i].name);
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
        // An error that is not a string is reported by its type name. Converting it with tostring() would run
        // script code (the global tostring, or the object's __tostring) outside any protected call: a script that
        // removed tostring or raised an error whose __tostring fails would escape here and end the process
        // (upstream #5430).
        const int error_type = lua_type(L, -1);
        const char *message = lua_typename(L, error_type);
        if (error_type == LUA_TSTRING) {
            message = lua_tostring(L, -1);
            lua_report_missing_function(message);
        }
        ERRORLOG("Lua error in %s: %s", func, message);
        lua_pop(L, 1); // pop error object

        if (exit_on_lua_error) {
            ERRORLOG("Exiting due to Lua error");
            exit(EXIT_FAILURE);
        }
        return false;
    }
    return true;
}


/** Closes the scripts' state, if there is one. Safe to call again: Lvl_script is NULL afterwards. */
void close_lua_script()
{
    if (Lvl_script != NULL)
    {
        // Custom lenses keep a pointer into this state and a reference in its registry: they go first.
        LensManager_ForgetLuaState(LensManager_GetInstance(), Lvl_script);
        lua_close(Lvl_script);
    }
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
    // A level restart and a save load open the scripts again: the state they replace is closed, not leaked
    // (upstream #5430).
    close_lua_script();
    Lvl_script = luaL_newstate();
    if (Lvl_script == NULL)
    {
        ERRORLOG("Failed to allocate Lua state");
        return false;
    }

    luaL_openlibs(Lvl_script);
    synchronise_lua_clock(Lvl_script);

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
        close_lua_script();
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
