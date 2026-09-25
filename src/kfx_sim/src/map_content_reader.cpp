/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file map_content_reader.cpp
 *     See map_content_reader.h.
 * @par Comment:
 *     None.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "map_content_reader.h"

#include "bflib_dernc.h"
#include "bflib_fileio.h"
// deps/centitoml/toml.h declares toml_parse() with no extern "C" guard of
// its own (unlike deps/centijson/include/value.h, which does) -- every
// existing include site is a plain .c file, where this is a non-issue.
// This is the first .cpp in the codebase to actually call toml_parse()
// (rather than just the already-guarded value_*() accessors), so the
// include needs an explicit extern "C" wrapper here to get C linkage
// matching toml_api.c's own compiled definition; without it the link step
// looks for a C++-mangled symbol that doesn't exist.
extern "C" {
#include "value_util.h"
}

#include <cstdio>
#include <cstring>
#include <vector>
#include <string>

#include "post_inc.h"

namespace {

std::string build_path(const char *dir, LevelNumber lvnum, const char *ext)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "/map%05" PRIu64 ".%s", (uint64_t)lvnum, ext);
    return std::string(dir) + buf;
}

// Loads a whole file into a NUL-terminated buffer (one extra byte over the
// file's own length) -- toml_parse() takes a plain C string, and this also
// works unmodified for the raw .slb/.own/.inf binary readers below (they
// only ever index within the real file length, never read the trailing
// NUL as data).
bool load_whole_file(const std::string &path, std::vector<char> &out)
{
    int64_t len = LbFileLength(path.c_str());
    if (len < 0)
        return false;
    out.resize((size_t)len + 1);
    if (len > 0)
    {
        int64_t got = LbFileLoadAt(path.c_str(), out.data());
        if (got != len)
            return false;
    }
    out[(size_t)len] = '\0';
    return true;
}

// The inverse of value_read_stl_coord() (src/kfx_config/include/
// value_util.h) -- shared here rather than only in the writer's
// append_stl_coord(), since the reader needs the same [whole,sub] pairing
// convention to interpret what it parses.
MapCoord read_stl_coord(VALUE *dict, const char *key)
{
    return value_read_stl_coord(value_dict_get(dict, key));
}

int64_t read_int_default(VALUE *dict, const char *key, int64_t def)
{
    VALUE *v = value_dict_get(dict, key);
    if ((v == NULL) || (value_type(v) != VALUE_INT32))
        return def;
    return value_int32(v);
}

// The game's own loader (lvl_filesdk1.c load_kfx_toml_file) accepts two layouts for these lists: an
// array of tables (`[[thing]]`, what the editor writes) and numbered tables (`[thing0]`, `[thing1]`, ...
// with `[common] ThingsCount = N`, what hand-authored maps such as the dk2maps pack use). The reader used
// to know only the first, so such maps opened with no things/lights/action points at all.
size_t native_record_count(VALUE *root, const char *array_name, const char *count_field)
{
    VALUE *arr = value_dict_get(root, array_name);
    if (value_type(arr) == VALUE_ARRAY)
        return value_array_size(arr);
    const int64_t n = value_int32(value_dict_get(value_dict_get(root, "common"), count_field));
    return (n > 0) ? (size_t)n : 0;
}

VALUE *native_record_at(VALUE *root, const char *array_name, const char *numbered_fmt, size_t k)
{
    VALUE *arr = value_dict_get(root, array_name);
    if (value_type(arr) == VALUE_ARRAY)
        return value_array_get(arr, k);
    char key[64];
    snprintf(key, sizeof(key), numbered_fmt, (int64_t)k);
    return value_dict_get(root, key);
}

} // namespace

bool MapContentReader::read(MapContent &content, const char *dir, LevelNumber lvnum)
{
    bool result = true;
    if (!read_slabs(content, dir, lvnum)) result = false;
    if (!read_ownership(content, dir, lvnum)) result = false;
    if (!read_texture(content, dir, lvnum)) result = false;
    if (!read_slab_texture(content, dir, lvnum)) result = false;
    if (!read_script(content, dir, lvnum)) result = false;
    if (!read_lua(content, dir, lvnum)) result = false;
    if (!read_things(content, dir, lvnum)) result = false;
    if (!read_lights(content, dir, lvnum)) result = false;
    if (!read_action_points(content, dir, lvnum)) result = false;
    if (!read_level_info(content, dir, lvnum)) result = false;
    return result;
}

