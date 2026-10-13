// Security pass S04 (docs/security-pass/README.md): what a script can use up. A runaway script is stopped by a
// deterministic instruction budget per game turn, a ceiling on memory, and a few guards on the library functions that
// do a lot of work in one call; the stop cannot be swallowed by pcall, xpcall or a coroutine; and ordinary use of
// those functions (including yielding across a pcall) is unchanged.
//
// Budgets here are small (the hook counts in millions), so a stopped loop takes a few milliseconds.
extern "C" {
#include <lualib.h>
}

#include "lua_script_fixture.h"
#include "lua_sandbox.h"

#include <chrono>
#include <string>

namespace {

/** Runs a chunk under the engine's protected call; returns its status (0 = ran) with the error message in `message`. */
int run(const std::string &code, std::string *message = nullptr)
{
    int status = luaL_loadstring(Lvl_script, code.c_str());
    if (status == 0)
        status = lua_pcall(Lvl_script, 0, 0, 0);
    if (status != 0) {
        if (message)
            *message = lua_tostring(Lvl_script, -1) ? lua_tostring(Lvl_script, -1) : "";
        lua_pop(Lvl_script, 1);
    }
    return status;
}

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
    const bool r = lua_toboolean(Lvl_script, -1) != 0;
    lua_pop(Lvl_script, 1);
    return r;
}

/** A game turn with a small budget (about `millions` million instructions) and the usual memory ceiling. */
void small_turn(int millions = 3)
{
    lua_sandbox_new_turn(Lvl_script);
    lua_sandbox_set_budget((int64_t)millions * 1000000);
}

struct Limited : LuaScriptFixture {
    Limited() { small_turn(); }
};

double seconds_since(std::chrono::steady_clock::time_point t0)
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

} // namespace

TEST_CASE_METHOD(Limited, "a loop that never ends is stopped", "[kfx_script][lua_limits]") {
    std::string msg;
    const auto t0 = std::chrono::steady_clock::now();
    CHECK(run("while true do end", &msg) != 0);
    CHECK(msg.find("instructions") != std::string::npos);
    CHECK(seconds_since(t0) < 5.0);
    CHECK(lua_sandbox_was_stopped());
    for (const char *loop : {"repeat until false", "for i = 1, math.huge do end", "local n = 0 while true do n = n + 1 end",
                             "local function f() return f() end f()"}) {
        small_turn();
        INFO(loop);
        CHECK(run(loop) != 0);
    }
}

TEST_CASE_METHOD(Limited, "a script cannot swallow the stop and carry on", "[kfx_script][lua_limits]") {
    const auto t0 = std::chrono::steady_clock::now();
    const char *const attempts[] = {
        "while true do pcall(function() while true do end end) end",
        "while true do xpcall(function() while true do end end, function(e) return e end) end",
        "while true do local ok = pcall(function() while true do end end) end",
        "xpcall(function() error('x') end, function() while true do end end) while true do end",
        "while true do local co = coroutine.create(function() while true do end end) coroutine.resume(co) end",
        "while true do pcall(coroutine.wrap(function() while true do end end)) end",
        "local f = coroutine.wrap(function() while true do end end) while true do pcall(f) end",
        "while true do pcall(pcall, pcall, function() while true do end end) end",
        "while true do pcall(function() pcall(function() while true do end end) end) end",
        "local co = coroutine.wrap(function() while true do pcall(function() while true do end end) end end) co()",
    };
    for (const char *attempt : attempts) {
        small_turn();
        INFO(attempt);
        CHECK(run(attempt) != 0);
    }
    CHECK(seconds_since(t0) < 30.0);
}

TEST_CASE_METHOD(Limited, "a new turn starts the budget again", "[kfx_script][lua_limits]") {
    CHECK(run("while true do end") != 0);
    small_turn();
    CHECK(run("local n = 0 for i = 1, 100000 do n = n + i end RESULT = n") == 0);
    CHECK(lua_true("RESULT == 5000050000"));
    // an ordinary amount of work a turn is nowhere near the budget of a real turn
    lua_sandbox_new_turn(Lvl_script);
    CHECK(run("local n = 0 for i = 1, 5000000 do n = n + i end") == 0);
    CHECK_FALSE(lua_sandbox_was_stopped());
}

