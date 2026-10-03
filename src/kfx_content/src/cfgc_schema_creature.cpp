/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file cfgc_schema_creature.cpp
 *     The curated schema of the creature files (plan 04 §4, plan 06 §4): the per-creature model files
 *     (`creatrs/<name>.cfg`, kind "creaturemodel") and `creature.cfg` (kind "creature"). Their loaders are
 *     hand-written, so there is no field table to reflect; the keys come from the exported key tables and
 *     each key's value shape from this table, checked against every shipped file by the schema coverage test.
 * @par Comment:
 *     Shape language of a key, one token per value: `N` number, `S` text, `E:reg` a name from a registry,
 *     `E:reg|NULL` the same with literal extras, `F:reg` / `F:table` a list of names (flags), `T:table` one
 *     name from a static table, `X*n` repeat a token n times, and a trailing `...` lets the last token repeat.
 */
#include "pre_inc.h"
#include "cfgc_schema.h"
#include "cfgc_schema_shapes.h"

#include <algorithm>
#include <cctype>
#include <cinttypes>
#include <cstdarg>
#include <cstring>
#include <map>
#include <sstream>

extern "C" {
#include "config.h"
#include "config_creature.h"
#include "config_crtrmodel.h"
#include "config_magic.h"
extern const struct NamedCommand creaturetype_instance_properties[];
extern const struct NamedCommand creaturetype_job_assign[];
extern const struct NamedCommand creaturetype_job_properties[];
extern const struct NamedCommand instance_range_desc[];
extern const struct NamedCommand spell_effect_flags[];
extern const struct NamedCommand magic_spell_properties[];
}
#include "post_inc.h"

/******************************************************************************/
namespace {

std::vector<std::string> table_names(const std::string &name);

const struct NamedCommand *table_by_name(const std::string &name)
{
    if (name == "creature_props") return creatmodel_properties_commands;
    if (name == "deathkind") return creature_deathkind_desc;
    if (name == "inst_props") return creaturetype_instance_properties;
    if (name == "job_assign") return creaturetype_job_assign;
    if (name == "job_props") return creaturetype_job_properties;
    if (name == "instance_range") return instance_range_desc;
    if (name == "graphics") return creature_graphics_desc;
    if (name == "spell_effect") return spell_effect_flags;
    if (name == "spell_props") return magic_spell_properties;
    return nullptr;
}

std::vector<std::string> table_names(const std::string &name)
{
    std::vector<std::string> out;
    for (const struct NamedCommand *t = table_by_name(name); t != nullptr && t->name != nullptr; t++)
        out.push_back(t->name);
    return out;
}

struct Shape
{
    const char *section;
    const char *key;
    const char *shape;
};

// Shapes for the keys whose parse function the schema can't read (cfgc_table_row_kind() gives CfgKind_Custom).
// Every other key's shape comes from its NamedField row (refactor pass 3, S04); the "every key has a shape" test in
// cfgc_schema_parser_tables_test.cpp keeps these lists to exactly those keys.
const Shape kModelShapes[] = {
    {"attributes", "Properties", "F:creature_props"}, {"attributes", "SpellImmunity", "F:spell_effect"},
    {"attributes", "HostileTowards", "E:creature|NULL ..."},

    {"attraction", "EntranceRoom", "E:room|NULL E:room|NULL E:room|NULL"}, {"attraction", "RoomSlabsRequired", "N N N"},

    {"annoyance", "LairEnemy", "F:creature|NULL"},

    {"experience", "Powers", "E:instance|NULL*10"}, {"experience", "PowersLevelRequired", "N*10"},
    {"experience", "LevelsTrainValues", "N*9"}, {"experience", "GrowUp", "N E:creature|NULL N"},
    {"experience", "SleepExperience", "text"},

    {"senses", "MaxAngleChange", "N"},

    {"appearance", "TransparencyFlags", "N"},

    {"sprites", "QuerySymbol", "I"}, {"sprites", "HandSymbol", "I"},

    {"sounds", "Hit", "N N"}, {"sounds", "Happy", "N N"}, {"sounds", "Sad", "N N"}, {"sounds", "Hang", "N N"},
    {"sounds", "Drop", "N N"}, {"sounds", "Torture", "N N"}, {"sounds", "Slap", "N N"}, {"sounds", "Die", "N N"},
    {"sounds", "Foot", "N N"}, {"sounds", "Fight", "N N"}, {"sounds", "Piss", "N N"},
};

const Shape kMagicShapes[] = {
    {"spell", "Name", "S"}, {"spell", "SelfCasted", "N S N"}, {"spell", "ShotModel", "E:shot"}, {"spell", "EffectModel", "S"},
    {"spell", "SymbolSprites", "I I"}, {"spell", "SpellPower", "S"}, {"spell", "AuraEffect", "S"},
    {"spell", "SpellFlags", "F:spell_effect"}, {"spell", "SummonCreature", "S N N"}, {"spell", "CleanseFlags", "F:spell_effect"},
    {"spell", "Properties", "F:spell_props"},

    {"special", "Artifact", "E:object"}, {"special", "SpeechPlayed", "S"},
};

const Shape kCreatureShapes[] = {
    {"common", "Creatures", "text"},

    {"instance", "Name", "S"}, {"instance", "SymbolSprites", "I"}, {"instance", "Graphics", "T:graphics"},
    {"instance", "Function", "text"}, {"instance", "RangeMin", "R:instance_range"}, {"instance", "RangeMax", "R:instance_range"},
    {"instance", "NoAnimationLoop", "N"}, {"instance", "ValidateSourceFunc", "text"}, {"instance", "ValidateTargetFunc", "text"},
    {"instance", "SearchTargetsFunc", "text"},

    {"job", "RelatedRoomRole", "S"}, {"job", "RelatedEvent", "S"}, {"job", "Assign", "F:job_assign"},
    {"job", "InitialState", "S"}, {"job", "ContinueState", "S"}, {"job", "PlayerFunctions", "text"},
    {"job", "CoordsFunctions", "text"}, {"job", "Properties", "F:job_props"},
};

struct ShapeList
{
    const char *kind;
    const Shape *shapes;
    size_t count;
};

const ShapeList kShapeLists[] = {
    {"creaturemodel", kModelShapes, sizeof(kModelShapes) / sizeof(kModelShapes[0])},
    {"magic", kMagicShapes, sizeof(kMagicShapes) / sizeof(kMagicShapes[0])},
    {"creature", kCreatureShapes, sizeof(kCreatureShapes) / sizeof(kCreatureShapes[0])},
};

void apply_shapes(CfgFileSchema &file, const Shape *shapes, size_t count)
{
    for (size_t i = 0; i < count; i++)
    {
        CfgSectionSpec *sec = file.find_section(shapes[i].section);
        CfgFieldSpec *f = sec != nullptr ? sec->find_field(shapes[i].key) : nullptr;
        if (f != nullptr)
            cfgc_apply_shape(*f, shapes[i].shape, table_names);
    }
}

} // namespace

