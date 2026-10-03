/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file cfgc_schema_campaign.cpp
 *     The curated schema of a campaign or map pack file (kind "campaign", plan 08 K0): `[common]`, `[strings]`,
 *     `[speech]` and the per-level `[mapNNNNN]` entries. Keys come from the loader's exported key tables, each key's
 *     value shape from the tables below (checked against all 40 shipped campaign and pack files).
 * @par Comment:
 *     Facts the editor relies on (config_campaigns.c): the FIRST language line of `[strings]` is the campaign's
 *     default language; `BONUS_LEVELS` is parallel to `SINGLE_LEVELS` (`0` = no bonus); `ASSIGN_CPU_KEEPERS` takes
 *     ON / OFF; `MULTI_LEVELS` appears in a shipped file but the loader has no such key (ignored).
 */
#include "pre_inc.h"
#include "cfgc_schema.h"
#include "cfgc_schema_shapes.h"

#include <algorithm>
#include <cinttypes>
#include <cstdarg>
#include <cstring>

extern "C" {
#include "config.h"
#include "config_campaigns.h"
#include "config_keeperfx.h"
}
#include "post_inc.h"

/******************************************************************************/
namespace {

std::vector<std::string> table_names(const std::string &name)
{
    std::vector<std::string> out;
    auto list = [&](const struct NamedCommand *t) {
        for (; t != nullptr && t->name != nullptr; t++)
            out.push_back(t->name);
    };
    if (name == "ensign") list(cmpgn_map_ensign_flag_options);
    else if (name == "logicval") list(logicval_type);
    else if (name == "markers")
        out = {"ENSIGNS", "PINPOINTS"};
    else if (name == "colours")
        out = {"RED", "BLUE", "GREEN", "YELLOW", "WHITE", "PURPLE", "BLACK", "ORANGE"};
    return out;
}

struct Shape
{
    const char *section;
    const char *key;
    const char *shape;
};

const Shape kShapes[] = {
    {"common", "NAME", "text"}, {"common", "DESCRIPTION", "text"},
    {"common", "SINGLE_LEVELS", "N ..."}, {"common", "BONUS_LEVELS", "N ..."}, {"common", "EXTRA_LEVELS", "N ..."},
    {"common", "HIGH_SCORES", "N S"}, // how many entries, and the file
    {"common", "LAND_VIEW_START", "S S"}, {"common", "LAND_VIEW_END", "S S"}, // land image, window frame
    {"common", "LAND_AMBIENT", "N N"},
    {"common", "LEVELS_LOCATION", "text"}, {"common", "LAND_LOCATION", "text"}, {"common", "CREATURES_LOCATION", "text"},
    {"common", "CONFIGS_LOCATION", "text"}, {"common", "MEDIA_LOCATION", "text"},
    {"common", "CREDITS", "text"}, {"common", "INTRO_MOVIE", "text"}, {"common", "OUTRO_MOVIE", "text"},
    {"common", "LAND_MARKERS", "T:markers"}, {"common", "HUMAN_PLAYER", "T:colours"}, {"common", "NAME_TEXT_ID", "N"},
    {"common", "ASSIGN_CPU_KEEPERS", "T:logicval"}, {"common", "SOUNDTRACK", "text"},

    {"map", "NAME_TEXT", "text"}, {"map", "NAME_ID", "S"}, // a string id, or an alias of one
    {"map", "ENSIGN_POS", "N N"}, {"map", "ENSIGN_ZOOM", "N N"}, {"map", "PLAYERS", "N"},
    {"map", "ENSIGN", "F:ensign"}, {"map", "OPTIONS", "F:ensign"}, {"map", "SPEECH", "S S"}, {"map", "LAND_VIEW", "S S"},
    {"map", "MAPSIZE", "N N"},
};

} // namespace

void describe_campaign_schema(ConfigSchema &schema)
{
    CfgFileSchema f;
    f.kind = "campaign";
    f.file_name = "<campaign>.cfg";

    auto add_keys = [&](const char *section, bool numbered, const struct NamedCommand *table) {
        f.sections.push_back(CfgSectionSpec());
        CfgSectionSpec &s = f.sections.back();
        s.basename = section;
        s.numbered = numbered;
        for (const struct NamedCommand *c = table; c != nullptr && c->name != nullptr; c++)
        {
            if (s.find_field(c->name) != nullptr)
                continue; // OPTIONS is the legacy name of ENSIGN: both are keys
            CfgFieldSpec k;
            k.key = c->name;
            CfgValueSpec p;
            p.kind = CfgKind_Unspecified;
            k.parts.push_back(p);
            s.fields.push_back(std::move(k));
        }
    };
    add_keys("common", false, cmpgn_common_commands);
    add_keys("map", true, cmpgn_map_commands);

    // [strings] and [speech]: one key per language, the value is a path.
    for (const char *block : {"strings", "speech"})
    {
        f.sections.push_back(CfgSectionSpec());
        CfgSectionSpec &s = f.sections.back();
        s.basename = block;
        for (const struct NamedCommand *c = lang_type; c->name != nullptr; c++)
        {
            CfgFieldSpec k;
            k.key = c->name;
            k.whole_string = true;
            CfgValueSpec p;
            p.kind = CfgKind_Custom;
            k.parts.push_back(p);
            s.fields.push_back(std::move(k));
        }
    }

    for (const Shape &sh : kShapes)
    {
        CfgSectionSpec *sec = f.find_section(sh.section);
        CfgFieldSpec *field = sec != nullptr ? sec->find_field(sh.key) : nullptr;
        if (field == nullptr && sec != nullptr)
        {
            sec->fields.push_back(CfgFieldSpec());
            field = &sec->fields.back();
            field->key = sh.key;
        }
        if (field != nullptr)
            cfgc_apply_shape(*field, sh.shape, table_names);
    }
    schema.files.push_back(std::move(f));
}
