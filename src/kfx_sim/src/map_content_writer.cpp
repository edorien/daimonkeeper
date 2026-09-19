/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file map_content_writer.cpp
 *     See map_content_writer.h.
 * @par Comment:
 *     None.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "map_content_writer.h"

#include "bflib_dernc.h"
#include "config_campaigns.h" // LEVEL_DESCRIPTION_LEN

#include <cstdio>
#include <string>

#include "post_inc.h"

namespace {

std::string build_path(const char *dir, LevelNumber lvnum, const char *ext)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "/map%05lu.%s", (unsigned long)lvnum, ext);
    return std::string(dir) + buf;
}

// docs/refactor/editor/phase3/05-slice6-atomic-write-lif.md -- writes via
// a temp-file-then-rename so a failure partway through leaves the
// *previous* save on disk intact, not a half-written file -- every file
// this class family writes (.slb/.own/.inf/.txt/.tngfx/.lgtfx/.aptfx/.lof/
// .lif and the classic .tng/.lgt/.apt) goes through this one helper.
bool save_text(const std::string &path, const std::string &text)
{
    return LbFileSaveAtomic(path.c_str(), text.data(), (unsigned long)text.size()) != 0;
}

// Splits a raw MapCoord (COORD_PER_STL=256 per subtile) into the
// [whole_subtile, sub_subtile] pair value_read_stl_coord() (src/kfx_config/
// include/value_util.h) expects on the read side -- the exact inverse of
// that function's own `(stl << 8) | (sub_stl & 0xFF)`.
void append_stl_coord(std::string &out, const char *key, MapCoord coord)
{
    long stl = coord >> 8;
    long sub_stl = coord & 0xFF;
    char buf[96];
    snprintf(buf, sizeof(buf), "%s = [%ld, %ld]\n", key, stl, sub_stl);
    out += buf;
}

void append_int(std::string &out, const char *key, long value)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "%s = %ld\n", key, value);
    out += buf;
}

void append_bool(std::string &out, const char *key, bool value)
{
    out += key;
    out += (value ? " = true\n" : " = false\n");
}

// TOML strings need at least backslash/quote escaping -- level names and
// creature names are short, user-authored text, never file paths or
// anything with control characters in practice, so this covers what
// actually appears without pulling in a general escaper.
void append_string(std::string &out, const char *key, const std::string &value)
{
    out += key;
    out += " = \"";
    for (char c : value)
    {
        if ((c == '"') || (c == '\\'))
            out += '\\';
        out += c;
    }
    out += "\"\n";
}

} // namespace

bool MapContentWriter::write(const MapContent &content, const char *dir, LevelNumber lvnum)
{
    bool result = true;
    if (!write_slabs(content, dir, lvnum)) result = false;
    if (!write_ownership(content, dir, lvnum)) result = false;
    if (!write_texture(content, dir, lvnum)) result = false;
    if (!write_slab_texture(content, dir, lvnum)) result = false;
    if (!write_script(content, dir, lvnum)) result = false;
    if (!write_things(content, dir, lvnum)) result = false;
    if (!write_lights(content, dir, lvnum)) result = false;
    if (!write_action_points(content, dir, lvnum)) result = false;
    if (!write_level_info(content, dir, lvnum)) result = false;
    if (!write_lif(content, dir, lvnum)) result = false;
    return result;
}

// Mirrors load_map_slab_file()'s own read exactly (lvl_filesdk1.c:1196,
// docs/refactor/editor/phase3/00-slice1-native-save.md's "ground-truth
// corrections" -- full LE u16 per slab, not "low byte = kind" as an
// earlier draft of the design doc had it): row-major, sy outer, sx inner.
bool MapContentWriter::write_slabs(const MapContent &content, const char *dir, LevelNumber lvnum)
{
    std::string buf;
    buf.resize((size_t)(content.map_tiles_x * content.map_tiles_y) * 2);
    size_t i = 0;
    for (long y = 0; y < content.map_tiles_y; y++)
    {
        for (long x = 0; x < content.map_tiles_x; x++)
        {
            unsigned n = content.slab_kind[content.slab_index(x, y)];
            buf[i] = (char)(n & 0xFF);
            buf[i + 1] = (char)((n >> 8) & 0xFF);
            i += 2;
        }
    }
    return save_text(build_path(dir, lvnum, "slb"), buf);
}

