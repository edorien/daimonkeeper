// Security pass S01 (docs/security-pass/README.md): what a campaign, level or mod script can reach in the
// production Lua state. Each case opens the state the way the game does (open_lua_script(), which loads the
// game's own scripts and then locks the state down) and checks that the way out of the game is not there:
// no files, processes, native code or debug library, and no bytecode loaded from anywhere.
//
// The bytecode used here is the compiled form of `return 1`, made in a throw-away state: the cases only
// need to show that a binary chunk is refused, not to carry anything.
extern "C" {
#include <lualib.h> // luaL_openlibs
}

#include "lua_script_fixture.h"
#include "lua_sandbox.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

namespace {

/** Evaluates a Lua expression in the production state; true if it came out true. */
bool eval_true(const std::string &expr)
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

/** The compiled form of `return 1`, made in a state of its own. */
std::string make_bytecode()
{
    lua_State *B = luaL_newstate();
    luaL_openlibs(B);
    REQUIRE(luaL_dostring(B, "return string.dump(function() return 1 end)") == 0);
    size_t len = 0;
    const char *data = lua_tolstring(B, -1, &len);
    std::string out(data, len);
    lua_close(B);
    REQUIRE(!out.empty());
    REQUIRE(out[0] == '\x1b'); // really a binary chunk
    return out;
}

void set_global_string(const char *name, const std::string &value)
{
    lua_pushlstring(Lvl_script, value.data(), value.size());
    lua_setglobal(Lvl_script, name);
}

} // namespace

TEST_CASE_METHOD(LuaScriptFixture, "untrusted Lua has no way out of the game", "[kfx_script][lua_sandbox]") {
    const char *const gone[] = {
        // libraries
        "ffi", "jit", "io", "debug",
        // the game's own access to the text of loaded scripts
        "__kfx_chunk_text", "__kfx_chunk_names",
        // loading code from files
        "dofile", "loadfile",
        // os: processes, environment, files
        "os.execute", "os.exit", "os.getenv", "os.remove", "os.rename", "os.tmpname", "os.setlocale",
        // bytecode out of functions
        "string.dump",
        // native libraries and file system probing
        "package.loadlib", "package.searchpath", "package.loaders[3]", "package.loaders[4]",
        // the same names reached as loaded or built-in modules
        "package.loaded.io", "package.loaded.debug", "package.loaded.ffi", "package.loaded.jit",
        "package.preload['string.buffer']", "package.preload['jit.util']", "package.preload['jit.profile']",
        "package.preload['ffi']",
    };
    for (const char *name : gone) {
        INFO(name);
        CHECK(eval_true(std::string(name) + " == nil"));
    }
    CHECK(eval_true("package.cpath == ''"));
}

TEST_CASE_METHOD(LuaScriptFixture, "removed modules cannot be required back", "[kfx_script][lua_sandbox]") {
    for (const char *name : {"ffi", "jit", "jit.util", "jit.profile", "string.buffer", "io", "debug"}) {
        INFO(name);
        CHECK(eval_true(std::string("not pcall(require, '") + name + "')"));
    }
}

TEST_CASE_METHOD(LuaScriptFixture, "what scripts legitimately use is still there", "[kfx_script][lua_sandbox]") {
    for (const char *name : {"type(os.time)", "type(os.date)", "type(os.clock)", "type(os.difftime)",
                             "type(string.format)", "type(string.rep)", "type(table.concat)", "type(math.random)",
                             "type(bit.band)", "type(coroutine.create)", "type(require)", "type(pcall)",
                             "type(setmetatable)", "type(print)", "type(load)", "type(loadstring)",
                             "type(package.loaded)", "type(package.path)"}) {
        INFO(name);
        CHECK(eval_true(std::string(name) + " ~= 'nil'"));
    }
    CHECK(eval_true("require('table.clear') ~= nil"));
    CHECK(eval_true("not pcall(require, 'table.new')")); // allocates what it is told to in one call (security pass S04)
}

TEST_CASE_METHOD(LuaScriptFixture, "load and loadstring take source only", "[kfx_script][lua_sandbox]") {
    set_global_string("BYTECODE", make_bytecode());

    // refused, however it is asked for
    CHECK(eval_true("load(BYTECODE) == nil"));
    CHECK(eval_true("load(BYTECODE, 'x', 'b') == nil"));
    CHECK(eval_true("load(BYTECODE, 'x', 'bt') == nil"));
    CHECK(eval_true("loadstring(BYTECODE) == nil"));
    CHECK(eval_true("loadstring(BYTECODE, 'x') == nil"));
    CHECK(eval_true("load(function() local s = BYTECODE; BYTECODE_DONE = not BYTECODE_DONE; "
                    "if BYTECODE_DONE then return s end end) == nil"));

    // the same functions on source: unchanged behaviour, including the environment argument
    CHECK(eval_true("load('return 1')() == 1"));
    CHECK(eval_true("loadstring('return 2')() == 2"));
    CHECK(eval_true("loadstring('return 3', 'name')() == 3"));
    CHECK(eval_true("load('return x', 'c', 't', {x = 5})() == 5"));
    CHECK(eval_true("select('#', load('this is not lua')) == 2"));
    CHECK(eval_true("not pcall(load)"));
}

