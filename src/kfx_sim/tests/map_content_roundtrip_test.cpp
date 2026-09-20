// kfx_sim coverage: MapContentWriter/MapContentReader (map_content_writer.h/
// map_content_reader.h) -- the real regression net for
// docs/refactor/editor/phase3/00-slice1-native-save.md's KFX-native save
// format. Builds a synthetic MapContent by hand (no kfx_sim_state, no
// campaign/LevelInformation, no real game data needed -- the whole point
// of decoupling the writer/reader from global state), writes it with
// KfxNativeMapContentWriter to a scratch temp directory, reads it back
// with KfxNativeMapContentReader, and deep-equals the result against the
// original.
#include <catch2/catch_test_macros.hpp>

#include "map_content.h"
#include "map_content_writer.h"
#include "map_content_reader.h"

#include <sys/stat.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

// A fresh scratch directory per test case -- LbFileSaveAt/LbFileLoadAt
// need the directory to already exist, they don't create one.
struct ScratchDir {
    char path[64];
    ScratchDir() {
        std::strcpy(path, "/tmp/kfx_map_content_test_XXXXXX");
        REQUIRE(mkdtemp(path) != nullptr);
    }
};

MapContent build_sample_content()
{
    MapContent c;
    c.map_tiles_x = 4;
    c.map_tiles_y = 4;
    c.slab_kind.assign(16, SlbT_ROCK);
    c.slab_owner.assign(16, 0);
    // A couple of distinct slabs, so the grid isn't uniform.
    c.slab_kind[c.slab_index(1, 1)] = SlbT_CLAIMED;
    c.slab_owner[c.slab_index(1, 1)] = 2;
    c.slab_kind[c.slab_index(2, 2)] = SlbT_PATH;
    c.texture_id = 3;
    // docs/refactor/editor/05-script-and-level-settings.md's "per-slab
    // texture paint" item -- a couple of non-zero overrides, same "not a
    // uniform grid" shape as slab_kind/slab_owner above.
    c.slab_texture.assign(16, 0);
    c.slab_texture[c.slab_index(1, 1)] = 5;
    c.slab_texture[c.slab_index(3, 0)] = 12;

    MapThingRecord creature;
    creature.thing_class = TCls_Creature;
    creature.model = 7;
    creature.owner = 1;
    creature.pos_x = 1000;
    creature.pos_y = 2000;
    creature.pos_z = 0;
    creature.orientation = 512;
    creature.creature_level = 4;
    creature.creature_gold = 250;
    creature.creature_health_percent = 80;
    creature.creature_name = "Grubnak \"the Bold\"";
    c.things.push_back(creature);

    MapThingRecord herogate;
    herogate.thing_class = TCls_Object;
    herogate.model = 3;
    herogate.owner = 5;
    herogate.pos_x = 3000;
    herogate.pos_y = 4000;
    herogate.parent_tile = 12;
    herogate.herogate_number = 2;
    c.things.push_back(herogate);

    MapThingRecord goldpile;
    goldpile.thing_class = TCls_Object;
    goldpile.model = 9;
    goldpile.owner = 0;
    goldpile.pos_x = 500;
    goldpile.pos_y = 600;
    goldpile.gold_value = 1500;
    c.things.push_back(goldpile);

    MapThingRecord trap;
    trap.thing_class = TCls_Trap;
    trap.model = 1;
    trap.owner = 1;
    trap.pos_x = 700;
    trap.pos_y = 800;
    trap.orientation = 256;
    c.things.push_back(trap);

    MapThingRecord door;
    door.thing_class = TCls_Door;
    door.model = 2;
    door.owner = 1;
    door.pos_x = 900;
    door.pos_y = 950;
    door.door_orientation = 1;
    door.door_locked = true;
    c.things.push_back(door);

    MapLightRecord l1;
    l1.is_dynamic = true;
    l1.pos_x = 1200;
    l1.pos_y = 1300;
    l1.pos_z = 400;
    l1.range = 2560;
    l1.intensity = 45;
    l1.parent_tile = 7;
    c.lights.push_back(l1);

    MapLightRecord l2;
    l2.is_dynamic = false;
    l2.pos_x = 1800;
    l2.pos_y = 1900;
    l2.range = 1024;
    l2.intensity = 20;
    c.lights.push_back(l2);

    MapActionPointRecord a1;
    a1.point_number = 1;
    a1.pos_x = 500;
    a1.pos_y = 500;
    a1.range = 256;
    c.action_points.push_back(a1);

    MapActionPointRecord a2;
    a2.point_number = 2;
    a2.pos_x = 2500;
    a2.pos_y = 2600;
    a2.range = 512;
    c.action_points.push_back(a2);

    c.level_info.name_text = "Slice 1 Round-Trip";
    c.level_info.players = 3;
    c.level_info.is_multiplayer = true;
    c.level_info.description_text = "A test level";
    c.level_info.author_text = "Map Maker";

    // docs/refactor/editor/05-script-and-level-settings.md §0 -- a real
    // classic-format script snippet (comment + an IF/ENDIF block), not just
    // a placeholder string, to catch anything that would mangle newlines or
    // special characters a real map's script actually uses.
    c.script_text = "REM Slice 1 test script\nIF(PLAYER0,1)\n  WIN_GAME\nENDIF\n";

    return c;
}

} // namespace