// Mirrors load_map_ownership_file()'s own read: one owner byte per
// subtile (not per slab), (map_subtiles_y+1)*(map_subtiles_x+1) laid out
// row-major -- every subtile of a slab shares that slab's owner.
bool MapContentWriter::write_ownership(const MapContent &content, const char *dir, LevelNumber lvnum)
{
    long subtiles_x = content.map_tiles_x * STL_PER_SLB;
    long subtiles_y = content.map_tiles_y * STL_PER_SLB;
    std::string buf;
    buf.resize((size_t)((subtiles_y + 1) * (subtiles_x + 1)));
    size_t i = 0;
    for (long y = 0; y <= subtiles_y; y++)
    {
        for (long x = 0; x <= subtiles_x; x++)
        {
            long sx = x / STL_PER_SLB;
            long sy = y / STL_PER_SLB;
            if (sx >= content.map_tiles_x) sx = content.map_tiles_x - 1;
            if (sy >= content.map_tiles_y) sy = content.map_tiles_y - 1;
            buf[i++] = (char)(unsigned char)content.slab_owner[content.slab_index(sx, sy)];
        }
    }
    return save_text(build_path(dir, lvnum, "own"), buf);
}

// Mirrors load_and_setup_map_info()'s own read: a single byte, the base
// texture set id.
bool MapContentWriter::write_texture(const MapContent &content, const char *dir, LevelNumber lvnum)
{
    char b = (char)(unsigned char)content.texture_id;
    return save_text(build_path(dir, lvnum, "inf"), std::string(1, b));
}

// Mirrors load_ext_slabs()'s own read: one byte per slab, row-major, same
// layout as write_slabs()/write_texture() above. Defensively tolerates a
// shorter (or entirely unpopulated) content.slab_texture -- unlike slab_
// kind/slab_owner, every MapContent producer has had to fill in since the
// struct's own introduction, this field was added later onto existing
// callers/tests that don't know about it yet (found live: a pre-existing
// Catch2 fixture with no slab_texture at all crashed here on an
// out-of-bounds read before this guard existed).
bool MapContentWriter::write_slab_texture(const MapContent &content, const char *dir, LevelNumber lvnum)
{
    std::string buf;
    buf.resize((size_t)(content.map_tiles_x * content.map_tiles_y));
    for (size_t i = 0; i < buf.size(); i++)
        buf[i] = (i < content.slab_texture.size()) ? (char)content.slab_texture[i] : (char)0;
    return save_text(build_path(dir, lvnum, "slx"), buf);
}

bool MapContentWriter::write_script(const MapContent &content, const char *dir, LevelNumber lvnum)
{
    // docs/refactor/editor/05-script-and-level-settings.md §0 -- this used
    // to always overwrite the script with the empty stub below, discarding
    // whatever a level actually had (real content, most of the time --
    // 85%+ of shipped levels use this classic .txt format). Empty
    // script_text means "nothing was read to preserve" (a genuinely new
    // map, or MapContentReader::read_script() found no file) -- only then
    // does the stub apply.
    if (content.script_text.empty())
        return save_text(build_path(dir, lvnum, "txt"), "REM Empty script, generated by the in-game editor.\n");
    return save_text(build_path(dir, lvnum, "txt"), content.script_text);
}

// Mirrors level_lof_file_parse()'s own commands (docs/refactor/editor/
// 07-investigation-findings.md F4): NAME_TEXT, KIND, PLAYERS. Written as
// the classic key=value .lof command form, not TOML -- .lof is its own,
// older, non-TOML format. Concrete on the base class (not per-subclass) --
// see map_content_writer.h's own comment on why this one is
// format-independent, unlike write_things/write_lights/write_action_points.
bool MapContentWriter::write_level_info(const MapContent &content, const char *dir, LevelNumber lvnum)
{
    char buf[512 + LEVEL_DESCRIPTION_LEN];
    int n = snprintf(buf, sizeof(buf),
        "NAME_TEXT = %s\n"
        "KIND = %s\n"
        "PLAYERS = %d\n",
        content.level_info.name_text.c_str(),
        content.level_info.is_multiplayer ? "MULTI" : "SINGLE",
        content.level_info.players);
    // docs/refactor/editor/05-script-and-level-settings.md -- DESCRIPTION is
    // a real, already-recognized .lof keyword (level_lof_file_parse(),
    // lvl_filesdk1.c) now actually read into LevelInformation::description;
    // only written when non-empty so a level with no description doesn't
    // gain an empty DESCRIPTION line it never had.
    if (!content.level_info.description_text.empty() && (n > 0) && ((size_t)n < sizeof(buf)))
    {
        snprintf(buf + n, sizeof(buf) - (size_t)n, "DESCRIPTION = %s\n",
            content.level_info.description_text.c_str());
    }
    return save_text(build_path(dir, lvnum, "lof"), buf);
}

