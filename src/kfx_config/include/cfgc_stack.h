/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file cfgc_stack.h
 *     Header file for cfgc_stack.cpp.
 * @par Purpose:
 *     docs/refactor/editor/fx-plans/10-config-content-model-and-writers.md
 *     §4.2 -- ConfigStack: the layers of one configuration file (base,
 *     campaign, level) for a ConfigTarget, and the merge that the loader
 *     performs, so a tool can ask "what is the effective value of this key,
 *     which layer set it, and what is beneath it".
 * @par Comment:
 *     Merge rules, all taken from config.c / config_rules.c:
 *       - blocks match by basename + number ("trap02" is "trap2"), named blocks
 *         by exact name; a block that repeats an earlier block's exact name is never read (the
 *         loader finds blocks by name, first one wins), but "trap02" and "trap2" are two blocks;
 *       - a key is overridden key by key; the last line of the highest layer wins;
 *       - a key with several values (SymbolSprites = big med) is overlaid position
 *         by position: a shorter line changes only the first values;
 *       - a block a schema marks list_replaces ([research], [sacrifices]) is
 *         replaced whole by the highest layer that contains it;
 *       - a numbered block beyond the base count extends the list.
 *     Mods (the loader's after-base / after-campaign / after-map lists) are not
 *     modelled in this version.
 *     Engine-decoupled (plan 10 §4.1): every directory is explicit.
 */
/******************************************************************************/
#ifndef DK_CFGC_STACK_H
#define DK_CFGC_STACK_H

#include <cstdint>
#include <string>
#include <vector>

#include "cfgc_content.h"
#include "cfgc_schema.h"

enum CfgLayer
{
    CfgLayer_Base = 0,
    CfgLayer_Campaign,
    CfgLayer_Level,
    CfgLayer_Count
};

const char *cfgc_layer_name(CfgLayer layer); // "base", "campaign", "level"

// Where the layers of a file live. Empty directories mean "no such layer".
struct ConfigTarget
{
    std::string base_dir;         // fxdata/
    std::string base_crtr_dir;    // creatrs/ (creature model files)
    std::string campaign_cfg_dir; // the campaign's CONFIGS_LOCATION
    std::string campaign_crtr_dir; // the campaign's CREATURES_LOCATION
    std::string level_dir;        // the directory of the level's map%05d.* files
    int64_t level_number = -1;    // -1: no level layer

    // Path of `file_name` ("trapdoor.cfg") in a layer, or "" when that layer is not part of the target.
    std::string path_for(const std::string &file_name, CfgLayer layer, bool creature_model = false) const;
};

// What the merge gives for one key.
struct CfgEffective
{
    std::vector<std::string> values; // one text per line: a single entry, except for list blocks
    CfgLayer source = CfgLayer_Base; // the highest layer that set it
    bool has_beneath = false;        // a lower layer also set it
    std::vector<std::string> beneath; // the merged values without the source layer
};

class ConfigStack
{
public:
    explicit ConfigStack(const CfgFileSchema *schema = nullptr) : schema_(schema) {}

    // Replaces a layer's content (kind and partial are set by the caller).
    void set_layer(CfgLayer layer, ConfigContent content);
    // Reads a layer from disk: a missing or empty path leaves the layer absent.
    // Returns true if the file was read.
    bool load_layer(CfgLayer layer, const std::string &path);
    // Reads every layer the target has for this file.
    static ConfigStack load(const ConfigTarget &target, const std::string &kind, const std::string &file_name,
        const CfgFileSchema *schema, bool creature_model = false);

    bool has_layer(CfgLayer layer) const { return present_[layer]; }
    const ConfigContent &layer(CfgLayer layer) const { return layers_[layer]; }

    // The blocks of the effective file, in first-seen order, by canonical id ("trap2", "common").
    std::vector<std::string> section_ids() const;
    // Blocks with this basename, sorted by number.
    std::vector<std::string> numbered_section_ids(const std::string &basename) const;
    // Highest number of a numbered block plus one (the count the loader ends with); 0 if none.
    int64_t section_count(const std::string &basename) const;
    // Layers that contain the block.
    std::vector<CfgLayer> layers_defining(const std::string &section_id) const;

    // Effective value of a key, or false if no layer sets it.
    bool effective(const std::string &section_id, const std::string &key, CfgEffective &out) const;
    // The keys any layer sets in the block, in first-seen order.
    std::vector<std::string> keys_of(const std::string &section_id) const;

    // Feeds every layer's Name = lines to the registry, base first.
    void collect_names(CfgNameSets &names) const;

    // "trap02" -> "trap2"; a name without a trailing number is unchanged.
    static std::string canonical_id(const CfgContentSection &s);

private:
    const CfgSectionSpec *spec_for(const std::string &section_id) const;
    // Merged values of a key using layers [0, upto] only. Returns false if none of them set it.
    bool merge_upto(const std::string &section_id, const std::string &key, int upto, std::vector<std::string> &values,
        int &top_layer) const;

    const CfgFileSchema *schema_;
    std::string kind_;
    ConfigContent layers_[CfgLayer_Count];
    bool present_[CfgLayer_Count] = {false, false, false};
};

#endif