TEST_CASE_METHOD(ScratchDir, "KfxNativeMapContentWriter/Reader round-trips a synthetic map exactly", "[kfx_sim][map_content]") {
    MapContent original = build_sample_content();

    KfxNativeMapContentWriter writer;
    REQUIRE(writer.write(original, path, 1));

    MapContent loaded;
    loaded.map_tiles_x = original.map_tiles_x;
    loaded.map_tiles_y = original.map_tiles_y;
    KfxNativeMapContentReader reader;
    REQUIRE(reader.read(loaded, path, 1));

    CHECK(loaded.slab_kind == original.slab_kind);
    CHECK(loaded.slab_owner == original.slab_owner);
    CHECK(loaded.texture_id == original.texture_id);
    CHECK(loaded.slab_texture == original.slab_texture);

    REQUIRE(loaded.things.size() == original.things.size());
    for (size_t i = 0; i < original.things.size(); i++) {
        const MapThingRecord &a = original.things[i];
        const MapThingRecord &b = loaded.things[i];
        CHECK(a.thing_class == b.thing_class);
        CHECK(a.model == b.model);
        CHECK(a.owner == b.owner);
        CHECK(a.pos_x == b.pos_x);
        CHECK(a.pos_y == b.pos_y);
        CHECK(a.pos_z == b.pos_z);
        CHECK(a.orientation == b.orientation);
        CHECK(a.parent_tile == b.parent_tile);
        CHECK(a.creature_level == b.creature_level);
        CHECK(a.creature_gold == b.creature_gold);
        CHECK(a.creature_health_percent == b.creature_health_percent);
        CHECK(a.creature_name == b.creature_name);
        CHECK(a.herogate_number == b.herogate_number);
        CHECK(a.gold_value == b.gold_value);
        CHECK(a.door_orientation == b.door_orientation);
        CHECK(a.door_locked == b.door_locked);
    }

    REQUIRE(loaded.lights.size() == original.lights.size());
    for (size_t i = 0; i < original.lights.size(); i++) {
        const MapLightRecord &a = original.lights[i];
        const MapLightRecord &b = loaded.lights[i];
        CHECK(a.is_dynamic == b.is_dynamic);
        CHECK(a.pos_x == b.pos_x);
        CHECK(a.pos_y == b.pos_y);
        CHECK(a.pos_z == b.pos_z);
        CHECK(a.range == b.range);
        CHECK(a.intensity == b.intensity);
        CHECK(a.parent_tile == b.parent_tile);
    }

    REQUIRE(loaded.action_points.size() == original.action_points.size());
    for (size_t i = 0; i < original.action_points.size(); i++) {
        const MapActionPointRecord &a = original.action_points[i];
        const MapActionPointRecord &b = loaded.action_points[i];
        CHECK(a.point_number == b.point_number);
        CHECK(a.pos_x == b.pos_x);
        CHECK(a.pos_y == b.pos_y);
        CHECK(a.range == b.range);
    }

    CHECK(loaded.level_info.name_text == original.level_info.name_text);
    CHECK(loaded.level_info.players == original.level_info.players);
    CHECK(loaded.level_info.is_multiplayer == original.level_info.is_multiplayer);
    CHECK(loaded.level_info.description_text == original.level_info.description_text);
    CHECK(loaded.level_info.author_text == original.level_info.author_text);

    CHECK(loaded.script_text == original.script_text);
}

namespace {
// .lif is write-only from MapContentWriter's own side -- MapContentReader
// never reads it back into a MapContent (see map_content_writer.h's own
// write_lif() comment on why: a discovery-registry sidecar, not map
// content) -- so this reads the raw file directly rather than round-
// tripping through the reader like the test above does for .lof.
std::string read_whole_file(const std::string &path)
{
    FILE *f = fopen(path.c_str(), "rb");
    if (f == nullptr)
        return std::string();
    std::string out;
    char buf[256];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        out.append(buf, n);
    fclose(f);
    return out;
}
} // namespace