// docs/refactor/editor/phase3/05-slice6-atomic-write-lif.md -- one line,
// "<lvnum>, <name>\r\n", matching real shipped .lif files exactly (verified
// against core_files/campgns/lqizgood/map00210.lif's own byte content) --
// level_lif_entry_parse() (lvl_filesdk1.c) tolerates LF alone too, but CRLF
// matches what real content actually ships. Per-level file (map%05u.lif),
// not merged into any other level's own .lif -- find_and_load_lif_files()
// globs *every* .lif file it finds and accumulates their entries, so many
// small single-entry files work identically to one large shared one, with
// none of the read-existing-and-merge complexity that would otherwise
// require. No-op when unnamed -- see this method's own declaration
// (map_content_writer.h) for why.
bool MapContentWriter::write_lif(const MapContent &content, const char *dir, LevelNumber lvnum)
{
    if (content.level_info.name_text.empty())
        return true;
    char buf[512];
    snprintf(buf, sizeof(buf), "%lu, %s\r\n", (unsigned long)lvnum, content.level_info.name_text.c_str());
    return save_text(build_path(dir, lvnum, "lif"), buf);
}

bool KfxNativeMapContentWriter::write_things(const MapContent &content, const char *dir, LevelNumber lvnum)
{
    std::string out = "[common]\n";
    append_int(out, "ThingsCount", (long)content.things.size());
    out += "\n";
    for (const MapThingRecord &t : content.things)
    {
        out += "[[thing]]\n";
        append_int(out, "ThingType", t.thing_class);
        append_int(out, "Subtype", t.model);
        append_int(out, "Ownership", t.owner);
        append_stl_coord(out, "SubtileX", t.pos_x);
        append_stl_coord(out, "SubtileY", t.pos_y);
        append_stl_coord(out, "SubtileZ", t.pos_z);
        if ((t.thing_class == TCls_Object) || (t.thing_class == TCls_Trap) || (t.thing_class == TCls_EffectGen))
            append_int(out, "ParentTile", t.parent_tile);
        if ((t.thing_class == TCls_Object) || (t.thing_class == TCls_Creature) || (t.thing_class == TCls_Trap))
            append_int(out, "Orientation", t.orientation);
        if (t.thing_class == TCls_Creature)
        {
            append_int(out, "CreatureLevel", t.creature_level + 1); // file format is 1-based
            append_int(out, "CreatureGold", t.creature_gold);
            append_int(out, "CreatureInitialHealth", t.creature_health_percent);
            if (!t.creature_name.empty())
                append_string(out, "CreatureName", t.creature_name);
        }
        if (t.thing_class == TCls_Object)
        {
            if (t.herogate_number != 0)
                append_int(out, "HerogateNumber", t.herogate_number);
            else if (t.custom_box_kind != 0)
                append_int(out, "CustomBox", t.custom_box_kind);
            else if (t.gold_value != 0)
                append_int(out, "GoldValue", t.gold_value);
        }
        if (t.thing_class == TCls_EffectGen)
            append_stl_coord(out, "EffectRange", t.effect_range);
        if (t.thing_class == TCls_Door)
        {
            append_int(out, "DoorOrientation", t.door_orientation);
            // Written as a plain 0/1 int, not a TOML boolean literal --
            // matches thing_create_thing_adv()'s own read
            // (value_int32(value_dict_get(init_data, "DoorLocked"))), not
            // value_coerce_bool() the way "Dynamic" (lights) reads.
            append_int(out, "DoorLocked", t.door_locked ? 1 : 0);
        }
        out += "\n";
    }
    return save_text(build_path(dir, lvnum, "tngfx"), out);
}

bool KfxNativeMapContentWriter::write_lights(const MapContent &content, const char *dir, LevelNumber lvnum)
{
    std::string out = "[common]\n";
    append_int(out, "LightsCount", (long)content.lights.size());
    out += "\n";
    for (const MapLightRecord &l : content.lights)
    {
        out += "[[light]]\n";
        append_bool(out, "Dynamic", l.is_dynamic);
        append_stl_coord(out, "SubtileX", l.pos_x);
        append_stl_coord(out, "SubtileY", l.pos_y);
        append_stl_coord(out, "SubtileZ", l.pos_z);
        append_stl_coord(out, "LightRange", l.range);
        append_int(out, "LightIntensity", (long)l.intensity);
        append_int(out, "ParentTile", (long)l.parent_tile);
        out += "\n";
    }
    return save_text(build_path(dir, lvnum, "lgtfx"), out);
}

bool KfxNativeMapContentWriter::write_action_points(const MapContent &content, const char *dir, LevelNumber lvnum)
{
    std::string out = "[common]\n";
    append_int(out, "ActionPointsCount", (long)content.action_points.size());
    out += "\n";
    for (const MapActionPointRecord &a : content.action_points)
    {
        out += "[[actionpoint]]\n";
        append_int(out, "PointNumber", a.point_number);
        append_stl_coord(out, "SubtileX", a.pos_x);
        append_stl_coord(out, "SubtileY", a.pos_y);
        append_stl_coord(out, "PointRange", a.range);
        out += "\n";
    }
    return save_text(build_path(dir, lvnum, "aptfx"), out);
}

