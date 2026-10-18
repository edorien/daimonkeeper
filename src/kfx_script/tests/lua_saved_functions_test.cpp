// Security pass S03 (docs/security-pass/README.md): what a saved game's script data can hold, and what loading it
// can do. A function is kept as a reference -- a global's name, or the exact text of an anonymous function that a
// script the game loaded contains -- never as bytecode, and loading only ever finds functions of the scripts that are
// loaded at that moment. These cases save in one Lua session, load in a fresh one, and feed loading blobs made to
// carry what a hostile save or resync could.
//
// The bytecode used is the compiled form of `return 1` (made in a throw-away state): the cases need to show that
// bytecode is refused, not to carry anything.
extern "C" {
#include <lualib.h>
}

#include "lua_script_fixture.h"
#include "lua_sandbox.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

namespace {

namespace fs = std::filesystem;

bool lua_true(const std::string &expr)
{
    const std::string chunk = "return (" + expr + ")";
    if (luaL_loadstring(Lvl_script, chunk.c_str()) != 0) {
        lua_pop(Lvl_script, 1);
        return false;
    }
    if (lua_pcall(Lvl_script, 0, 1, 0) != 0) {
        lua_pop(Lvl_script, 1);
        return false;
    }
    const bool result = lua_toboolean(Lvl_script, -1) != 0;
    lua_pop(Lvl_script, 1);
    return result;
}

std::string lua_string(const std::string &expr)
{
    const std::string chunk = "return (" + expr + ")";
    REQUIRE(luaL_loadstring(Lvl_script, chunk.c_str()) == 0);
    REQUIRE(lua_pcall(Lvl_script, 0, 1, 0) == 0);
    size_t len = 0;
    const char *data = lua_tolstring(Lvl_script, -1, &len);
    std::string out = data ? std::string(data, len) : std::string();
    lua_pop(Lvl_script, 1);
    return out;
}

void set_global(const char *name, const std::string &value)
{
    lua_pushlstring(Lvl_script, value.data(), value.size());
    lua_setglobal(Lvl_script, name);
}

struct TempDir {
    fs::path dir;
    TempDir() : dir(fs::temp_directory_path() / ("kfx_lua_saved_fn_" + std::to_string(std::random_device{}()))) { fs::create_directories(dir); }
    ~TempDir() { std::error_code ec; fs::remove_all(dir, ec); }
    std::string write(const char *name, const std::string &content) const
    {
        const fs::path p = dir / name;
        std::ofstream(p, std::ios::binary) << content;
        return p.generic_string();
    }
};

/** Runs a script file the way the game runs a level script: source only, its text kept. */
void load_script(const std::string &path)
{
    INFO(path);
    const int status = lua_sandbox_dofile(Lvl_script, path.c_str());
    if (status != 0) {
        INFO(lua_tostring(Lvl_script, -1));
        FAIL("script did not load");
    }
}

const char *const kScript =
    "Fns = {}\n"
    "Fns.add = function(a, b) return a + b end\n"
    "Fns.twice = function(x)\n"
    "    local r = 0\n"
    "    for i = 1, 2 do r = r + x end\n"
    "    return r\n"
    "end\n"
    "Fns.vararg = function(...) return select('#', ...) end\n"
    "function GlobalHelper(x) return x * 3 end\n";

std::string make_bytecode()
{
    lua_State *B = luaL_newstate();
    luaL_openlibs(B);
    REQUIRE(luaL_dostring(B, "return string.dump(function() return 1 end)") == 0);
    size_t len = 0;
    const char *data = lua_tolstring(B, -1, &len);
    std::string out(data, len);
    lua_close(B);
    REQUIRE(out.size() > 8);
    return out;
}

// binser's wire format, enough to build blobs by hand: a count/length as one byte (n + 27, up to 100) or two
std::string number(size_t n)
{
    if (n <= 100)
        return std::string(1, (char)(n + 27));
    n += 8192;
    return std::string{(char)(128 + (n >> 8)), (char)(n & 0xFF)};
}
std::string str(const std::string &s) { return "\xCE" + number(s.size()) + s; }
const std::string kVersion3 = std::string(1, (char)(3 + 27));
/** The blob of a game table holding `value` (an already encoded value) under the key "f", saved as version 3. */
std::string game_with(const std::string &value)
{
    return "\xCF" + number(0) + number(1) + str("f") + value + kVersion3;
}

} // namespace

