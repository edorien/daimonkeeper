/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file content_form.h
 *     docs/refactor/editor/fx-plans/03-content-editors-foundation.md §7 -- the form row every structured
 *     editor (Rules, Trap and Door, later Room, Creature, ...) draws: key label, a widget chosen by the
 *     schema's kind, where the value comes from, and Reset. One implementation, so all editors behave and
 *     look alike.
 * @par Comment:
 *     Internal to kfx_editor. ImGui code (frontgui_widgets wrappers).
 */
#ifndef DK_CONTENT_FORM_H
#define DK_CONTENT_FORM_H

#include <map>
#include <set>
#include <string>
#include <vector>

#include <imgui.h>

#include "content_struct.h"

/** One em of the current font, times `em`: layout positions that follow the UI font scale. */
float form_col(float em);
ImVec4 form_layer_colour(CfgLayer layer);
const char *form_layer_name(CfgLayer layer); // "Base", "Campaign", "Level"
std::string form_lower(std::string s);
bool form_parse_int(const std::string &s, int64_t &out);
std::vector<std::string> form_split_words(const std::string &text);
std::string form_join_words(const std::vector<std::string> &words);

/** A drop-down over `options` for `value`. A value not among them stays selectable, marked "(unknown)". With
 *  no options at all it is a text box. True when the value changed. */
bool form_pick_name(const char *id, std::string &value, const std::vector<std::string> &options, float width);

struct FormContext
{
    StructuredSession *session = nullptr;
    // From the base file: help text by "section/key" and the key's spelling by lower-case key.
    std::map<std::string, std::string> help;
    std::map<std::string, std::string> spelling;
    std::set<std::string> bad; // "section/key" (lower-case) the pending edits give problems, this frame
    bool show_ignored = false; // draw keys that no loader reads
    // Keys whose row is read only whatever the layer (function names, ...): lower-case key names.
    // Per-level arrays that also get a curve under the boxes (lower-case keys); the others are boxes only.
    std::set<std::string> plot_keys;
    std::set<std::string> read_only;

    /** Reads help and key spelling from a base file. */
    void read_reference(const std::string &base_file_path);
    /** Recomputes `bad` from the session's pending edits. Call once per frame. */
    void refresh_problems();
    std::string display_key(const CfgFieldSpec &spec) const;
    /** The help for a key, or "". */
    std::string help_for(const std::string &section, const std::string &key) const;
    /** Draws the row of one key of one block ("game", "trap3"). */
    void draw_row(const std::string &section, const CfgFieldSpec &spec);
    /** A row that only shows a value (label + text), for information taken from elsewhere. */
    void draw_info_row(const std::string &label, const std::string &value, const std::string &note);
};

#endif