bool cfgc_creature_shape_override(const std::string &kind, const std::string &section, const std::string &key)
{
    for (const ShapeList &list : kShapeLists)
        if (kind == list.kind)
            for (size_t i = 0; i < list.count; i++)
                if (strcasecmp(list.shapes[i].section, section.c_str()) == 0 && strcasecmp(list.shapes[i].key, key.c_str()) == 0)
                    return true;
    return false;
}

std::vector<CfgShapeOverride> cfgc_creature_shape_overrides()
{
    std::vector<CfgShapeOverride> out;
    for (const ShapeList &list : kShapeLists)
        for (size_t i = 0; i < list.count; i++)
            out.push_back({list.kind, list.shapes[i].section, list.shapes[i].key});
    return out;
}

void describe_creature_schemas(ConfigSchema &schema)
{
    CfgFileSchema f;
    f.kind = "creature";
    f.file_name = "creature.cfg";
    cfgc_add_table_fields(cfgc_schema_section(f, "common", false), creaturetype_common_named_fields);
    cfgc_add_table_fields(cfgc_schema_section(f, "experience", false), creaturetype_experience_named_fields);
    cfgc_add_table_fields(cfgc_schema_section(f, "instance", true), creaturetype_instance_named_fields);
    cfgc_add_table_fields(cfgc_schema_section(f, "job", true), creaturetype_job_named_fields);
    cfgc_add_table_fields(cfgc_schema_section(f, "angerjob", true), creaturetype_angerjob_named_fields);
    cfgc_add_table_fields(cfgc_schema_section(f, "attackpref", true), creaturetype_attackpref_named_fields);
    schema.files.push_back(std::move(f));

    for (const ShapeList &list : kShapeLists)
        if (CfgFileSchema *file = schema.find(list.kind))
            apply_shapes(*file, list.shapes, list.count);
}