TEST_CASE("function_source finds each function's text and skips strings, comments and other blocks", "[kfx_script][lua_saved_functions]") {
    LuaScriptFixture fx;
    set_global("SRC",
        "local s = \"end function\" -- function end\n"
        "--[[ function( end ]] local t = [==[ function() end ]==]\n"
        "local a = function(x) return x end\n"
        "local b = function(...)\n"
        "  if x then return 1 elseif y then return 2 else return 3 end\n"
        "  while z do repeat q() until w end\n"
        "  for i = 1, 3 do do end end\n"
        "  return function(y) return 'end' end\n"
        "end\n"
        "function named(p) end\n");
    REQUIRE(lua_true("#require('core.function_source')._scan(SRC) == 4"));
    // inner functions come first (their `end` comes first); the named one is marked as such
    CHECK(lua_true("require('core.function_source')._scan(SRC)[1].text == 'function(x) return x end'"));
    CHECK(lua_true("require('core.function_source')._scan(SRC)[2].text == \"function(y) return 'end' end\""));
    CHECK(lua_true("require('core.function_source')._scan(SRC)[3].nparams == 0 and "
                   "require('core.function_source')._scan(SRC)[3].vararg == true"));
    CHECK(lua_true("require('core.function_source')._scan(SRC)[3].last == 9"));
    CHECK(lua_true("require('core.function_source')._scan(SRC)[4].named == true"));
}

TEST_CASE("scripts the game loads keep their text, by chunk name", "[kfx_script][lua_saved_functions]") {
    LuaScriptFixture fx;
    TempDir tmp;
    const std::string path = tmp.write("kept.lua", kScript);
    load_script(path);
    set_global("CHUNK", "@" + path);
    CHECK(lua_true("Fns.add(2, 3) == 5"));
    // the text is reachable by the game's own scripts only
    CHECK(lua_true("__kfx_chunk_text == nil"));
    CHECK(lua_true("pcall(require('core.function_source').describe, Fns.add)"));
    CHECK(lua_true("select(1, require('core.function_source').describe(Fns.add)) == 'source'"));
    CHECK(lua_true("select(2, require('core.function_source').describe(Fns.add)) == 'function(a, b) return a + b end'"));
    CHECK(lua_true("select(1, require('core.function_source').describe(GlobalHelper)) == 'global'"));
    CHECK(lua_true("select(2, require('core.function_source').describe(GlobalHelper)) == 'GlobalHelper'"));
}

TEST_CASE("functions saved in one session are found again in a fresh one", "[kfx_script][lua_saved_functions]") {
    TempDir tmp;
    const std::string path = tmp.write("level.lua", kScript);
    std::string blob;
    {
        LuaScriptFixture fx;
        load_script(path);
        REQUIRE(execute_lua_code_from_script(
            "Game.f = { add = Fns.add, twice = Fns.twice, vararg = Fns.vararg, helper = GlobalHelper, same = Fns.add,\n"
            "           keep = string.rep, say = print }\n"
            "Game.trigger = CreateTrigger('OnTurn', Fns.twice)\n"));
        blob = lua_string("GetSerializedData()");
        REQUIRE_FALSE(blob.empty());
        CHECK(blob.find('\xD2') == std::string::npos);                       // no function written as bytecode
        CHECK(blob.find(std::string("\x1b") + "LJ") == std::string::npos);
    }
    {
        LuaScriptFixture fx;                                                 // a fresh Lua state: nothing carried over
        load_script(path);
        set_global("BLOB", blob);
        REQUIRE(lua_true("pcall(SetSerializedData, BLOB)"));
        CHECK(lua_true("Game.f.add(2, 3) == 5"));
        CHECK(lua_true("Game.f.twice(4) == 8"));
        CHECK(lua_true("Game.f.vararg(1, 2, 3) == 3"));
        CHECK(lua_true("Game.f.helper == GlobalHelper"));
        CHECK(lua_true("Game.f.helper(2) == 6"));
        CHECK(lua_true("Game.f.same == Game.f.add"));                        // one function stays one function
        CHECK(lua_true("Game.f.keep == string.rep and Game.f.say == print"));
        CHECK(lua_true("Game.trigger.action == Game.f.twice or type(Game.triggers[1].action) == 'function'"));
        // a restored function can be saved again, and what comes out loads again
        CHECK(lua_true("pcall(SetSerializedData, GetSerializedData())"));
        CHECK(lua_true("Game.f.twice(5) == 10"));
    }
}

