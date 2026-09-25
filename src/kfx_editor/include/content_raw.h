/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file content_raw.h
 *     docs/refactor/editor/fx-plans/03-content-editors-foundation.md F3 -- the raw config editor's
 *     logic without any UI: pick a file of a target, edit one layer of it as text, validate, see the
 *     effective values (the text taking the place of that layer), and write it back atomically.
 * @par Comment:
 *     Internal to kfx_editor. Built on the kfx_config content layer (ConfigDocument, ConfigStack,
 *     cfgc_validate_document, WriteBatch); the base layer is shown but never written.
 */
/******************************************************************************/
#ifndef DK_CONTENT_RAW_H
#define DK_CONTENT_RAW_H

#include <string>
#include <vector>

#include "cfgc_stack.h"
#include "cfgc_validate.h"

struct RawFileEntry
{
    std::string name;            // as shown: "trapdoor.cfg", "creatrs/imp.cfg"
    std::string file_name;       // the file name in its layer directory: "trapdoor.cfg", "imp.cfg"
    bool creature_model = false; // lives in the creature directories, not the config ones
    std::string kind;            // schema kind, empty if the file has no schema (plain text)
};

/** Every *.cfg / *.toml in the base config directory plus every creature model file, sorted. */
std::vector<RawFileEntry> content_raw_list_files(const ConfigTarget &target);

/** One row of the effective view. */
struct RawEffectiveRow
{
    std::string section; // canonical id
    std::string key;
    std::string value;   // merged text ("; " joined for list keys)
    CfgLayer source = CfgLayer_Base;
    bool has_beneath = false;
    std::string beneath;
    bool in_layer = false; // the edited text sets this key
};

class RawConfigSession
{
public:
    /** Reads the layer's file (or starts an empty one). False if the file has no directory in this target. */
    bool open(const ConfigTarget &target, const RawFileEntry &file, CfgLayer layer);

    bool is_open() const { return open_; }
    const RawFileEntry &file() const { return file_; }
    CfgLayer layer() const { return layer_; }
    /** Full path of the file being edited, "" if the layer has none in this target. */
    const std::string &path() const { return path_; }
    /** The file exists on disk. */
    bool exists() const { return exists_; }
    /** Base files are shown, never written. */
    bool writable() const { return open_ && layer_ != CfgLayer_Base && !path_.empty(); }
    bool dirty() const { return text_ != original_; }

    std::string &text() { return text_; }
    const std::string &text() const { return text_; }
    void set_text(const std::string &t) { text_ = t; }
    void revert() { text_ = original_; }

    /** Problems the loader would meet reading the text as this layer. Never blocks a write. */
    std::vector<CfgDiagnostic> validate() const;

    /** The merged values with the text as the edited layer. `only_layer`: just the keys the text sets. */
    std::vector<RawEffectiveRow> effective_rows(bool only_layer) const;

    /** Writes the text (an empty text deletes the file). False with `error` set if nothing was written. */
    bool apply(std::string *error);
    /** Deletes the layer's file, keeping the text in the editor as an unsaved file. */
    bool delete_file(std::string *error);

    /** Schema keys of this file kind, for syntax colouring. */
    std::vector<std::string> known_keys() const;

private:
    ConfigStack build_stack() const;
    const CfgFileSchema *schema() const;

    ConfigTarget target_;
    RawFileEntry file_;
    CfgLayer layer_ = CfgLayer_Level;
    std::string path_;
    std::string original_;
    std::string text_;
    bool exists_ = false;
    bool open_ = false;
};

#endif