bool MapContentReader::read_slabs(MapContent &content, const char *dir, LevelNumber lvnum)
{
    std::vector<char> buf;
    if (!load_whole_file(build_path(dir, lvnum, "slb"), buf))
        return false;
    size_t needed = (size_t)(content.map_tiles_x * content.map_tiles_y) * 2;
    if (buf.size() < needed + 1) // +1 for load_whole_file's trailing NUL
        return false;
    content.slab_kind.assign((size_t)(content.map_tiles_x * content.map_tiles_y), 0);
    size_t i = 0;
    for (int64_t y = 0; y < content.map_tiles_y; y++)
    {
        for (int64_t x = 0; x < content.map_tiles_x; x++)
        {
            uint64_t n = (unsigned char)buf[i] | ((unsigned char)buf[i + 1] << 8);
            content.slab_kind[content.slab_index(x, y)] = (SlabKind)n;
            i += 2;
        }
    }
    return true;
}

bool MapContentReader::read_ownership(MapContent &content, const char *dir, LevelNumber lvnum)
{
    std::vector<char> buf;
    if (!load_whole_file(build_path(dir, lvnum, "own"), buf))
        return false;
    int64_t subtiles_x = content.map_tiles_x * STL_PER_SLB;
    int64_t subtiles_y = content.map_tiles_y * STL_PER_SLB;
    size_t needed = (size_t)((subtiles_y + 1) * (subtiles_x + 1));
    if (buf.size() < needed + 1)
        return false;
    content.slab_owner.assign((size_t)(content.map_tiles_x * content.map_tiles_y), 0);
    int64_t row_stride = subtiles_x + 1;
    for (int64_t y = 0; y < content.map_tiles_y; y++)
    {
        for (int64_t x = 0; x < content.map_tiles_x; x++)
        {
            // Every subtile of a slab shares that slab's owner byte
            // (write_ownership()'s own fill pattern) -- the slab's
            // top-left subtile is as representative as any other.
            size_t i = (size_t)(y * STL_PER_SLB * row_stride + x * STL_PER_SLB);
            content.slab_owner[content.slab_index(x, y)] = (PlayerNumber)(unsigned char)buf[i];
        }
    }
    return true;
}

bool MapContentReader::read_texture(MapContent &content, const char *dir, LevelNumber lvnum)
{
    std::vector<char> buf;
    if (!load_whole_file(build_path(dir, lvnum, "inf"), buf))
        return false;
    if (buf.size() < 2) // +1 trailing NUL over the real 1-byte payload
        return false;
    content.texture_id = (unsigned char)buf[0];
    return true;
}

bool MapContentReader::read_slab_texture(MapContent &content, const char *dir, LevelNumber lvnum)
{
    content.slab_texture.assign((size_t)(content.map_tiles_x * content.map_tiles_y), 0);
    std::vector<char> buf;
    if (!load_whole_file(build_path(dir, lvnum, "slx"), buf))
        return true; // no .slx on disk -- no per-slab overrides, not a read failure
    size_t needed = (size_t)(content.map_tiles_x * content.map_tiles_y);
    if (buf.size() < needed + 1) // malformed/short -- same guard load_ext_slabs() itself uses live
        return true;
    for (size_t i = 0; i < needed; i++)
        content.slab_texture[i] = (unsigned char)buf[i];
    return true;
}

bool MapContentReader::read_script(MapContent &content, const char *dir, LevelNumber lvnum)
{
    std::vector<char> buf;
    if (!load_whole_file(build_path(dir, lvnum, "txt"), buf))
    {
        content.script_text.clear();
        return true; // no .txt on disk -- nothing to preserve, not a read failure
    }
    content.script_text.assign(buf.data(), buf.size() - 1); // drop load_whole_file's trailing NUL
    return true;
}

bool MapContentReader::read_lua(MapContent &content, const char *dir, LevelNumber lvnum)
{
    std::vector<char> buf;
    if (!load_whole_file(build_path(dir, lvnum, "lua"), buf) || buf.empty())
    {
        content.lua_text.clear();
        content.has_lua = false;
        return true; // no .lua on disk -- not a read failure
    }
    content.lua_text.assign(buf.data(), buf.size() - 1); // drop load_whole_file's trailing NUL
    content.has_lua = true;
    return true;
}