TEST_CASE_METHOD(ScratchDir, "KfxNativeMapContentWriter writes a per-level .lif matching real shipped content's own format", "[kfx_sim][map_content]") {
    MapContent content = build_sample_content();
    REQUIRE(!content.level_info.name_text.empty()); // build_sample_content() always sets one

    KfxNativeMapContentWriter writer;
    REQUIRE(writer.write(content, path, 42));

    std::string lif_path = std::string(path) + "/map00042.lif";
    std::string contents = read_whole_file(lif_path);
    std::string expected = "42, " + content.level_info.name_text + "\r\n";
    CHECK(contents == expected);
}

TEST_CASE_METHOD(ScratchDir, "KfxNativeMapContentWriter skips .lif entirely for an unnamed level", "[kfx_sim][map_content]") {
    MapContent content = build_sample_content();
    content.level_info.name_text.clear();

    KfxNativeMapContentWriter writer;
    REQUIRE(writer.write(content, path, 43));

    std::string lif_path = std::string(path) + "/map00043.lif";
    struct stat st;
    CHECK(stat(lif_path.c_str(), &st) != 0); // ENOENT -- file was never created
}

// docs/refactor/editor/05-script-and-level-settings.md §0 -- the actual
// regression this slice exists to prevent: write_script() used to always
// overwrite a real script with an empty stub. build_sample_content()'s own
// script_text is already covered by the round-trip test above; these three
// cover the boundary cases that test doesn't reach.
TEST_CASE_METHOD(ScratchDir, "KfxNativeMapContentWriter falls back to the empty-script stub only when script_text is empty", "[kfx_sim][map_content]") {
    MapContent content = build_sample_content();
    content.script_text.clear();

    KfxNativeMapContentWriter writer;
    REQUIRE(writer.write(content, path, 44));

    std::string txt_path = std::string(path) + "/map00044.txt";
    CHECK(read_whole_file(txt_path) == "REM Empty script, generated by the in-game editor.\n");
}

TEST_CASE_METHOD(ScratchDir, "KfxNativeMapContentReader leaves script_text empty (not a read failure) when .txt is missing", "[kfx_sim][map_content]") {
    MapContent loaded;
    loaded.map_tiles_x = 1;
    loaded.map_tiles_y = 1;
    loaded.slab_kind.assign(1, SlbT_ROCK);
    loaded.slab_owner.assign(1, 0);
    KfxNativeMapContentReader reader;
    // read() aggregates every sub-reader's success; things/lights/APs are
    // TOML files that don't exist in this bare scratch dir either, so this
    // whole call is expected to report false overall -- what matters here
    // is specifically that a missing .txt didn't corrupt script_text into
    // something other than empty.
    reader.read(loaded, path, 99);
    CHECK(loaded.script_text.empty());
}

// fx-plans/02-lua-scripts.md L1 -- the .lua is an opaque byte blob that both
// formats carry, and a Lua-only level never gets an invented .txt.
TEST_CASE_METHOD(ScratchDir, "map .lua round-trips byte-identically in both formats", "[kfx_sim][map_content]") {
    std::string lua = "\xEF\xBB\xBF-- caf\xC3\xA9\r\nfunction OnGameStart()\r\nend\r\n";
    lua.append(1 << 20, 'x'); // 1 MB
    for (int fmt = 0; fmt < 2; fmt++)
    {
        MapContent content = build_sample_content();
        content.lua_text = lua;
        content.has_lua = true;
        KfxNativeMapContentWriter native;
        ClassicMapContentWriter classic;
        MapContentWriter &w = (fmt == 0) ? (MapContentWriter &)native : (MapContentWriter &)classic;
        REQUIRE(w.write(content, path, 46));
        CHECK(read_whole_file(std::string(path) + "/map00046.lua") == lua);

        MapContent loaded;
        loaded.map_tiles_x = content.map_tiles_x;
        loaded.map_tiles_y = content.map_tiles_y;
        KfxNativeMapContentReader reader;
        reader.read(loaded, path, 46);
        CHECK(loaded.has_lua);
        CHECK(loaded.lua_text == lua);
    }
}

TEST_CASE_METHOD(ScratchDir, "an empty .lua is kept and a missing one is not invented or left stale", "[kfx_sim][map_content]") {
    struct stat st;
    std::string lua_path = std::string(path) + "/map00047.lua";
    KfxNativeMapContentWriter writer;

    MapContent content = build_sample_content();
    content.has_lua = true; // empty file
    REQUIRE(writer.write(content, path, 47));
    REQUIRE(stat(lua_path.c_str(), &st) == 0);
    CHECK(st.st_size == 0);
    MapContent loaded;
    loaded.map_tiles_x = content.map_tiles_x;
    loaded.map_tiles_y = content.map_tiles_y;
    KfxNativeMapContentReader reader;
    reader.read(loaded, path, 47);
    CHECK(loaded.has_lua);
    CHECK(loaded.lua_text.empty());

    content.has_lua = false; // stale file removed
    REQUIRE(writer.write(content, path, 47));
    CHECK(stat(lua_path.c_str(), &st) != 0);
    reader.read(loaded, path, 47);
    CHECK_FALSE(loaded.has_lua);
}