TEST_CASE_METHOD(Limited, "memory use is capped", "[kfx_script][lua_limits]") {
    lua_sandbox_limit_resources(Lvl_script, 1000000000, 32 * 1024); // 32 MiB
    small_turn(1000);
    std::string msg;
    const auto t0 = std::chrono::steady_clock::now();
    CHECK(run("local t = {} for i = 1, 100000000 do t[i] = {i, i, i} end", &msg) != 0);
    CHECK(msg.find("memory") != std::string::npos);
    CHECK(seconds_since(t0) < 20.0);
    CHECK(run("collectgarbage() collectgarbage('collect')") == 0);
}

TEST_CASE_METHOD(Limited, "library calls that do a lot in one go are limited", "[kfx_script][lua_limits]") {
    std::string msg;
    CHECK(run("return ('x'):rep(2^30)", &msg) != 0);
    CHECK(msg.find("larger than") != std::string::npos);
    CHECK(run("return string.rep('abc', 1e9, ', ')") != 0);
    CHECK(lua_true("('ab'):rep(3) == 'ababab' and ('ab'):rep(3, '-') == 'ab-ab-ab' and ('x'):rep(0) == '' and ('x'):rep(-1) == ''"));
    CHECK(lua_true("#('x'):rep(1000000) == 1000000"));

    // collectgarbage: reading works, the rest does nothing
    CHECK(lua_true("type(collectgarbage('count')) == 'number' and collectgarbage('count') > 0"));
    CHECK(lua_true("collectgarbage('stop') == 0 and collectgarbage('step') == 0 and collectgarbage('setpause', 1) == 0"));
    CHECK(lua_true("collectgarbage('restart') == 0"));

    // the table functions that shift everything after the position
    small_turn();
    CHECK(run("local t = {} for i = 1, 100000 do t[i] = i end for i = 1, 100000 do table.insert(t, 1, i) end") != 0);
    small_turn();
    CHECK(run("local t = {} for i = 1, 100000 do t[i] = i end for i = 1, 100000 do table.remove(t, 1) end") != 0);
    small_turn();
    CHECK(lua_true("(function() local t = {} for i = 1, 1000 do table.insert(t, i) end "
                   "for i = 1, 100 do table.insert(t, 1, i) table.remove(t, 1) end return #t == 1000 end)()"));
    CHECK(lua_true("package.preload['table.new'] == nil and package.preload['table.clear'] ~= nil"));
}

TEST_CASE_METHOD(Limited, "patterns that could take minutes on a long subject are refused", "[kfx_script][lua_limits]") {
    std::string msg;
    CHECK(run("return (('a'):rep(5000)):find('.-.-.-.-b')", &msg) != 0);
    CHECK(msg.find("pattern too expensive") != std::string::npos);
    CHECK(run("return (('a'):rep(5000)):match('(.*)(.*)(.*)b')") != 0);
    CHECK(run("return (('a'):rep(300)):gsub('a?a?a?a?a?a?a?a?a?a?aaaaaaaaaa', 'x')") != 0);
    CHECK(run("for w in (('a'):rep(5000)):gmatch('.-.-.-b') do end") != 0);

    // what scripts use: unchanged
    CHECK(lua_true("('abc'):find('b') == 2"));
    CHECK(lua_true("select(2, ('hello world'):find('o w')) == 7"));
    CHECK(lua_true("('  trim me  '):match('^%s*(.-)%s*$') == 'trim me'"));
    CHECK(lua_true("(('a'):rep(100000)):find('b') == nil"));
    CHECK(lua_true("(('a'):rep(100000)):find('.-b') == nil"));                // one quantifier: linear
    CHECK(lua_true("(('a'):rep(5000)):find('.-.-.-b', 1, true) == nil"));      // plain: no pattern at all
    CHECK(lua_true("('a.b'):find('.', 1, true) == 2"));
    CHECK(lua_true("select('#', ('k=v'):match('(%w+)=(%w+)')) == 2"));
    CHECK(lua_true("('x-y'):gsub('[%-]', '+') == 'x+y'"));                     // a - inside a set is not a quantifier
    CHECK(lua_true("(('[a-z]+'):rep(10)):find('[a-z]+', 1, false) == 2"));
    CHECK(lua_true("(function() local n = 0 for a, b in ('k1=v1,k2=v2'):gmatch('(%w+)=(%w+)') do n = n + 1 end return n == 2 end)()"));
    CHECK(lua_true("('abc'):gsub('%w', function(c) return c:upper() end) == 'ABC'"));
    CHECK(lua_true("('f(a(b)c)d'):match('%b()') == '(a(b)c)'"));
    CHECK(lua_true("('THE (quick) fox'):find('%((%a+)%)') == 5"));
    CHECK(lua_true("('x'):rep(3):find('x', 2) == 2"));
}

