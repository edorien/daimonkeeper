// kfx_editor: editor_kfx_compat.cpp -- the "Force KeeperFX" save's check that a
// map loads in the KeeperFX release this game claims compatibility with.
// Sample names come from the generated reference itself (not hard-coded), so
// these keep working when it's regenerated against a later release; a list
// that's empty there just skips its cases.
#include <catch2/catch_test_macros.hpp>

#include "editor_kfx_compat.h"
#include "map_content.h"

#include <cctype>
#include <string>

#include "../src/kfx_compat_reference.inc"

namespace {
// The base game's kind names, as far as these tests care: whatever the reference
// says is fork-only is still the base kind at that model number.
std::string base_names(const char *kind, int64_t model)
{
    for (const KfxRefForkOnlyKind *k = kfx_ref_forkonly_kinds; k->kind != nullptr; k++)
        if (std::string(k->kind) == kind && k->index == model)
            return k->name;
    return "SOMETHING_KEEPERFX_HAS";
}

bool mentions(const std::vector<std::string> &problems, const std::string &text)
{
    for (const std::string &p : problems)
        if (p.find(text) != std::string::npos)
            return true;
    return false;
}

MapContent plain_map(void)
{
    MapContent c;
    c.map_tiles_x = 2;
    c.map_tiles_y = 2;
    c.slab_kind.assign(4, 0);
    c.script_text = "LEVEL_VERSION(1)\nSTART_MONEY(PLAYER0,2500)\n";
    return c;
}
}

TEST_CASE("a map using only what KeeperFX has passes", "[kfx_editor][kfx_compat]") {
    CHECK(editor_kfx_compat_problems(plain_map(), base_names).empty());
    CHECK(std::string(editor_kfx_compat_reference_label()).rfind("KeeperFX ", 0) == 0);
}

TEST_CASE("fork-only level script commands are refused, with their line", "[kfx_editor][kfx_compat]") {
    const char *cmd = kfx_ref_forkonly_script_commands[0];
    if (cmd == nullptr)
        SKIP("the reference release has every script command this game has");
    MapContent c = plain_map();
    std::string lower = cmd;
    for (char &ch : lower)
        ch = (char)std::tolower((unsigned char)ch);
    c.script_text = "LEVEL_VERSION(1)\n"
                    "REM " + std::string(cmd) + "(1) -- a comment doesn't count\n"
                    "QUICK_MESSAGE(0,\"" + std::string(cmd) + " in a string doesn't count\",PLAYER0)\n"
                    + lower + "(PLAYER0,1)\n";
    const auto problems = editor_kfx_compat_problems(c, base_names);
    REQUIRE(problems.size() == 1);
    CHECK(problems[0] == "script line 4: " + std::string(cmd));
}

TEST_CASE("fork-only Lua API calls are refused; comments, strings and the map's own functions aren't", "[kfx_editor][kfx_compat]") {
    const char *fn = kfx_ref_forkonly_lua_functions[0];
    if (fn == nullptr)
        SKIP("the reference release has every Lua function this game has");
    const std::string name = fn;
    MapContent c = plain_map();
    c.has_lua = true;
    c.lua_text = "-- " + name + "(1)\n"
                 "local s = \"" + name + "(2)\"\n"
                 "--[[ " + name + "(3) ]]\n"
                 "thing:" + name + " (4)\n";
    auto problems = editor_kfx_compat_problems(c, base_names);
    REQUIRE(problems.size() == 1);
    CHECK(problems[0] == "Lua line 4: " + name + "()");

    // The same name defined by the map's own Lua is its own function, not the API.
    c.lua_text = "function " + name + "(x) return x end\n" + name + "(5)\n";
    CHECK(editor_kfx_compat_problems(c, base_names).empty());

    // Lua is only checked when the map has any.
    c.lua_text = name + "(6)\n";
    c.has_lua = false;
    CHECK(editor_kfx_compat_problems(c, base_names).empty());
}

TEST_CASE("base-game kinds KeeperFX doesn't have are refused, campaign-defined ones aren't", "[kfx_editor][kfx_compat]") {
    const KfxRefForkOnlyKind *k = &kfx_ref_forkonly_kinds[0];
    if (k->kind == nullptr)
        SKIP("the reference release has every base-game kind this game has");
    MapContent c = plain_map();
    if (std::string(k->kind) == "slab")
    {
        c.slab_kind[1] = (SlabKind)k->index;
        c.slab_kind[2] = (SlabKind)k->index;
    }
    else
    {
        MapThingRecord t;
        t.thing_class = std::string(k->kind) == "object" ? TCls_Object
                      : std::string(k->kind) == "trap" ? TCls_Trap
                      : std::string(k->kind) == "door" ? TCls_Door
                      : std::string(k->kind) == "creature" ? TCls_Creature : TCls_EffectGen;
        t.model = (ThingModel)k->index;
        c.things.push_back(t);
        c.things.push_back(t);
    }
    auto problems = editor_kfx_compat_problems(c, base_names);
    REQUIRE(problems.size() == 1);
    CHECK(mentions(problems, std::string(k->kind) + " " + k->name + " (model " + std::to_string(k->index) + "), used 2 times"));

    // The level's campaign redefines that model number: its own config ships with the map.
    auto campaign_names = [](const char *, int64_t) { return std::string("CAMPAIGN_OWN_KIND"); };
    CHECK(editor_kfx_compat_problems(c, campaign_names).empty());
}

TEST_CASE("the last refusal is kept for the save dialog", "[kfx_editor][kfx_compat]") {
    editor_set_last_save_compat_problems({"script line 1: X"});
    REQUIRE(editor_last_save_compat_problems().size() == 1);
    editor_set_last_save_compat_problems({});
    CHECK(editor_last_save_compat_problems().empty());
}

#include "config_creature.h"
#include "config_effects.h"
#include "config_objects.h"
#include "config_terrain.h"
#include "config_trapdoor.h"
#include "kfx_config_state.h"

#include <cstring>

TEST_CASE("the generated reference matches the shipped base data", "[kfx_editor][kfx_compat]") {
    // Each kind the reference calls fork-only must be what this game's own
    // config/fxdata defines at that model number -- else the .inc is stale:
    // re-run scripts/gen_kfx_compat_reference.py.
    for (const KfxRefForkOnlyKind *k = kfx_ref_forkonly_kinds; k->kind != nullptr; k++)
    {
        INFO(std::string(k->kind) + " " + std::to_string(k->index) + " " + k->name);
        std::memset(&kfx_config_state, 0, sizeof(kfx_config_state));
        const std::string kind = k->kind;
        const struct ConfigFileData *file = (kind == "slab") ? &keeper_terrain_file_data
            : (kind == "object") ? &keeper_objects_file_data
            : (kind == "trap" || kind == "door") ? &keeper_trapdoor_file_data
            : (kind == "creature") ? &keeper_creaturetp_file_data : &keeper_effects_file_data;
        const std::string path = std::string(KFX_EDITOR_TEST_REPO_ROOT "/config/fxdata/") + file->filename;
        REQUIRE(file->load_func(path.c_str(), 0));
        const std::string loaded = (kind == "slab") ? slab_code_name((SlabKind)k->index)
            : (kind == "object") ? object_code_name((ThingModel)k->index)
            : (kind == "trap") ? trap_code_name(k->index)
            : (kind == "door") ? door_code_name(k->index)
            : (kind == "creature") ? creature_code_name((ThingModel)k->index)
            : effectgenerator_code_name((ThingModel)k->index);
        CHECK(loaded == k->name);
    }
}
