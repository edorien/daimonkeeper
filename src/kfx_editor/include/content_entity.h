/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file content_entity.h
 *     docs/refactor/editor/fx-plans/03-content-editors-foundation.md §4 -- the window shared by the entity editors
 *     (Trap and Door, Spell and Ability, later Room, Creature): a target picker, a switch between the kinds of
 *     entity a file holds (traps | doors, powers | spells | shots | specials), a list of the entities with layer
 *     markers, tabs of grouped form rows, read-only summaries of what an entity links to, and a table comparing
 *     them all. One implementation; each editor is a configuration of it.
 * @par Comment:
 *     Internal to kfx_editor. ImGui code (frontgui_widgets wrappers) over StructuredSession and FormContext.
 */
#ifndef DK_CONTENT_ENTITY_H
#define DK_CONTENT_ENTITY_H

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "content_form.h"
#include "content_picker.h"
#include "content_struct.h"

/** A read-only summary of another entity that one key of this one names: a trap's EffectType names a shot. */
struct EntityLink
{
    std::string mode_basename;   // the entity kind that has the key ("trap")
    std::string key;             // "EffectType"
    std::string group;           // the tab the summary is drawn under
    std::string kind;            // schema kind of the file the target is in ("magic")
    std::string file_name;       // "magic.cfg"
    std::string target_basename; // "shot"
    int64_t key_word = -1;       // the name is this word of the value (a Function's argument); -1: the whole value
    bool optional = false;       // draw nothing when the name is not found (an argument that may be a shot or a spell)
    std::string title;           // "Fires"
    std::vector<std::string> show; // keys of the target to show
    std::string note;            // shown under the summary
};

/** A column of the comparison table that comes from a link's target ("Shot damage"). */
struct EntityLinkedColumn
{
    size_t link = 0;      // index into EntityConfig::links
    std::string key;      // key of the target ("Damage")
    std::string heading;
};

struct EntityMode
{
    std::string label;    // "Traps"
    std::string kind;     // the schema kind and file when this mode's entities live in another file than the
    std::string file_name; // editor's main one (spell editor: abilities are in creature.cfg); empty: the editor's own
    std::string basename; // "trap"
    std::vector<std::string> groups; // tab names, in order
    std::function<std::string(const std::string &key)> group_of; // key -> tab name
    // Comparison columns: a key, or "Key[0]" / "Key[last]" for one word of a multi-value key.
    std::vector<std::string> compare_keys;
    std::vector<EntityLinkedColumn> compare_linked;
    // Keys whose value should be unique among the entities of this kind (a panel position), warned about under `unique_group`.
    std::vector<std::string> unique_keys;
    std::string unique_group;
    // A named block that is one item rather than a numbered list ([block_health]): the list has just it.
    bool single = false;
    // A note drawn at the top of a tab ("changes how every slab of this kind behaves").
    std::map<std::string, std::string> group_notes;
};

struct EntityConfig
{
    std::string title;   // "Trap and Door Editor"
    std::string imgui_id; // "##ContentTrapDoor"
    std::string kind;    // schema kind ("trapdoor")
    std::string file_name;
    std::vector<EntityMode> modes;
    std::vector<EntityLink> links;
    std::set<std::string> read_only; // lower-case keys drawn read only, in every file
    std::set<std::string> plot_keys; // lower-case per-level array keys drawn with a curve too
    std::string empty_text; // shown before a target is picked
};

class EntityEditor
{
public:
    explicit EntityEditor(EntityConfig config) : cfg_(std::move(config)) {}

    /** Opens the window; `map_host`: on the map being edited. */
    void open(bool map_host);
    void frame();
    bool is_open() const { return open_; }
    /** Brings a mode tab and a group tab ("Compare all" included) to the front on the next frame. */
    void show(size_t mode, const std::string &group);

private:
    // One open file: its session and its form helper (help and key spelling from the base file).
    struct Bundle
    {
        StructuredSession session;
        FormContext form;
        bool loaded = false;
        bool reference_read = false;
    };
    void load();
    void draw_window();
    void draw_mode(size_t mode, float body_h);
    void draw_group(const EntityMode &m, Bundle &b, const std::string &group, const std::string &id);
    void draw_links(const EntityMode &m, Bundle &b, const std::string &group, const std::string &id);
    void draw_compare(const EntityMode &m, Bundle &b, const std::vector<std::string> &ids);
    Bundle &bundle_for(const EntityMode &m);
    bool any_dirty() const;
    std::string title_of(Bundle &b, const std::string &id) const;
    static std::string link_name(Bundle &b, const std::string &id, const EntityLink &l);
    std::string linked_id(const EntityLink &l, const std::string &name) const;
    std::string linked_value(const EntityLink &l, const std::string &target_id, const std::string &key) const;
    const ConfigStack *aux_for(const EntityLink &l) const;

    EntityConfig cfg_;
    bool open_ = false;
    ContentPicker pick_;
    std::map<std::string, Bundle> bundles_; // by file name
    std::string loaded_signature_;
    std::string status_;
    bool loaded_ = false;
    std::vector<int64_t> selected_; // per mode: the list row
    std::map<std::string, std::unique_ptr<ConfigStack>> aux_; // per linked file name
    int64_t force_mode_ = -1;
    std::string force_group_;
};

#endif