TEST_CASE("loading a save refuses what is not a function of the loaded scripts", "[kfx_script][lua_saved_functions]") {
    LuaScriptFixture fx;
    TempDir tmp;
    load_script(tmp.write("level.lua", kScript));
    REQUIRE(execute_lua_code_from_script("Game.marker = 'untouched'"));

    auto refused = [](const std::string &blob, const char *why) {
        INFO(why);
        set_global("HOSTILE", blob);
        CHECK(lua_true("pcall(SetSerializedData, HOSTILE) == false"));
        CHECK(lua_true("Game.marker == 'untouched'"));                       // the running game is left as it was
    };

    // bytecode, however it is wrapped
    refused(game_with("\xD2" + number(make_bytecode().size()) + make_bytecode()), "bytecode function (210)");
    refused(game_with("\xD2" + number(3) + "abc"), "bytecode function, garbage");
    // text that is not a function of any loaded script, however harmless it looks
    refused(game_with("\xD6" + str("source") + str("function() return 'NOT-IN-ANY-LOADED-SCRIPT' end")), "source not in scripts");
    refused(game_with("\xD6" + str("source") + str("function(a, b) return a + b end end end")), "source with extra tokens");
    refused(game_with("\xD6" + str("source") + str("function(a, b) return a + b end ")), "source that differs by a space");
    refused(game_with("\xD6" + str("source") + str("function(a, b) return a - b end")), "source that differs by a character");
    // global references that are not Lua functions the scripts define
    for (const char *name : {"os", "load", "loadstring", "require", "print", "string", "nonexistent", "Game", "Fns"})
        refused(game_with("\xD6" + str("global") + str(name)), name);
    refused(game_with("\xD6" + str("global") + str("..")), "global ..");
    refused(game_with("\xD6" + str("nothing") + str("x")), "unknown kind");
    refused(game_with("\xD6" + number(5) + str("x")), "kind that is not a string");
    // built-in functions by name that were never registered for saves
    for (const char *name : {"global:os.execute", "global:os.time", "global:io.open", "global:load", "global:require",
                             "global:debug.getinfo", "global:setmetatable", "global:ffi.C", "global:package.loadlib",
                             "registry:FILE*.write", "global:jit.off"})
        refused(game_with("\xD3" + str(name)), name);

    // controls: the same blobs with a reference to something that *is* there load, so the cases above were
    // refused for what they hold and not for how they are built
    auto accepted = [](const std::string &blob, const char *check, const char *why) {
        INFO(why);
        set_global("CONTROL", blob);
        CHECK(lua_true("pcall(SetSerializedData, CONTROL)"));
        CHECK(lua_true(check));
    };
    accepted(game_with("\xD6" + str("global") + str("GlobalHelper")), "Game.f == GlobalHelper", "global control");
    accepted(game_with("\xD6" + str("source") + str("function(a, b) return a + b end")), "Game.f(2, 3) == 5", "source control");
    accepted(game_with("\xD3" + str("global:string.rep")), "Game.f == string.rep", "resource control");
    REQUIRE(execute_lua_code_from_script("Game.marker = 'untouched'"));

    // versions this game does not read, and data that is not a game table
    const std::string empty_table = "\xCF" + number(0) + number(0);
    refused(empty_table + number(4), "version 4");
    refused(empty_table + number(1), "version 1 (the old bytecode-in-base64 format)");
    refused(number(0) + number(3), "no table");
    refused("", "empty");
    refused(std::string(1, '\xFF'), "garbage");

    // control: the helpers above do build blobs the loader reads
    set_global("CONTROL", empty_table + number(3));
    CHECK(lua_true("pcall(SetSerializedData, CONTROL) and Game.marker == nil"));
    REQUIRE(execute_lua_code_from_script("Game.marker = 'untouched'"));
    set_global("CONTROL", empty_table + number(2));
    CHECK(lua_true("pcall(SetSerializedData, CONTROL)"));                     // version 2 data without functions still loads
}