TEST_CASE_METHOD(ScratchDir, "a Lua-only level is not given a stub .txt", "[kfx_sim][map_content]") {
    MapContent content = build_sample_content();
    content.script_text.clear();
    content.lua_text = "function OnGameStart() end\n";
    content.has_lua = true;
    KfxNativeMapContentWriter writer;
    REQUIRE(writer.write(content, path, 48));
    struct stat st;
    CHECK(stat((std::string(path) + "/map00048.txt").c_str(), &st) != 0);
    CHECK(stat((std::string(path) + "/map00048.lua").c_str(), &st) == 0);
}

// docs/refactor/editor/05-script-and-level-settings.md's "per-slab texture
// paint" item -- slab_texture's own boundary cases, same shape as
// script_text's pair above: a missing .slx means "no overrides", not a
// read failure, and a writer with an all-zero grid (a map nobody's painted
// yet) still writes a real .slx (unlike script_text's own empty-means-
// skip-and-stub convention -- see write_slab_texture()'s own comment for
// why this one's always written).
TEST_CASE_METHOD(ScratchDir, "KfxNativeMapContentReader leaves slab_texture all-zero (not a read failure) when .slx is missing", "[kfx_sim][map_content]") {
    MapContent loaded;
    loaded.map_tiles_x = 2;
    loaded.map_tiles_y = 2;
    loaded.slab_kind.assign(4, SlbT_ROCK);
    loaded.slab_owner.assign(4, 0);
    KfxNativeMapContentReader reader;
    reader.read(loaded, path, 100); // overall false expected, same reasoning as script_text's own test above
    REQUIRE(loaded.slab_texture.size() == 4);
    for (unsigned char v : loaded.slab_texture)
        CHECK(v == 0);
}

TEST_CASE_METHOD(ScratchDir, "KfxNativeMapContentWriter always writes .slx, even an all-zero grid", "[kfx_sim][map_content]") {
    MapContent content = build_sample_content();
    content.slab_texture.assign(16, 0);

    KfxNativeMapContentWriter writer;
    REQUIRE(writer.write(content, path, 45));

    std::string slx_path = std::string(path) + "/map00045.slx";
    struct stat st;
    CHECK(stat(slx_path.c_str(), &st) == 0); // file exists, unlike write_script()'s own skip-when-empty case
    CHECK(read_whole_file(slx_path).size() == 16);
}

// A save in one format must not leave the other format's files behind: the
// loader prefers .tngfx/.lgtfx/.aptfx and any existing .clm/.dat/.wib, so a
// stale copy would silently replace what was just saved.
TEST_CASE_METHOD(ScratchDir, "saving in one format removes the other format's files", "[kfx_sim][map_content]") {
    MapContent content = build_sample_content();
    KfxNativeMapContentWriter native;
    ClassicMapContentWriter classic;
    std::string dir = path;
    auto exists = [&](const char *ext) {
        struct stat st;
        return stat((dir + "/map00042." + ext).c_str(), &st) == 0;
    };
    REQUIRE(native.write(content, path, 42));
    REQUIRE(exists("tngfx"));
    content.derived_dat.assign(8, 1);
    REQUIRE(classic.write(content, path, 42));
    CHECK_FALSE(exists("tngfx"));
    CHECK_FALSE(exists("lgtfx"));
    CHECK_FALSE(exists("aptfx"));
    CHECK(exists("tng"));
    CHECK(exists("dat"));
    REQUIRE(native.write(content, path, 42));
    CHECK(exists("tngfx"));
    CHECK_FALSE(exists("tng"));
    CHECK_FALSE(exists("dat"));
}

// The loader takes the map size from the .lof, so the writer must put it there.
TEST_CASE_METHOD(ScratchDir, "the .lof carries MAPSIZE", "[kfx_sim][map_content]") {
    MapContent content = build_sample_content();
    KfxNativeMapContentWriter writer;
    REQUIRE(writer.write(content, path, 5));
    std::string lof = std::string(path) + "/map00005.lof";
    FILE *f = fopen(lof.c_str(), "rb");
    REQUIRE(f != nullptr);
    char buf[1024] = {};
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    CHECK(std::string(buf, n).find("MAPSIZE = 4 4") != std::string::npos);
}
