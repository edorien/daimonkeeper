/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file content_struct.h
 *     docs/refactor/editor/fx-plans/03-content-editors-foundation.md §7/§9 -- the logic behind the
 *     structured (form) editors, without any UI: one layer of one config file of a target, the
 *     effective value of every key with where it comes from, pending edits, and a write that goes
 *     through the kfx_config writer (so spacing, comments and no-op elimination follow the same
 *     rules as every other writer).
 * @par Comment:
 *     Internal to kfx_editor. Generic over the schema kind; the Rules editor is the first user.
 */
/******************************************************************************/
#ifndef DK_CONTENT_STRUCT_H
#define DK_CONTENT_STRUCT_H

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cfgc_stack.h"
#include "cfgc_writer.h"

/** What a form row shows for one key. */
struct FieldView
{
    std::string text;         // the value as text ("" when nothing sets it and there is no default)
    bool is_set = false;      // some layer (or a pending edit) sets it; false: the loader's default applies
    CfgLayer source = CfgLayer_Base; // the layer that sets it (this layer while pending)
    bool overridden_here = false;    // this layer's file sets it, or a pending edit does
    bool pending = false;     // changed in the editor, not written yet
    bool has_beneath = false; // a lower layer also sets it
    std::string beneath;      // that value
    bool same_as_beneath = false; // overridden here with the value the layer beneath already gives
};

/** What a list row set (research, sacrifices) shows. */
struct ListView
{
    std::vector<std::string> lines;
    bool is_set = false;
    CfgLayer source = CfgLayer_Base;
    bool overridden_here = false;
    bool pending = false;
};

class StructuredSession
{
public:
    /** Reads the layers up to `layer` of the file. False if the target has no file location for that layer
     *  (or the kind has no schema). Existing pending edits are dropped. */
    bool open(const ConfigTarget &target, const std::string &kind, const std::string &file_name, CfgLayer layer,
        bool creature_model = false);

    bool is_open() const { return open_; }
    CfgLayer layer() const { return layer_; }
    const std::string &kind() const { return kind_; }
    const CfgFileSchema *schema() const { return schema_; }
    /** Base is shown, never written. */
    bool writable() const { return open_ && layer_ != CfgLayer_Base && !path_.empty(); }
    const std::string &path() const { return path_; }
    bool file_exists() const { return exists_; }

    FieldView value_of(const std::string &section, const std::string &key) const;
    ListView list(const std::string &section, const std::string &key) const;

    /** The numbered blocks of one basename ("trap", "door") any layer up to this one has, sorted by number. */
    std::vector<std::string> section_ids(const std::string &basename) const { return stack_.numbered_section_ids(basename); }
    /** True if this layer's file has the block, or an edit to it is pending. */
    bool section_touched(const std::string &section) const;

    /** Pending edits. `set` of a value equal to the current layer's own value is dropped again. */
    void set(const std::string &section, const std::string &key, const std::string &text);
    void reset(const std::string &section, const std::string &key);
    void set_list(const std::string &section, const std::string &key, const std::vector<std::string> &lines);
    void reset_list(const std::string &section, const std::string &key);
    bool is_pending(const std::string &section, const std::string &key) const;

    bool dirty() const { return !pending_.empty(); }
    size_t pending_count() const { return pending_.size(); }
    void discard() { pending_.clear(); }

    /** Names defined by this target's configuration, all files, layers up to this one (for drop-downs). */
    const CfgNameSets &names() const { return names_; }

    /** Problems the pending values would give the loader (ranges, names, ...). */
    std::vector<CfgDiagnostic> diagnostics() const;

    /** The pending edits as a ChangeSet. */
    ChangeSet changes() const;

    /** Writes the pending edits (through the kind's writer, atomically) and re-reads the layers.
     *  Returns false with `error` set if nothing was written. `warnings` (optional) counts diagnostics of
     *  warning level or above. */
    bool apply(std::string *error, size_t *warnings = nullptr);

private:
    struct Pending
    {
        bool is_reset = false;
        bool is_list = false;
        std::string key_text;             // the key as the caller spelled it
        std::vector<std::string> values; // Set: one entry; list: the lines
    };
    typedef std::pair<std::string, std::string> Key;

    static Key make_key(const std::string &section, const std::string &key);
    bool load_layers();
    // The key as the files already spell it (any block of that kind), else as the caller did.
    std::string spelled(const std::string &section, const std::string &key) const;

    ConfigTarget target_;
    std::string kind_;
    std::string file_name_;
    bool creature_ = false;
    CfgLayer layer_ = CfgLayer_Level;
    std::string path_;
    bool exists_ = false;
    bool open_ = false;
    const CfgFileSchema *schema_ = nullptr;
    ConfigStack stack_;                // layers up to and including `layer_`, as on disk
    ConfigStack lower_;                // layers below `layer_`
    CfgNameSets names_;
    std::map<Key, Pending> pending_;
};

#endif