TEST_CASE("only the harmless built-ins can be saved by name", "[kfx_script][lua_saved_functions]") {
    LuaScriptFixture fx;
    // allowed: the engine's own API and the safe parts of the standard library
    CHECK(lua_true("pcall(GetSerializedData)"));
    for (const char *fn : {"string.rep", "table.insert", "math.floor", "print", "tostring", "pairs", "bit.band"}) {
        INFO(fn);
        CHECK(lua_true(std::string("(function() Game.f = ") + fn + "; local ok = pcall(GetSerializedData); Game.f = nil; return ok end)()"));
    }
    // not allowed: they would be callable by a script with data from the save
    for (const char *fn : {"os.time", "os.clock", "load", "loadstring", "require", "setmetatable", "rawset", "collectgarbage"}) {
        INFO(fn);
        CHECK(lua_true(std::string("(function() Game.f = ") + fn + "; local ok = pcall(GetSerializedData); Game.f = nil; return not ok end)()"));
    }
}

TEST_CASE("a function that can't be kept in a save is refused when it is registered", "[kfx_script][lua_saved_functions]") {
    LuaScriptFixture fx;
    TempDir tmp;
    load_script(tmp.write("level.lua",
        std::string(kScript) +
        "Fns.pair = { function(a) return a end, function(a) return a + 1 end }\n"
        "Fns.same = { function(a) return a end, function(a) return a end }\n"
        "local function helper() return 1 end\n"
        "Fns.helper = helper\n"));
    // from a script file: fine
    CHECK(lua_true("pcall(CreateTrigger, 'OnTurn', Fns.add)"));
    CHECK(lua_true("pcall(CreateTrigger, 'OnTurn', Fns.same[1])"));              // two identical texts on a line: no doubt which
    // made at run time from a string, not from a script the game loaded
    CHECK(lua_true("not pcall(CreateTrigger, 'OnTurn', loadstring('return function() return 5 end')())"));
    CHECK(lua_true("select(2, pcall(CreateTrigger, 'OnTurn', loadstring('return function() return 5 end')())):find('script file', 1, true) ~= nil"));
    // two different functions of the same shape on one line
    CHECK(lua_true("not pcall(CreateTrigger, 'OnTurn', Fns.pair[1])"));
    CHECK(lua_true("select(2, pcall(CreateTrigger, 'OnTurn', Fns.pair[2])):find('line of its own', 1, true) ~= nil"));
    // a named local function
    CHECK(lua_true("not pcall(CreateTrigger, 'OnTurn', Fns.helper)"));
    // a block of a level script (LUA ... ENDLUA) is kept like a file
    REQUIRE(execute_lua_code_from_script("SCRIPT_BLOCK = { fn = function(x) return x .. '!' end }"));
    CHECK(lua_true("pcall(CreateTrigger, 'OnTurn', SCRIPT_BLOCK.fn)"));
    CHECK(lua_true("select(1, require('core.function_source').describe(SCRIPT_BLOCK.fn)) == 'source'"));
}

TEST_CASE("binser keeps NaN, infinities and registered 64-bit integers", "[kfx_script][lua_saved_functions]") {
    std::string blob;
    {
        LuaScriptFixture fx;
        REQUIRE(execute_lua_code_from_script("Game.nan = 0/0; Game.inf = 1/0; Game.ninf = -1/0; Game.half = 0.5"));
        blob = lua_string("GetSerializedData()");
    }
    {
        LuaScriptFixture fx;
        set_global("BLOB", blob);
        REQUIRE(lua_true("pcall(SetSerializedData, BLOB)"));
        CHECK(lua_true("Game.nan ~= Game.nan"));                             // NaN used to be read back as 0
        CHECK(lua_true("Game.inf == 1/0 and Game.ninf == -1/0 and Game.half == 0.5"));
    }
    LuaScriptFixture fx;
    // A 64-bit integer literal is cdata; one registered as a resource is written by name. types.cdata passed the
    // length of the output instead of the output itself, so this raised an error instead.
    CHECK(lua_true("(function()\n"
                   "  local b = require('external.binser').newbinser()\n"
                   "  local big = 5LL\n"
                   "  b.registerResource(big, 'big')\n"
                   "  local ok, s = pcall(b.serialize, big)\n"
                   "  if not ok then return false end\n"
                   "  local v = b.deserialize(s)\n"
                   "  return v[1] == big\n"
                   "end)()"));
}
