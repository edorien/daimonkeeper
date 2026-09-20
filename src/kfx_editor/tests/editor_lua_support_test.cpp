#include <catch2/catch_test_macros.hpp>
#include "editor_lua_support.h"

#include <filesystem>
#include <fstream>

TEST_CASE("scan_api finds functions, class members and constants in the shipped stubs", "[kfx_editor][lua]") {
    const auto names = editor_lua_scan_api(std::string(KFX_SOURCE_DIR) + "/config/fxdata/lua");
    CHECK(names.count("WinGame"));
    CHECK(names.count("SetDigger"));
    CHECK(names.count("RegisterTimerEvent"));
    CHECK_FALSE(names.count("local"));
    CHECK(names.size() > 100);
}

TEST_CASE("scan_api on a synthetic file", "[kfx_editor][lua]") {
    const auto dir = std::filesystem::temp_directory_path() / "kfx_lua_api_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "sub");
    std::ofstream(dir / "sub" / "a.lua") << "function Foo(a) end\r\nfunction Bar.baz(x)\r\nlocal hidden = 1\r\nMAXV = 5\r\nif x == 1 then\r\n";
    const auto names = editor_lua_scan_api(dir.string());
    CHECK(names.count("Foo"));
    CHECK(names.count("Bar"));
    CHECK(names.count("MAXV"));
    CHECK_FALSE(names.count("hidden"));
    CHECK_FALSE(names.count("x"));
    std::filesystem::remove_all(dir);
}

TEST_CASE("require_at finds the module under the cursor", "[kfx_editor][lua]") {
    const std::string line = "local m = require \"dungeon_architect\" -- x";
    CHECK(editor_lua_require_at(line, 12) == "dungeon_architect");
    CHECK(editor_lua_require_at(line, 25) == "dungeon_architect");
    CHECK(editor_lua_require_at(line, 2).empty());
    CHECK(editor_lua_require_at("x = require('a.b')", 6) == "a.b");
    CHECK(editor_lua_require_at("x = myrequire('a.b')", 6).empty());
}

TEST_CASE("resolve_module walks the roots in order and maps dots to folders", "[kfx_editor][lua]") {
    const auto dir = std::filesystem::temp_directory_path() / "kfx_lua_resolve_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "one");
    std::filesystem::create_directories(dir / "two" / "pkg");
    std::ofstream(dir / "one" / "m.lua") << "1";
    std::ofstream(dir / "two" / "m.lua") << "2";
    std::ofstream(dir / "two" / "pkg" / "n.lua") << "3";
    const std::vector<std::string> roots = {(dir / "one").string(), (dir / "two").string()};
    CHECK(editor_lua_resolve_module("m", roots) == (dir / "one" / "m.lua").string());
    CHECK(editor_lua_resolve_module("pkg.n", roots) == (dir / "two" / "pkg" / "n.lua").string());
    CHECK(editor_lua_resolve_module("nope", roots).empty());
    std::filesystem::remove_all(dir);
}

TEST_CASE("template defines OnGameStart", "[kfx_editor][lua]") {
    CHECK(editor_lua_template().find("function OnGameStart()") != std::string::npos);
}

#include "editor_lua_stubs.h"

TEST_CASE("stub parser reads params, docs and returns", "[kfx_editor][lua]") {
    const auto fns = editor_lua_parse_stub_text(
        "---@meta\n\n---Does a thing.\n---More.\n---@param player Player the owner\n---@param level? integer\n---@return boolean ok\nfunction DoThing(player,level) end\n\n-- plain\nfunction NoDoc(a, b) end\nfunction Thing:method() end\n",
        "grp");
    REQUIRE(fns.size() == 3);
    CHECK(fns[0].name == "DoThing");
    CHECK(fns[0].group == "grp");
    CHECK(fns[0].doc == "Does a thing. More.");
    REQUIRE(fns[0].params.size() == 2);
    CHECK(fns[0].params[0].type == "Player");
    CHECK(fns[0].params[0].description == "the owner");
    CHECK(fns[0].params[1].optional);
    CHECK(fns[0].returns == "boolean ok");
    CHECK(editor_lua_signature(fns[0]) == "DoThing(player, level?)");
    CHECK(editor_lua_call_template(fns[0]) == "DoThing(player)");
    CHECK(fns[1].doc.empty());
    CHECK(fns[1].params.size() == 2);
    CHECK(editor_lua_call_template(fns[1]) == "NoDoc(a, b)");
}

TEST_CASE("stub catalog from the shipped fxdata has the known functions", "[kfx_editor][lua]") {
    const auto cat = editor_lua_load_stubs(std::string(KFX_SOURCE_DIR) + "/config/fxdata/lua");
    bool digger = false, timer = false;
    for (const auto &f : cat)
    {
        if (f.name == "SetDigger")
        {
            digger = true;
            CHECK(f.group == "players");
            CHECK(editor_lua_call_template(f) == "SetDigger(player, creature)");
            CHECK_FALSE(f.doc.empty());
        }
        if (f.name == "RegisterTimerEvent")
            timer = true;
        CHECK(f.name.find(':') == std::string::npos);
    }
    CHECK(digger);
    CHECK(timer);
    CHECK(cat.size() > 100);
}

TEST_CASE("event snippets are built from the Register*Event stubs", "[kfx_editor][lua]") {
    const auto cat = editor_lua_load_stubs(std::string(KFX_SOURCE_DIR) + "/config/fxdata/lua");
    const auto events = editor_lua_event_functions(cat);
    CHECK(events.size() >= 10);
    bool timer = false;
    for (const LuaFunctionDoc *f : events)
        if (f->name == "RegisterTimerEvent")
        {
            timer = true;
            const std::string s = editor_lua_event_snippet(*f);
            CHECK(s.find("function OnTimer(eventData, triggerData)") == 0);
            CHECK(s.find("RegisterTimerEvent(OnTimer, time, periodic)") != std::string::npos);
        }
    CHECK(timer);
}

TEST_CASE("module copy goes to the level folder, never overwrites, and writes atomically", "[kfx_editor][lua]") {
    const auto dir = std::filesystem::temp_directory_path() / "kfx_lua_copy_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "orig");
    std::ofstream(dir / "orig" / "m.lua", std::ios::binary) << "one\r\n";
    const std::string target = editor_lua_copy_target((dir / "lvl").string(), "pkg.m");
    CHECK(target == (dir / "lvl" / "pkg" / "m.lua").string());
    CHECK(editor_lua_copy_file((dir / "orig" / "m.lua").string(), target));
    CHECK(std::filesystem::exists(target));
    CHECK_FALSE(editor_lua_copy_file((dir / "orig" / "m.lua").string(), target)); // exists
    CHECK(editor_lua_write_file(target, std::string("two\0x", 5)));
    std::ifstream f(target, std::ios::binary);
    std::string got((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    CHECK(got == std::string("two\0x", 5));
    CHECK_FALSE(std::filesystem::exists(target + ".tmp"));
    CHECK(editor_lua_same_file(target, (dir / "lvl" / "pkg" / ".." / "pkg" / "m.lua").string()));
    CHECK_FALSE(editor_lua_same_file(target, (dir / "orig" / "m.lua").string()));
    std::filesystem::remove_all(dir);
}