bool KfxNativeMapContentReader::read_things(MapContent &content, const char *dir, LevelNumber lvnum)
{
    std::vector<char> buf;
    if (!load_whole_file(build_path(dir, lvnum, "tngfx"), buf))
        return false;

    VALUE root;
    char err[255] = "";
    if (toml_parse(buf.data(), err, sizeof(err), &root))
        return false;

    const size_t count = native_record_count(&root, "thing", "ThingsCount");
    content.things.clear();
    content.things.reserve(count);
    for (size_t k = 0; k < count; k++)
    {
        VALUE *d = native_record_at(&root, "thing", "thing%" PRId64, (int64_t)(k));
        if (value_type(d) != VALUE_DICT)
            continue;
        MapThingRecord t;
        // Same forms the game's own loader accepts (thing_factory.c thing_create_thing_adv): ThingType as an
        // integer or a class name ("Object", "Creature", ...), and the model as `Subtype` (integer) or
        // `SubtypeStringID` (a name). Hand-authored maps (e.g. the dk2maps pack) use the name forms; reading
        // only integers turned every one of their things into class 0.
        const int64_t cls = value_parse_class(value_dict_get(d, "ThingType"));
        t.thing_class = (ThingClass)((cls >= 0) ? cls : 0);
        int64_t model = -1;
        VALUE *subtype_name = value_dict_get(d, "SubtypeStringID");
        if ((cls >= 0) && (subtype_name != NULL) && (value_type(subtype_name) == VALUE_STRING))
            model = value_parse_model(cls, subtype_name);
        t.model = (ThingModel)((model >= 0) ? model : read_int_default(d, "Subtype", 0));
        t.owner = (PlayerNumber)read_int_default(d, "Ownership", 0);
        t.pos_x = read_stl_coord(d, "SubtileX");
        t.pos_y = read_stl_coord(d, "SubtileY");
        t.pos_z = read_stl_coord(d, "SubtileZ");
        t.parent_tile = read_int_default(d, "ParentTile", -1);
        t.orientation = read_int_default(d, "Orientation", 0);
        if (t.thing_class == TCls_Creature)
        {
            t.creature_level = (int64_t)read_int_default(d, "CreatureLevel", 1) - 1;
            t.creature_gold = read_int_default(d, "CreatureGold", 0);
            t.creature_health_percent = (int64_t)read_int_default(d, "CreatureInitialHealth", 0);
            VALUE *name = value_dict_get(d, "CreatureName");
            if ((name != NULL) && (value_type(name) == VALUE_STRING))
                t.creature_name = value_string(name);
        }
        if (t.thing_class == TCls_Object)
        {
            t.herogate_number = read_int_default(d, "HerogateNumber", 0);
            t.custom_box_kind = read_int_default(d, "CustomBox", 0);
            t.gold_value = read_int_default(d, "GoldValue", 0);
        }
        if (t.thing_class == TCls_EffectGen)
            t.effect_range = read_stl_coord(d, "EffectRange");
        if (t.thing_class == TCls_Door)
        {
            t.door_orientation = read_int_default(d, "DoorOrientation", 0);
            t.door_locked = read_int_default(d, "DoorLocked", 0) != 0;
        }
        content.things.push_back(t);
    }
    value_fini(&root);
    return true;
}

bool KfxNativeMapContentReader::read_lights(MapContent &content, const char *dir, LevelNumber lvnum)
{
    std::vector<char> buf;
    if (!load_whole_file(build_path(dir, lvnum, "lgtfx"), buf))
        return false;

    VALUE root;
    char err[255] = "";
    if (toml_parse(buf.data(), err, sizeof(err), &root))
        return false;

    const size_t count = native_record_count(&root, "light", "LightsCount");
    content.lights.clear();
    content.lights.reserve(count);
    for (size_t k = 0; k < count; k++)
    {
        VALUE *d = native_record_at(&root, "light", "light%" PRId64, (int64_t)(k));
        if (value_type(d) != VALUE_DICT)
            continue;
        MapLightRecord l;
        l.is_dynamic = value_coerce_bool(value_dict_get(d, "Dynamic"));
        l.pos_x = read_stl_coord(d, "SubtileX");
        l.pos_y = read_stl_coord(d, "SubtileY");
        l.pos_z = read_stl_coord(d, "SubtileZ");
        l.range = read_stl_coord(d, "LightRange");
        l.intensity = (uint64_t)read_int_default(d, "LightIntensity", 0);
        l.parent_tile = (uint64_t)read_int_default(d, "ParentTile", 0);
        content.lights.push_back(l);
    }
    value_fini(&root);
    return true;
}