TEST_CASE_METHOD(LuaScriptFixture, "script text from a level or the console is run as source only", "[kfx_script][lua_sandbox]") {
    const std::string bytecode = make_bytecode();
    CHECK_FALSE(execute_lua_code_from_script(bytecode.c_str()));
    CHECK(execute_lua_code_from_script("SANDBOX_TEST_RAN = true"));
    CHECK(eval_true("SANDBOX_TEST_RAN == true"));
    lua_pushnil(Lvl_script);
    lua_setglobal(Lvl_script, "SANDBOX_TEST_RAN");
}

TEST_CASE("module names are plain", "[kfx_script][lua_sandbox]") {
    for (const char *ok : {"a", "core.serialisation", "my-mod.helpers_2", "A.B.C"})
        CHECK(lua_sandbox_module_name_ok(ok));
    for (const char *bad : {"", ".", "..", "../x", "a/b", "a\\b", ".a", "a.", "a..b", "a b", "a:b", "C:x", "a\n", "a%s"})
        CHECK_FALSE(lua_sandbox_module_name_ok(bad));
    CHECK_FALSE(lua_sandbox_module_name_ok(nullptr));
}

TEST_CASE_METHOD(LuaScriptFixture, "require loads Lua source from the script folders and nothing else", "[kfx_script][lua_sandbox]") {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / ("kfx_lua_sandbox_test_" + std::to_string(std::random_device{}()));
    fs::create_directories(dir);
    {
        std::ofstream(dir / "goodmod.lua") << "return { answer = 42 }\n";
        std::ofstream(dir / "badsyntax.lua") << "this is not lua\n";
        const std::string bytecode = make_bytecode();
        std::ofstream(dir / "binmod.lua", std::ios::binary).write(bytecode.data(), (std::streamsize)bytecode.size());
        std::ofstream(dir / "binmod.so") << "not a library";
    }
    std::string path = dir.generic_string() + "/?.lua";
    set_global_string("TEST_PATH", path);
    CHECK(eval_true("(function() package.path = TEST_PATH; return true end)()"));

    CHECK(eval_true("require('goodmod').answer == 42"));
    CHECK(eval_true("not pcall(require, 'binmod')"));           // bytecode in a .lua file: refused
    CHECK(eval_true("not pcall(require, 'badsyntax')"));
    CHECK(eval_true("not pcall(require, 'nonexistent_module')"));
    // names that could leave the folder are refused before any file is looked for
    for (const char *name : {"../goodmod", "sub/../goodmod", "/goodmod", "a\\\\b", ".goodmod", "goodmod.", "a..b"}) {
        INFO(name);
        CHECK(eval_true(std::string("not pcall(require, '") + name + "')"));
    }
    // a native library next to the scripts is not looked for at all
    CHECK(eval_true("not pcall(require, 'binmod.so')"));

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE_METHOD(LuaScriptFixture, "the module path is the game's folders only", "[kfx_script][lua_sandbox]") {
    // LuaJIT's default path would add ./?.lua and its install directories
    CHECK(eval_true("package.path:find('luajit', 1, true) == nil"));
    CHECK(eval_true("package.path:find('./?.lua', 1, true) == nil"));
    CHECK(eval_true("package.path:find('fxdata', 1, true) ~= nil"));
}

TEST_CASE_METHOD(LuaScriptFixture, "the game's own scripts still work with the debug library gone", "[kfx_script][lua_sandbox]") {
    // TriggerSystem looks at a function's name and upvalues with debug.*; serialisation walks the registry
    CHECK(eval_true("debug == nil"));
    CHECK(execute_lua_code_from_script(
        "function SandboxTestAction() end\n"
        "SANDBOX_TRIGGER_A = CreateTrigger('OnTurn', SandboxTestAction)\n"       // by global name (debug.getinfo)
        "SANDBOX_TRIGGER_B = CreateTrigger('OnTurn', function() return 1 end)\n" // anonymous (debug.getupvalue)
        "local captured = {}\n"
        "SANDBOX_ERR = select(2, pcall(CreateTrigger, 'OnTurn', function() return captured end))\n")); // upvalue table: refused
    CHECK(eval_true("SANDBOX_TRIGGER_A.action == 'SandboxTestAction'"));
    CHECK(eval_true("type(SANDBOX_TRIGGER_B.action) == 'function'"));
    CHECK(eval_true("tostring(SANDBOX_ERR):find('upvalue', 1, true) ~= nil"));

    // a value type that needs the registry-based registrations of core/serialisation.lua
    CHECK(eval_true("type(GetSerializedData()) == 'string'"));
    CHECK(eval_true("pcall(SetSerializedData, GetSerializedData())"));

    for (const char *g : {"SandboxTestAction", "SANDBOX_TRIGGER_A", "SANDBOX_TRIGGER_B", "SANDBOX_ERR"}) {
        lua_pushnil(Lvl_script);
        lua_setglobal(Lvl_script, g);
    }
}

// An older or damaged core.serialisation (one that returns no module table) is reported as an error status; it must
// not raise a Lua error outside a protected call, which on Windows is an unhandled exception and elsewhere a panic.
TEST_CASE_METHOD(LuaScriptFixture, "sealing the serialisation script reports a bad module instead of raising", "[kfx_script][lua_sandbox]") {
    REQUIRE(luaL_dostring(Lvl_script, "package.loaded['core.serialisation'] = true") == 0);
    const int top = lua_gettop(Lvl_script);
    CHECK(lua_sandbox_seal_core(Lvl_script) != 0);
    lua_settop(Lvl_script, top);
    REQUIRE(luaL_dostring(Lvl_script, "package.loaded['core.serialisation'] = nil") == 0);
}
