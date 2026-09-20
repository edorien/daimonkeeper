/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file map_content_writer.h
 *     Serializes a MapContent snapshot to disk, one format per subclass.
 * @par Purpose:
 *     docs/refactor/editor/phase3/00-slice1-native-save.md -- a small
 *     virtual class family (Strategy pattern), modeled on this codebase's
 *     own existing LensEffect (src/kfx_render/include/LensEffect.h) /
 *     IPlatform (src/kfx_platform/include/platform/IPlatform.h) style: one
 *     abstract interface, one concrete subclass per output format, so
 *     choosing a format is choosing a concrete class, not branching at
 *     every call site. This slice ships one concrete writer
 *     (KfxNativeMapContentWriter, TOML). A classic-binary
 *     ClassicMapContentWriter is a named, deferred future subclass -- the
 *     base class is already shaped so adding it needs no rework here.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_MAP_CONTENT_WRITER_H
#define DK_MAP_CONTENT_WRITER_H

#include "bflib_basics.h"
#include "globals.h"
#include "map_content.h"

// Shared, format-independent pieces (identical either way -- docs/refactor/
// editor/03-map-serialization.md §3 step 4) are concrete methods here, not
// overridden per format; only the pieces that genuinely differ between
// TOML and classic-binary are pure virtual.
class MapContentWriter
{
public:
    virtual ~MapContentWriter() = default;

    // Writes every file this format produces for `content` into `dir` (a
    // plain directory path -- no lvnum/fgroup campaign-path resolution;
    // see the phase-3 slice-1 doc for why the whole class family is kept
    // decoupled from that). Returns false if any single file fails to
    // write; still attempts every file rather than stopping at the first
    // failure, so a caller gets the fullest possible picture of what went
    // wrong from one call.
    bool write(const MapContent &content, const char *dir, LevelNumber lvnum);

    // docs/refactor/editor/phase3/04-slice5-playtest-settings-overwrite.md
    // -- public entry point for writing *just* the level's identity/
    // metadata files (.lof + .lif, docs/refactor/editor/phase3/
    // 05-slice6-atomic-write-lif.md), independent of the rest of write()'s
    // own file set (the Level Settings dialog's own Apply action needs
    // exactly this, not a full map re-save). Any concrete subclass works
    // -- both are format-independent, see their own comments below.
    bool write_level_info_only(const MapContent &content, const char *dir, LevelNumber lvnum)
    {
        bool result = write_level_info(content, dir, lvnum);
        if (!write_lif(content, dir, lvnum))
            result = false;
        return result;
    }

protected:
    bool write_slabs(const MapContent &content, const char *dir, LevelNumber lvnum);
    bool write_ownership(const MapContent &content, const char *dir, LevelNumber lvnum);
    bool write_texture(const MapContent &content, const char *dir, LevelNumber lvnum);
    // docs/refactor/editor/05-script-and-level-settings.md's "per-slab
    // texture paint" item -- .slx ("ExtSlab", load_ext_slabs()/
    // lvl_filesdk1.c), format-independent like write_texture() above (both
    // formats use the same flat one-byte-per-slab file, and it's read
    // regardless of which format loaded the rest of the map). Always
    // written, like write_slabs()/write_ownership() -- not skipped when
    // every override is 0, so a KFX-editor save is self-consistently
    // complete rather than depending on whether a .slx happened to exist
    // before this save.
    bool write_slab_texture(const MapContent &content, const char *dir, LevelNumber lvnum);
    // docs/refactor/editor/05-script-and-level-settings.md §0 -- writes
    // content.script_text verbatim (round-tripped by MapContentReader::
    // read_script()); falls back to a minimal empty-script stub only when
    // script_text is empty (a genuinely new map, or nothing was read), so a
    // reload's later load_script() step still has something valid to find.
    bool write_script(const MapContent &content, const char *dir, LevelNumber lvnum);
    // fx-plans/02-lua-scripts.md L1 -- writes lua_text verbatim when has_lua,
    // and removes a stale map%05lu.lua when the map has none, so a Save As
    // over an existing number can't leave a foreign Lua script running.
    bool write_lua(const MapContent &content, const char *dir, LevelNumber lvnum);
    // docs/refactor/editor/phase3/03-slice4-file-dialogs.md -- .lof
    // (NAME_TEXT/KIND/PLAYERS) is format-independent (a KFX-editor
    // auto-discovery/metadata sidecar, not part of either map-data format
    // itself), so this moved here from a per-subclass override once a
    // classic-format save needed the same level-name write KFX-native
    // already had.
    bool write_level_info(const MapContent &content, const char *dir, LevelNumber lvnum);
    // docs/refactor/editor/phase3/05-slice6-atomic-write-lif.md -- .lif
    // (the shared freeplay registry -- level_lif_entry_parse(),
    // lvl_filesdk1.c) turned out to be the mechanism *real shipped classic
    // content* actually uses for Free Play discovery (confirmed against
    // core_files/campgns/lqizgood/map00210.lif, a real level with no .lof
    // at all) -- format-independent same as .lof, written per-level
    // (map%05u.lif, one line) rather than merged into any other level's
    // own .lif, so no read-existing-and-append complexity is needed.
    // No-op (returns true) when the level has no name yet -- an unnamed
    // entry is meaningless to level_lif_entry_parse() and it logs a
    // warning if the name field comes back empty.
    bool write_lif(const MapContent &content, const char *dir, LevelNumber lvnum);