bool KfxNativeMapContentReader::read_action_points(MapContent &content, const char *dir, LevelNumber lvnum)
{
    std::vector<char> buf;
    if (!load_whole_file(build_path(dir, lvnum, "aptfx"), buf))
        return false;

    VALUE root;
    char err[255] = "";
    if (toml_parse(buf.data(), err, sizeof(err), &root))
        return false;

    const size_t count = native_record_count(&root, "actionpoint", "ActionPointsCount");
    content.action_points.clear();
    content.action_points.reserve(count);
    for (size_t k = 0; k < count; k++)
    {
        VALUE *d = native_record_at(&root, "actionpoint", "actionpoint%" PRId64, (int64_t)(k));
        if (value_type(d) != VALUE_DICT)
            continue;
        MapActionPointRecord a;
        a.point_number = read_int_default(d, "PointNumber", 0);
        a.pos_x = read_stl_coord(d, "SubtileX");
        a.pos_y = read_stl_coord(d, "SubtileY");
        a.range = read_stl_coord(d, "PointRange");
        content.action_points.push_back(a);
    }
    value_fini(&root);
    return true;
}

// .lof is a classic key=value command format, not TOML (see
// map_content_writer.cpp's own write_level_info() comment) -- a small
// line parser matching exactly what that function emits, not a reuse of
// level_lof_file_parse() (which registers into the real campaign/
// LevelInformation structures, a much bigger job out of scope here).
bool KfxNativeMapContentReader::read_level_info(MapContent &content, const char *dir, LevelNumber lvnum)
{
    std::vector<char> buf;
    if (!load_whole_file(build_path(dir, lvnum, "lof"), buf))
        return false;

    char *line = std::strtok(buf.data(), "\r\n");
    while (line != NULL)
    {
        char *eq = std::strchr(line, '=');
        if (eq != NULL)
        {
            *eq = '\0';
            char *key = line;
            char *value = eq + 1;
            while ((*key == ' ') || (*key == '\t')) key++;
            char *key_end = key + std::strlen(key);
            while ((key_end > key) && ((key_end[-1] == ' ') || (key_end[-1] == '\t'))) *--key_end = '\0';
            while ((*value == ' ') || (*value == '\t')) value++;

            if (std::strcmp(key, "NAME_TEXT") == 0)
                content.level_info.name_text = value;
            else if (std::strcmp(key, "KIND") == 0)
                content.level_info.is_multiplayer = (std::strcmp(value, "MULTI") == 0);
            else if (std::strcmp(key, "PLAYERS") == 0)
                content.level_info.players = atoi(value);
            else if (std::strcmp(key, "DESCRIPTION") == 0)
                content.level_info.description_text = value;
            else if (std::strcmp(key, "AUTHOR") == 0)
                content.level_info.author_text = value;
        }
        line = std::strtok(NULL, "\r\n");
    }
    return true;
}

// --- ClassicMapContentReader ------------------------------------------------
// Mirrors ClassicMapContentWriter's own byte packing exactly (map_content_
// writer.cpp) -- see that file's header comment for the field-layout
// source (thing_create_thing()/light_create_light()/
// actnpoint_create_actnpoint(), not the TOML-path "_adv" callbacks).

namespace {

uint64_t read_u8(const std::vector<char> &buf, size_t off) { return (unsigned char)buf[off]; }
uint64_t read_u16le(const std::vector<char> &buf, size_t off)
{
    return (unsigned char)buf[off] | ((uint64_t)(unsigned char)buf[off + 1] << 8);
}
uint64_t read_u32le(const std::vector<char> &buf, size_t off)
{
    return (uint64_t)(unsigned char)buf[off]
        | ((uint64_t)(unsigned char)buf[off + 1] << 8)
        | ((uint64_t)(unsigned char)buf[off + 2] << 16)
        | ((uint64_t)(unsigned char)buf[off + 3] << 24);
}

const size_t kLegacyThingSize = 21;
const size_t kLegacyLightSize = 20;
const size_t kLegacyActionPointSize = 8;

} // namespace