TEST_CASE_METHOD(Limited, "pcall, xpcall and coroutines behave as before", "[kfx_script][lua_limits]") {
    CHECK(lua_true("select('#', pcall(function() return 1, 2, 3 end)) == 4"));
    CHECK(lua_true("(function() local ok, e = pcall(error, 'x', 0) return ok == false and e == 'x' end)()"));
    CHECK(lua_true("(function() local ok, e = pcall(error, {code = 7}) return ok == false and e.code == 7 end)()"));
    CHECK(lua_true("(function() local ok, e = pcall(function() local x = nil; return x.y end) return ok == false and type(e) == 'string' end)()"));
    CHECK(lua_true("(function() local a, b, c = xpcall(function(x, y) return x + y, 9 end, print, 3, 4) return a == true and b == 7 and c == 9 end)()"));
    CHECK(lua_true("(function() local ok, e = xpcall(function() error('boom', 0) end, function(m) return 'handled ' .. m end) return ok == false and e == 'handled boom' end)()"));
    CHECK(lua_true("not pcall(pcall)"));

    // a generator in a for loop, resume/yield, error out of a coroutine
    CHECK(lua_true("(function() local sum = 0 for v in coroutine.wrap(function() for i = 1, 5 do coroutine.yield(i) end end) do sum = sum + v end return sum == 15 end)()"));
    CHECK(lua_true("(function() local co = coroutine.create(function(a) local b = coroutine.yield(a + 1) return b * 2 end) "
                   "local ok1, v1 = coroutine.resume(co, 1) local ok2, v2 = coroutine.resume(co, 10) "
                   "return ok1 and v1 == 2 and ok2 and v2 == 20 and coroutine.status(co) == 'dead' end)()"));
    CHECK(lua_true("(function() local co = coroutine.create(function() error('inside') end) local ok, e = coroutine.resume(co) "
                   "return ok == false and tostring(e):find('inside', 1, true) ~= nil end)()"));
    CHECK(lua_true("not pcall(coroutine.wrap(function() error('inside wrap') end))"));

    // yielding across a pcall (scripts written as coroutines rely on it)
    CHECK(lua_true("(function() local co = coroutine.wrap(function() local ok, v = pcall(function() return coroutine.yield(1) end) return v end) "
                   "return co() == 1 and co(7) == 7 end)()"));
    CHECK(lua_true("(function() local co = coroutine.wrap(function() local ok, v = xpcall(function() return coroutine.yield(1) end, print) return v end) "
                   "return co() == 1 and co(8) == 8 end)()"));
}

TEST_CASE_METHOD(Limited, "the game's own event processing still works under the limits", "[kfx_script][lua_limits]") {
    REQUIRE(run("FIRED = 0; function SandboxLimitsAction() FIRED = FIRED + 1 end; "
                "CreateTrigger('Limits', SandboxLimitsAction, {})") == 0);
    std::string msg;
    const int st = run("ProcessEvent('Limits', {}) ProcessEvent('Limits', {})", &msg);
    INFO(msg);
    REQUIRE(st == 0);
    CHECK(lua_true("FIRED == 2"));
    // an error in one trigger's action is collected by the trigger system (its pcall) as before
    // (anonymous functions must come from a script the game loaded: a level script's block, as here)
    REQUIRE(execute_lua_code_from_script("CreateTrigger('Limits', function() error('trigger failed') end, {})"));
    CHECK(run("ProcessEvent('Limits', {})") == 0);
    // ... but a runaway one stops the whole event, and the game gets control back
    small_turn();
    REQUIRE(execute_lua_code_from_script("CreateTrigger('Limits', function() while true do end end, {})"));
    CHECK(run("ProcessEvent('Limits', {})") != 0);
    CHECK(lua_sandbox_was_stopped());
}
