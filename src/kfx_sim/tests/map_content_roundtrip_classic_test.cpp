// kfx_sim coverage: ClassicMapContentWriter/Reader (map_content_writer.h/
// map_content_reader.h) -- see docs/refactor/editor/phase3/
// 01-slice2-classic-save.md. Mirrors map_content_roundtrip_test.cpp's own
// shape, but the sample content is deliberately *legacy-compatible*
// (map_is_legacy_compatible() would accept it) -- unlike the KFX-native
// sample, it doesn't set anything the classic .tng/.lgt/.apt formats have
// no field for at all (Orientation on Object/Creature/Trap, creature gold/
// health%/name, a gold pile's custom value): those fields are real,
// verified-by-reading-the-loader limitations of the classic format itself,
// not bugs in this writer, so a test built around them would only prove
// "the writer silently drops what it can't carry", not "round-trips
// correctly" -- map_content_compat_test.cpp is where that lossiness is
// actually asserted on.
#include <catch2/catch_test_macros.hpp>

#include "map_content.h"
#include "map_content_writer.h"
#include "map_content_reader.h"

#include <sys/stat.h>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

struct ScratchDir {
    char path[64];
    ScratchDir() {
        std::strcpy(path, "/tmp/kfx_map_content_classic_test_XXXXXX");
        REQUIRE(mkdtemp(path) != nullptr);
    }
};

MapContent build_legacy_compatible_content()
{
    MapContent c;
    c.map_tiles_x = 85;
    c.map_tiles_y = 85;
    c.slab_kind.assign(85 * 85, SlbT_ROCK);
    c.slab_owner.assign(85 * 85, 0);
    c.slab_kind[c.slab_index(10, 10)] = SlbT_CLAIMED;
    c.slab_owner[c.slab_index(10, 10)] = 1;
    c.texture_id = 1;
    // docs/refactor/editor/05-script-and-level-settings.md's "per-slab
    // texture paint" item -- .slx is format-independent (write_slab_
    // texture()'s own comment), so the classic path round-trips it too.
    c.slab_texture.assign(85 * 85, 0);
    c.slab_texture[c.slab_index(10, 10)] = 7;

    MapThingRecord creature;
    creature.thing_class = TCls_Creature;
    creature.model = 3;
    creature.owner = 1;
    creature.pos_x = 2560;
    creature.pos_y = 3072;
    creature.pos_z = 0;
    creature.creature_level = 3; // representable -- only orientation/gold/health%/name aren't
    c.things.push_back(creature);

    MapThingRecord herogate;
    herogate.thing_class = TCls_Object;
    herogate.model = 3;
    herogate.owner = 5;
    herogate.pos_x = 4096;
    herogate.pos_y = 4608;
    herogate.herogate_number = 1;
    c.things.push_back(herogate);

    MapThingRecord trap;
    trap.thing_class = TCls_Trap;
    trap.model = 2;
    trap.owner = 1;
    trap.pos_x = 1536;
    trap.pos_y = 1792;
    c.things.push_back(trap);

    MapThingRecord door;
    door.thing_class = TCls_Door;
    door.model = 1;
    door.owner = 1;
    door.pos_x = 2048;
    door.pos_y = 2304;
    door.door_orientation = 1; // representable for doors specifically
    door.door_locked = true;
    c.things.push_back(door);

    MapLightRecord l;
    l.is_dynamic = true;
    l.pos_x = 3000;
    l.pos_y = 3200;
    l.pos_z = 300;
    l.range = 2048;
    l.intensity = 30;
    l.parent_tile = 4;
    c.lights.push_back(l);

    MapActionPointRecord a;
    a.point_number = 1;
    a.pos_x = 1000;
    a.pos_y = 1100;
    a.range = 256;
    c.action_points.push_back(a);

    return c;
}

} // namespace

TEST_CASE_METHOD(ScratchDir, "ClassicMapContentWriter/Reader round-trips a legacy-compatible synthetic map", "[kfx_sim][map_content]") {
    MapContent original = build_legacy_compatible_content();

    ClassicMapContentWriter writer;
    REQUIRE(writer.write(original, path, 1));

    MapContent loaded;
    loaded.map_tiles_x = original.map_tiles_x;
    loaded.map_tiles_y = original.map_tiles_y;
    ClassicMapContentReader reader;
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
        CHECK(a.creature_level == b.creature_level);
        CHECK(a.herogate_number == b.herogate_number);
        CHECK(a.door_orientation == b.door_orientation);
        CHECK(a.door_locked == b.door_locked);
    }

    REQUIRE(loaded.lights.size() == original.lights.size());
    CHECK(loaded.lights[0].is_dynamic == original.lights[0].is_dynamic);
    CHECK(loaded.lights[0].pos_x == original.lights[0].pos_x);
    CHECK(loaded.lights[0].pos_y == original.lights[0].pos_y);
    CHECK(loaded.lights[0].pos_z == original.lights[0].pos_z);
    CHECK(loaded.lights[0].range == original.lights[0].range);
    CHECK(loaded.lights[0].intensity == original.lights[0].intensity);
    CHECK(loaded.lights[0].parent_tile == original.lights[0].parent_tile);

    REQUIRE(loaded.action_points.size() == original.action_points.size());
    CHECK(loaded.action_points[0].point_number == original.action_points[0].point_number);
    CHECK(loaded.action_points[0].pos_x == original.action_points[0].pos_x);
    CHECK(loaded.action_points[0].pos_y == original.action_points[0].pos_y);
    CHECK(loaded.action_points[0].range == original.action_points[0].range);
}