bool ClassicMapContentReader::read_things(MapContent &content, const char *dir, LevelNumber lvnum)
{
    std::vector<char> buf;
    if (!load_whole_file(build_path(dir, lvnum, "tng"), buf))
        return false;
    if (buf.size() < 3) // 2-byte count + trailing NUL
        return false;
    uint64_t count = read_u16le(buf, 0);
    size_t off = 2;
    content.things.clear();
    content.things.reserve(count);
    for (uint64_t k = 0; k < count; k++)
    {
        if (off + kLegacyThingSize > buf.size() - 1) // -1 for the trailing NUL
            break;
        MapThingRecord t;
        t.pos_x = (MapCoord)read_u16le(buf, off + 0);
        t.pos_y = (MapCoord)read_u16le(buf, off + 2);
        t.pos_z = (MapCoord)read_u16le(buf, off + 4);
        t.thing_class = (ThingClass)read_u8(buf, off + 6);
        t.model = (ThingModel)read_u8(buf, off + 7);
        t.owner = (PlayerNumber)read_u8(buf, off + 8);
        uint64_t range = read_u16le(buf, off + 9);
        uint64_t index = read_u16le(buf, off + 11);
        unsigned char params[8];
        for (int64_t i = 0; i < 8; i++)
            params[i] = (unsigned char)read_u8(buf, off + 13 + i);

        t.parent_tile = (int64_t)index; // Object/Trap/EffectGen only, per the writer
        if (t.thing_class == TCls_EffectGen)
            t.effect_range = (MapCoord)range;
        if (t.thing_class == TCls_Object)
        {
            // Can't tell HerogateNumber from CustomBox back apart from the
            // raw bytes alone (both are "params[1]") -- same ambiguity the
            // production classic loader has, resolved there by checking
            // the *model*'s own object-class flags after creation
            // (object_is_hero_gate()/thing_is_custom_special_box()).
            // MapContent has no such check on hand here; stored as
            // herogate_number, the more common case for a map's own
            // "extra" objects -- a real limitation of round-tripping
            // classic custom-box maps through this reader, not the writer
            // (which always writes the correct one), worth revisiting if
            // custom boxes become a real test case.
            t.herogate_number = params[1];
        }
        else if (t.thing_class == TCls_Creature)
        {
            t.creature_level = (int64_t)params[1];
        }
        else if (t.thing_class == TCls_Door)
        {
            t.door_orientation = params[0];
            t.door_locked = params[1] != 0;
        }
        content.things.push_back(t);
        off += kLegacyThingSize;
    }
    return true;
}

bool ClassicMapContentReader::read_lights(MapContent &content, const char *dir, LevelNumber lvnum)
{
    std::vector<char> buf;
    if (!load_whole_file(build_path(dir, lvnum, "lgt"), buf))
        return false;
    if (buf.size() < 5)
        return false;
    uint64_t count = read_u32le(buf, 0);
    size_t off = 4;
    content.lights.clear();
    content.lights.reserve(count);
    for (uint64_t k = 0; k < count; k++)
    {
        if (off + kLegacyLightSize > buf.size() - 1)
            break;
        MapLightRecord l;
        l.range = (MapCoord)read_u16le(buf, off + 0); // radius
        l.intensity = read_u8(buf, off + 2);
        // off+3 = flags, off+4..9 = 3x unused i16 -- not round-tripped,
        // matches the writer's own "not implemented" scope.
        l.pos_x = (MapCoord)read_u16le(buf, off + 10);
        l.pos_y = (MapCoord)read_u16le(buf, off + 12);
        l.pos_z = (MapCoord)read_u16le(buf, off + 14);
        // off+16 = unused
        l.is_dynamic = read_u8(buf, off + 17) != 0;
        l.parent_tile = read_u16le(buf, off + 18);
        content.lights.push_back(l);
        off += kLegacyLightSize;
    }
    return true;
}

bool ClassicMapContentReader::read_action_points(MapContent &content, const char *dir, LevelNumber lvnum)
{
    std::vector<char> buf;
    if (!load_whole_file(build_path(dir, lvnum, "apt"), buf))
        return false;
    if (buf.size() < 5)
        return false;
    uint64_t count = read_u32le(buf, 0);
    size_t off = 4;
    content.action_points.clear();
    content.action_points.reserve(count);
    for (uint64_t k = 0; k < count; k++)
    {
        if (off + kLegacyActionPointSize > buf.size() - 1)
            break;
        MapActionPointRecord a;
        a.pos_x = (MapCoord)read_u16le(buf, off + 0);
        a.pos_y = (MapCoord)read_u16le(buf, off + 2);
        a.range = (MapCoord)read_u16le(buf, off + 4);
        a.point_number = (int64_t)read_u16le(buf, off + 6);
        content.action_points.push_back(a);
        off += kLegacyActionPointSize;
    }
    return true;
}

// .lif isn't written this slice (ClassicMapContentWriter::write_level_info()'s
// own comment) -- nothing to read back either.
bool ClassicMapContentReader::read_level_info(MapContent &content, const char *dir, LevelNumber lvnum)
{
    (void)content; (void)dir; (void)lvnum;
    return true;
}
