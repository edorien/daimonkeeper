/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file map_content_reader.h
 *     Parses a saved map's files back into a MapContent snapshot, one
 *     format per subclass -- the mirror image of map_content_writer.h.
 * @par Purpose:
 *     docs/refactor/editor/phase3/00-slice1-native-save.md. This is new,
 *     purpose-built parsing -- not a refactor of the production
 *     load_tngfx_file()/load_lgtfx_file()/load_aptfx_file() (lvl_filesdk1.c),
 *     which mutate kfx_sim_state/kfx_render's live global arrays directly
 *     via callbacks and stay untouched here, to avoid destabilizing real
 *     gameplay loading. It does reuse the same underlying TOML parsing
 *     library (CentiTOML's toml_parse()/VALUE tree) and the same
 *     value_read_stl_coord() field-decoding helper those loaders use
 *     (src/kfx_config/include/value_util.h), so the actual byte-level
 *     format handling is shared, proven code -- only the "what do I do
 *     with each field" destination differs (a MapContent struct instead
 *     of a live Thing/Light/ActionPoint).
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_MAP_CONTENT_READER_H
#define DK_MAP_CONTENT_READER_H

#include "bflib_basics.h"
#include "globals.h"
#include "map_content.h"

class MapContentReader
{
public:
    virtual ~MapContentReader() = default;

    // Reads every file this format expects for a map sized
    // map_tiles_x/map_tiles_y (must already be set on `content` -- the
    // slab grid's own dimensions aren't re-derived from file size) out of
    // `dir` into `content`. Returns false if any required file is missing
    // or malformed; still attempts every file rather than stopping at the
    // first failure.
    bool read(MapContent &content, const char *dir, LevelNumber lvnum);

protected:
    bool read_slabs(MapContent &content, const char *dir, LevelNumber lvnum);
    bool read_ownership(MapContent &content, const char *dir, LevelNumber lvnum);
    bool read_texture(MapContent &content, const char *dir, LevelNumber lvnum);
    // docs/refactor/editor/05-script-and-level-settings.md's "per-slab
    // texture paint" item -- .slx ("ExtSlab", load_ext_slabs()/
    // lvl_filesdk1.c), format-independent like read_texture() above (both
    // formats use the same flat one-byte-per-slab file). A missing/short
    // file isn't an error -- leaves every entry 0 ("no override"), the same
    // convention load_ext_slabs() itself already uses live.
    bool read_slab_texture(MapContent &content, const char *dir, LevelNumber lvnum);
    // docs/refactor/editor/05-script-and-level-settings.md §0 -- verbatim,
    // format-independent like the three siblings above (both formats use
    // the same plain-text map%05lu.txt). A missing file isn't an error --
    // leaves script_text empty, which write_script() (map_content_writer.h)
    // treats as "no real script to preserve, safe to write the empty stub".
    bool read_script(MapContent &content, const char *dir, LevelNumber lvnum);

    virtual bool read_things(MapContent &content, const char *dir, LevelNumber lvnum) = 0;
    virtual bool read_lights(MapContent &content, const char *dir, LevelNumber lvnum) = 0;
    virtual bool read_action_points(MapContent &content, const char *dir, LevelNumber lvnum) = 0;
    virtual bool read_level_info(MapContent &content, const char *dir, LevelNumber lvnum) = 0;
};

class KfxNativeMapContentReader : public MapContentReader
{
protected:
    bool read_things(MapContent &content, const char *dir, LevelNumber lvnum) override;
    bool read_lights(MapContent &content, const char *dir, LevelNumber lvnum) override;
    bool read_action_points(MapContent &content, const char *dir, LevelNumber lvnum) override;
    bool read_level_info(MapContent &content, const char *dir, LevelNumber lvnum) override;
};

// docs/refactor/editor/phase3/01-slice2-classic-save.md -- mirrors
// ClassicMapContentWriter (map_content_writer.h). read_level_info() is a
// no-op (returns true) this slice -- .lif isn't written yet, see that
// class's own comment.
class ClassicMapContentReader : public MapContentReader
{
protected:
    bool read_things(MapContent &content, const char *dir, LevelNumber lvnum) override;
    bool read_lights(MapContent &content, const char *dir, LevelNumber lvnum) override;
    bool read_action_points(MapContent &content, const char *dir, LevelNumber lvnum) override;
    bool read_level_info(MapContent &content, const char *dir, LevelNumber lvnum) override;
};

#endif
