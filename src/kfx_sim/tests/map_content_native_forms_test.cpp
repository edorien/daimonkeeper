// The native map readers (map_content_reader.cpp) must accept every layout the game's own loader does
// (lvl_filesdk1.c load_kfx_toml_file + thing_factory.c): lists as an array of tables ("[[thing]]", what the
// editor writes) OR numbered tables ("[thing0]", "[thing1]" ... with "[common] ThingsCount"), and ThingType as
// an integer or a class name ("Object", "Creature"). Hand-authored maps such as the dk2maps pack use the
// numbered/name forms; the reader used to see none of their things.
#include <catch2/catch_test_macros.hpp>

#include "map_content.h"
#include "map_content_reader.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

struct Exposed : public KfxNativeMapContentReader
{
    using KfxNativeMapContentReader::read_action_points;
    using KfxNativeMapContentReader::read_lights;
    using KfxNativeMapContentReader::read_things;
};

struct ScratchDir {
    char path[64];
    ScratchDir()
    {
        std::strcpy(path, "/tmp/kfx_native_forms_XXXXXX");
        REQUIRE(mkdtemp(path) != nullptr);
    }
    ~ScratchDir()
    {
        std::string cmd = std::string("rm -rf ") + path;
        if (std::system(cmd.c_str()) != 0)
            std::fprintf(stderr, "could not remove %s\n", path);
    }
    void write(const char *name, const std::string &text) const
    {
        const std::string p = std::string(path) + "/" + name;
        FILE *f = std::fopen(p.c_str(), "wb");
        REQUIRE(f != nullptr);
        std::fwrite(text.data(), 1, text.size(), f);
        std::fclose(f);
    }
};

} // namespace

TEST_CASE("native things: numbered tables with class names (the hand-authored dk2maps layout)", "[kfx_sim][map_content]") {
    ScratchDir dir;
    dir.write("map09941.tngfx",
        "[common]\nThingsCount = 3\n\n"
        "[thing0]\nThingType = \"Object\"\nSubtype = 5\nOwnership = 0\nSubtileX = [40, 128]\nSubtileY = [55, 128]\nSubtileZ = [0, 0]\n\n"
        "[thing1]\nThingType = \"Object\"\nSubtype = 5\nOwnership = 1\nSubtileX = [80, 128]\nSubtileY = [55, 128]\nSubtileZ = [0, 0]\n\n"
        "[thing2]\nThingType = \"Creature\"\nSubtype = 7\nOwnership = 1\nSubtileX = [10, 0]\nSubtileY = [12, 0]\nSubtileZ = [0, 0]\n");
    MapContent content;
    REQUIRE(Exposed().read_things(content, dir.path, 9941));
    REQUIRE(content.things.size() == 3);
    CHECK(content.things[0].thing_class == TCls_Object);
    CHECK(content.things[0].model == 5);
    CHECK(content.things[0].owner == 0);
    CHECK(content.things[1].thing_class == TCls_Object);
    CHECK(content.things[1].owner == 1);
    CHECK(content.things[2].thing_class == TCls_Creature);
    CHECK(content.things[2].model == 7);
}

TEST_CASE("native things: the editor's array layout with integer classes still reads", "[kfx_sim][map_content]") {
    ScratchDir dir;
    dir.write("map09942.tngfx",
        "[common]\nThingsCount = 2\n\n"
        "[[thing]]\nThingType = 1\nSubtype = 5\nOwnership = 2\nSubtileX = [40, 128]\nSubtileY = [55, 128]\nSubtileZ = [0, 0]\n\n"
        "[[thing]]\nThingType = 5\nSubtype = 3\nOwnership = 0\nSubtileX = [1, 0]\nSubtileY = [2, 0]\nSubtileZ = [0, 0]\n");
    MapContent content;
    REQUIRE(Exposed().read_things(content, dir.path, 9942));
    REQUIRE(content.things.size() == 2);
    CHECK(content.things[0].thing_class == 1);
    CHECK(content.things[0].owner == 2);
    CHECK(content.things[1].thing_class == 5);
    CHECK(content.things[1].model == 3);
}

TEST_CASE("native lights and action points also read in the numbered layout", "[kfx_sim][map_content]") {
    ScratchDir dir;
    dir.write("map09943.lgtfx",
        "[common]\nLightsCount = 1\n\n"
        "[light0]\nSubtileX = [30, 128]\nSubtileY = [31, 128]\nSubtileZ = [2, 0]\nRadius = 5\nIntensity = 10\n");
    dir.write("map09943.aptfx",
        "[common]\nActionPointsCount = 2\n\n"
        "[actionpoint0]\nNumber = 1\nSubtileX = [20, 0]\nSubtileY = [21, 0]\nRange = 3\n\n"
        "[actionpoint1]\nNumber = 2\nSubtileX = [40, 0]\nSubtileY = [41, 0]\nRange = 4\n");
    MapContent content;
    REQUIRE(Exposed().read_lights(content, dir.path, 9943));
    CHECK(content.lights.size() == 1);
    REQUIRE(Exposed().read_action_points(content, dir.path, 9943));
    CHECK(content.action_points.size() == 2);
}