// --- ClassicMapContentWriter ------------------------------------------------
// docs/refactor/editor/phase3/01-slice2-classic-save.md -- byte layouts
// read directly from thing_create_thing()/light_create_light()/
// actnpoint_create_actnpoint() (lvl_filesdk1.c's classic-path readers'
// construction targets), not just struct sizeof()s. Raw byte packing
// (append_u8/u16/u32), not a local mirror of the #pragma pack(1) structs
// those readers use privately -- matches this file's own write_slabs()
// precedent and sidesteps any struct-layout/alignment ambiguity entirely.

namespace {

void append_u8(std::string &out, unsigned v) { out += (char)(v & 0xFF); }
void append_u16le(std::string &out, unsigned v)
{
    out += (char)(v & 0xFF);
    out += (char)((v >> 8) & 0xFF);
}
void append_u32le(std::string &out, unsigned long v)
{
    out += (char)(v & 0xFF);
    out += (char)((v >> 8) & 0xFF);
    out += (char)((v >> 16) & 0xFF);
    out += (char)((v >> 24) & 0xFF);
}

} // namespace

bool ClassicMapContentWriter::write_things(const MapContent &content, const char *dir, LevelNumber lvnum)
{
    std::string out;
    append_u16le(out, (unsigned)content.things.size());
    for (const MapThingRecord &t : content.things)
    {
        append_u16le(out, (unsigned)(t.pos_x & 0xFFFF)); // LegacyCoord3d
        append_u16le(out, (unsigned)(t.pos_y & 0xFFFF));
        append_u16le(out, (unsigned)(t.pos_z & 0xFFFF));
        append_u8(out, t.thing_class);
        append_u8(out, (unsigned)t.model);
        append_u8(out, (unsigned)t.owner);
        long range = 0;
        long index = (t.parent_tile >= 0) ? t.parent_tile : 0;
        unsigned char params[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        if (t.thing_class == TCls_EffectGen)
            range = t.effect_range;
        if (t.thing_class == TCls_Object)
        {
            if (t.herogate_number != 0)
                params[1] = (unsigned char)t.herogate_number;
            else if (t.custom_box_kind != 0)
                params[1] = (unsigned char)t.custom_box_kind;
            // GoldValue has no classic representation at all -- see this
            // file's own header comment; silently not written (matches
            // map_is_legacy_compatible() refusing such a map Auto-format
            // in the first place, so this path is only reached under
            // Force Classic, a deliberate "I accept the data loss" choice).
        }
        else if (t.thing_class == TCls_Creature)
        {
            params[1] = (unsigned char)t.creature_level;
        }
        else if (t.thing_class == TCls_Door)
        {
            params[0] = (unsigned char)t.door_orientation;
            params[1] = t.door_locked ? 1 : 0;
        }
        append_u16le(out, (unsigned)range);
        append_u16le(out, (unsigned)index);
        for (int i = 0; i < 8; i++)
            append_u8(out, params[i]);
    }
    return save_text(build_path(dir, lvnum, "tng"), out);
}

bool ClassicMapContentWriter::write_lights(const MapContent &content, const char *dir, LevelNumber lvnum)
{
    std::string out;
    append_u32le(out, (unsigned long)content.lights.size());
    for (const MapLightRecord &l : content.lights)
    {
        append_u16le(out, (unsigned)(l.range & 0xFFFF)); // radius (i16)
        append_u8(out, l.intensity & 0xFF);
        append_u8(out, 0); // flags -- unimplemented, see this file's own header comment
        append_u16le(out, 0); // field_4_unused
        append_u16le(out, 0); // field_6_unused
        append_u16le(out, 0); // field_8_unused
        append_u16le(out, (unsigned)(l.pos_x & 0xFFFF));
        append_u16le(out, (unsigned)(l.pos_y & 0xFFFF));
        append_u16le(out, (unsigned)(l.pos_z & 0xFFFF));
        append_u8(out, 0); // field_10_unused
        append_u8(out, l.is_dynamic ? 1 : 0);
        append_u16le(out, (unsigned)(l.parent_tile & 0xFFFF)); // attached_slb (i16)
    }
    return save_text(build_path(dir, lvnum, "lgt"), out);
}

bool ClassicMapContentWriter::write_action_points(const MapContent &content, const char *dir, LevelNumber lvnum)
{
    std::string out;
    append_u32le(out, (unsigned long)content.action_points.size());
    for (const MapActionPointRecord &a : content.action_points)
    {
        append_u16le(out, (unsigned)(a.pos_x & 0xFFFF)); // LegacyCoord2d
        append_u16le(out, (unsigned)(a.pos_y & 0xFFFF));
        append_u16le(out, (unsigned)(a.range & 0xFFFF));
        append_u16le(out, (unsigned)a.point_number);
    }
    return save_text(build_path(dir, lvnum, "apt"), out);
}
