/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file content_strings.h
 *     docs/refactor/editor/fx-plans/09-text-strings-editor.md -- the logic behind the Text editor, without any UI: one
 *     language of a target's string files (base, the campaign's, the level's), the effective text of every id with the
 *     layer it comes from, pending edits, and an atomic write of the edited layer's file.
 * @par Comment:
 *     Internal to kfx_editor. Built on cfgc_strings (byte-preserving entries, code-page codec, stack): entries the
 *     author does not touch are written back byte for byte.
 */
#ifndef DK_CONTENT_STRINGS_H
#define DK_CONTENT_STRINGS_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "cfgc_strings.h"
#include "cfgc_stack.h" // CfgLayer
#include "content_target.h"

/** Ids beyond this are never read by the game (STRINGS_MAX). */
const size_t kStringsMax = 2000;

struct StringsPaths
{
    std::string base;     // fxdata/gtext_<lang>.dat
    std::string campaign; // the campaign's [strings] path for the language, "" if it has none
    std::string level;    // map%05d.<lang>.dat beside the level, "" without a level
};

/** Where the string files of a language are for a campaign (may be null) and a level (level_number < 0: none). */
StringsPaths content_strings_paths(const std::string &root, const ContentCampaign *campaign, const std::string &level_dir,
    int64_t level_number, const std::string &lang);

/** Languages offered: those with a base file in `<root>/fxdata`, plus the ones the campaign names. Sorted, "eng" first. */
std::vector<std::string> content_strings_languages(const std::string &root, const ContentCampaign *campaign);

/** Where each string id is used: "map00300 name", "map00300 script". From the campaign file's NAME_IDs and the
 *  DISPLAY_OBJECTIVE / DISPLAY_INFORMATION commands of the scripts of `levels` (in `levels_dir`). */
std::map<int64_t, std::vector<std::string>> content_strings_usage(const ContentCampaign *campaign, const std::string &levels_dir,
    const std::vector<int64_t> &levels);


class StringsSession
{
public:
    bool open(const StringsPaths &paths, const std::string &lang, CfgLayer layer);

    bool is_open() const { return open_; }
    CfgLayer layer() const { return layer_; }
    const std::string &lang() const { return lang_; }
    /** The file of the layer being edited ("" if there is none for it). */
    const std::string &path() const { return path_; }
    bool file_exists() const { return exists_; }
    /** False for the base layer, a language with a multi-byte code page, or a layer without a file. */
    bool writable() const;
    std::string why_read_only() const;

    /** One past the highest id any layer, or a pending edit, has. */
    size_t id_count() const;

    struct View
    {
        std::string text;            // the effective text, UTF-8, line breaks as "\n"
        bool found = false;          // some layer has a non-empty entry
        CfgLayer source = CfgLayer_Base;
        bool overridden_here = false; // this layer's file (or a pending edit) sets it
        bool pending = false;
        bool has_beneath = false;
        std::string beneath;
    };
    View view(size_t id) const;
    /** This layer's own text for the id ("" if it has none), pending edit applied. */
    std::string own_text(size_t id) const;

    /** Edits (text in UTF-8 with "\n" line breaks; stored as CR LF like the shipped files). An edit equal to what the
     *  layer already has vanishes. `reset` blanks the entry (the layer beneath shows through). */
    void set_text(size_t id, const std::string &text);
    void reset(size_t id);
    bool is_pending(size_t id) const { return pending_.count(id) > 0; }
    bool dirty() const { return !pending_.empty(); }
    size_t pending_count() const { return pending_.size(); }
    void discard() { pending_.clear(); }

    /** The smallest id from 1 that no layer defines and no edit uses: where "Add string" puts a new one. */
    size_t next_free_id() const;

    struct Problem
    {
        size_t id = 0;
        std::string message;
    };
    /** What is wrong with the pending edits (characters the code page cannot hold, ids the game never reads). */
    std::vector<Problem> diagnostics() const;

    /** Writes the edited layer's file atomically and re-reads it. False with `error` if nothing was written. */
    bool apply(std::string *error);

private:
    bool load_layers();

    StringsPaths paths_;
    std::string lang_;
    CfgLayer layer_ = CfgLayer_Level;
    std::string path_;
    bool exists_ = false;
    bool open_ = false;
    StringsStack stack_;
    std::map<size_t, std::string> pending_; // id -> new text (UTF-8, "\n"); "" resets
};

#endif