    // Files the *other* format leaves behind. The loader prefers the KeeperFX
    // .tngfx/.lgtfx/.aptfx and any existing .clm/.dat/.wib, so a stale copy from
    // an earlier save in the other format would silently override this save.
    // Called after a successful write.
    virtual void remove_stale_files(const char *dir, LevelNumber lvnum) = 0;

    // No-op in the base class; the classic writer writes .clm/.dat/.wib.
    virtual bool write_derived_data(const MapContent &content, const char *dir, LevelNumber lvnum);

    virtual bool write_things(const MapContent &content, const char *dir, LevelNumber lvnum) = 0;
    virtual bool write_lights(const MapContent &content, const char *dir, LevelNumber lvnum) = 0;
    virtual bool write_action_points(const MapContent &content, const char *dir, LevelNumber lvnum) = 0;
};

// TOML emission for tngfx/lgtfx/aptfx/lof -- field names/types read
// directly from the production loader callbacks (thing_create_thing_adv()/
// light_create_light_adv()/actnpoint_create_actnpoint_adv()), not just the
// design doc's summary of them; see map_content.h's own per-struct
// comments for the exact source cross-references. Hand-emitted: CentiTOML
// (deps/centitoml) is parse-only, no writer exists in this codebase.
class KfxNativeMapContentWriter : public MapContentWriter
{
protected:
    void remove_stale_files(const char *dir, LevelNumber lvnum) override;
    bool write_things(const MapContent &content, const char *dir, LevelNumber lvnum) override;
    bool write_lights(const MapContent &content, const char *dir, LevelNumber lvnum) override;
    bool write_action_points(const MapContent &content, const char *dir, LevelNumber lvnum) override;
};

// docs/refactor/editor/phase3/01-slice2-classic-save.md -- classic binary
// emission for .tng/.lgt/.apt, field layouts read directly from
// thing_create_thing()/light_create_light()/actnpoint_create_actnpoint()
// (the *non*-"_adv" classic-path constructors -- these support materially
// fewer fields per thing than the TOML path: no Orientation on any class,
// no creature gold/health%/name, no custom gold-pile amount). `.lof`/`.lif`
// (this class's own display name/freeplay registration) are both written
// -- see the base class's own write_level_info()/write_lif() comments for
// why neither is format-specific.
class ClassicMapContentWriter : public MapContentWriter
{
protected:
    void remove_stale_files(const char *dir, LevelNumber lvnum) override;
    bool write_derived_data(const MapContent &content, const char *dir, LevelNumber lvnum) override;
    bool write_things(const MapContent &content, const char *dir, LevelNumber lvnum) override;
    bool write_lights(const MapContent &content, const char *dir, LevelNumber lvnum) override;
    bool write_action_points(const MapContent &content, const char *dir, LevelNumber lvnum) override;
};

#endif
