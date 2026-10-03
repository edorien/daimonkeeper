/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file cfgc_schema_engine.cpp
 *     Reflection of the engine's field tables into a ConfigSchema (plan 10 W2).
 * @par Comment:
 *     Reads only const tables (NamedField arrays, exported NamedCommand key
 *     tables). The dynamic name registries (door_desc, object_desc, ...) are
 *     filled at load time, so their contents are never read here; a field that
 *     uses one is recorded as "names come from registry X".
 */
#include "pre_inc.h"
#include "cfgc_schema.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstring>
#include <map>

extern "C" {
#include "config.h"
#include "config_creature.h"
#include "config_trapdoor.h"
#include "config_rules.h"
#include "config_objects.h"
#include "config_terrain.h"
#include "config_magic.h"
#include "config_effects.h"
#include "config_lenses.h"
#include "config_cubes.h"
#include "config_crtrstates.h"
#include "config_powerhands.h"
#include "config_players.h"

// Dynamic name tables with no header declaration of their own (defined non-const in the loaders).
extern "C" {
extern struct NamedCommand special_desc[];
extern struct NamedCommand cube_desc[];
}
}
#include "post_inc.h"

/******************************************************************************/
namespace {

struct DynamicRegistry
{
    const struct NamedCommand *table;
    const char *registry;
};

// The name tables the loaders fill from each section's "Name =" (plan 10 F4).
const DynamicRegistry *dynamic_registries(size_t &count)
{
    static const DynamicRegistry list[] = {
        {creature_desc, "creature"}, {trap_desc, "trap"},         {door_desc, "door"},
        {slab_desc, "slab"},         {room_desc, "room"},         {object_desc, "object"},
        {shot_desc, "shot"},         {spell_desc, "spell"},       {power_desc, "power"},
        {instance_desc, "instance"}, {effect_desc, "effect"},     {effectgen_desc, "effectgen"},
        {effectelem_desc, "effectelem"},   {lenses_desc, "lenses"},
        {powerhand_desc, "powerhand"}, {creatrstate_desc, "creatrstate"}, {player_state_commands, "playerstate"},
        {special_desc, "special"}, {cube_desc, "cube"},
        {angerjob_desc, "angerjob"}, {creaturejob_desc, "creaturejob"}, {attackpref_desc, "attackpref"},
    };
    count = sizeof(list) / sizeof(list[0]);
    return list;
}

const char *registry_of(const struct NamedCommand *table)
{
    size_t n = 0;
    const DynamicRegistry *list = dynamic_registries(n);
    for (size_t i = 0; i < n; i++)
        if (list[i].table == table)
            return list[i].registry;
    return nullptr;
}

int64_t datatype_min(uchar type)
{
    switch (type)
    {
    case dt_uchar: case dt_ushort: case dt_uint: case dt_ulong: case dt_ulonglong: return 0;
    case dt_schar: return SCHAR_MIN;
    case dt_char: return CHAR_MIN;
    case dt_short: return SHRT_MIN;
    case dt_int: return INT_MIN;
    default: return INT64_MIN;
    }
}

int64_t datatype_max(uchar type)
{
    switch (type)
    {
    case dt_uchar: return UCHAR_MAX;
    case dt_schar: return SCHAR_MAX;
    case dt_char: return CHAR_MAX;
    case dt_short: return SHRT_MAX;
    case dt_ushort: return USHRT_MAX;
    case dt_int: return INT_MAX;
    case dt_uint: return UINT_MAX;
    default: return INT64_MAX;
    }
}

CfgFieldKind kind_of(const struct NamedField &f)
{
    if (f.parse_func == value_default) return f.namedCommand != nullptr ? CfgKind_Enum : CfgKind_Number;
    if (f.parse_func == value_name) return CfgKind_Text;
    if (f.parse_func == value_flagsfield || f.parse_func == value_longflagsfield) return CfgKind_Flags;
    if (f.parse_func == value_icon) return CfgKind_Icon;
    if (f.parse_func == value_animid) return CfgKind_AnimId;
    if (f.parse_func == value_stringId) return CfgKind_StringId;
    if (f.parse_func == value_effOrEffEl) return CfgKind_EffectRef;
    if (f.parse_func == value_function) return CfgKind_Function;
    if (f.parse_func == value_stltocoord) return CfgKind_Coord;
    return CfgKind_Custom;
}

void fill_names(const struct NamedField &f, CfgValueSpec &part)
{
    if (f.namedCommand == nullptr)
        return;
    if (f.parse_func == value_function)
        return; // the table names Lua callback slots, not values
    if (const char *reg = registry_of(f.namedCommand))
    {
        part.enum_registry = reg;
        return;
    }
    if (f.parse_func == value_longflagsfield)
    {
        for (const struct LongNamedCommand *c = (const struct LongNamedCommand *)f.namedCommand; c->name != nullptr; c++)
            part.enum_names.push_back(c->name);
        return;
    }
    for (const struct NamedCommand *c = f.namedCommand; c->name != nullptr; c++)
        part.enum_names.push_back(c->name);
}

void add_table_fields(CfgSectionSpec &sec, const struct NamedField *fields)
{
    // The loader assigns the words of a line to the consecutive rows of one name in table order (argnum only selects
    // the first row; a table may write it wrongly, as door SLABKIND does), so a value's position is the row's order.
    std::map<std::string, size_t> seen;
    for (const struct NamedField *f = fields; f != nullptr && f->name != nullptr; f++)
    {
        CfgFieldSpec *spec = sec.find_field(f->name);
        if (spec == nullptr)
        {
            // A row that writes the same struct field as an earlier row of another name is an alias of it.
            std::string alias_of;
            if (f->field != nullptr)
                for (const struct NamedField *g = fields; g != f; g++)
                    if (g->field == f->field && g->argnum == f->argnum && strcasecmp(g->name, f->name) != 0)
                    {
                        alias_of = g->name;
                        break;
                    }
            sec.fields.push_back(CfgFieldSpec());
            spec = &sec.fields.back();
            spec->key = f->name;
            spec->alias_of = alias_of;
        }
        std::string upper_name;
        for (const char *c = f->name; *c != '\0'; c++)
            upper_name += (char)std::toupper((unsigned char)*c);
        const size_t idx = f->argnum < 0 ? 0 : seen[upper_name]++;
        if (f->argnum < 0)
            spec->whole_string = true;
        if (spec->parts.size() <= idx)
            spec->parts.resize(idx + 1);
        CfgValueSpec &part = spec->parts[idx];
        part.kind = kind_of(*f);
        part.min = std::max<int64_t>(f->min, datatype_min(f->type));
        part.max = std::min<int64_t>(f->max, datatype_max(f->type));
        part.default_value = f->default_value;
        fill_names(*f, part);
    }
}

CfgSectionSpec &section(CfgFileSchema &file, const std::string &basename, bool numbered)
{
    CfgSectionSpec *s = file.find_section(basename);
    if (s == nullptr)
    {
        file.sections.push_back(CfgSectionSpec());
        s = &file.sections.back();
        s->basename = basename;
        s->numbered = numbered;
    }
    return *s;
}

void add_key_table(CfgSectionSpec &sec, const struct NamedCommand *table, CfgFieldKind kind = CfgKind_Unspecified)
{
    for (const struct NamedCommand *c = table; c != nullptr && c->name != nullptr; c++)
    {
        if (sec.find_field(c->name) != nullptr)
            continue;
        CfgFieldSpec spec;
        spec.key = c->name;
        CfgValueSpec part;
        part.kind = kind;
        spec.parts.push_back(part);
        sec.fields.push_back(std::move(spec));
    }
}

// A recipe line is "<result> <victim> <victim> ...": the result is a creature, a spell or a unique function
// depending on the key; the victims are creatures (up to six).
void describe_sacrifices(CfgSectionSpec &sec)
{
    for (CfgFieldSpec &f : sec.fields)
    {
        std::string k;
        for (char c : f.key)
            k += (char)std::toupper((unsigned char)c);
        CfgValueSpec result;
        result.kind = CfgKind_Enum;
        if (k == "MKCREATURE" || k == "MKGOODHERO")
            result.enum_registry = "creature";
        else if (k == "NEGSPELLALL" || k == "POSSPELLALL")
            result.enum_registry = "spell";
        else if (k == "NEGUNIQFUNC" || k == "POSUNIQFUNC")
        {
            for (const struct NamedCommand *c = sacrifice_unique_desc; c->name != nullptr; c++)
                result.enum_names.push_back(c->name);
        }
        else
            continue; // CustomReward / CustomPunish: not described
        CfgValueSpec victim;
        victim.kind = CfgKind_Enum;
        victim.enum_registry = "creature";
        f.parts.clear();
        f.parts.push_back(result);
        f.parts.push_back(victim);
        f.repeat_last = true;
    }
}

void add_table_set(CfgFileSchema &file, const struct NamedFieldSet &set)
{
    add_table_fields(section(file, set.block_basename, true), set.named_fields);
}

// Keys that appear in shipped files but that no loader of this build reads (plan 10 §8a.2). They are
// recorded, not dropped, so an editor can say "no effect" instead of offering a control that does nothing.
struct IgnoredKey
{
    const char *kind;
    const char *section;
    const char *key;
    const char *note;
};

const char *const kCountNote = "the count is derived from the numbered blocks; the loader does not read it";
const char *const kModNote = "used by community configs; this build does not read it";

const IgnoredKey kIgnoredKeys[] = {
    {"trapdoor", "common", "TrapsCount", kCountNote},
    {"trapdoor", "common", "DoorsCount", kCountNote},
    {"terrain", "common", "RoomsCount", kCountNote},
    {"terrain", "common", "SlabsCount", kCountNote},
    {"objects", "common", "ObjectsCount", kCountNote},
    {"magic", "common", "SpellsCount", kCountNote},
    {"magic", "common", "ShotsCount", kCountNote},
    {"magic", "common", "PowerCount", kCountNote},
    {"rules", "rooms", "BarrackTime", "shipped in base rules.cfg; the loader accepts it but nothing uses it"},
    {"rules", "game", "GemEffectiveness", kModNote},
    {"rules", "game", "GoldPerGoldBlock", kModNote},
    {"rules", "health", "GameTurnsPerPrisonHealthGain", kModNote},
    {"rules", "health", "PrisonHealthGain", kModNote},
    {"rules", "magic", "ArmegeddonTeleportNeutrals", kModNote},
    {"creaturemodel", "attraction", "EntranceForce", kModNote},
    {"creaturemodel", "sounds", "Hurt", kModNote},
    {"creaturemodel", "sprites", "GFX18", kModNote},
    {"creaturemodel", "sprites", "GFX21", kModNote},
    {"creature", "common", "InstancesCount", "shipped in creature.cfg of some campaigns; the loader derives the count from the blocks"},
    {"creature", "common", "SwapCreatures", kModNote},
    {"campaign", "common", "MULTI_LEVELS", "shipped in a multiplayer pack; the loader has no such key (the levels come from the folder)"},
    {"magic", "shot", "DamageType", kModNote},
    {"magic", "power", "Function", kModNote},
    {"magic", "power", "Functions", kModNote},
    {"magic", "spell", "Power", kModNote},
    {"magic", "spell", "Transform", kModNote},
};

// Aliases are found by table order (the later row is the alias); where the older spelling comes first
// the base files' own spelling is made canonical here.
struct PreferredKey
{
    const char *kind;
    const char *section;
    const char *key;
};

const PreferredKey kPreferredKeys[] = {
    {"objects", "object", "Size_Z"}, // Size_YZ is the older spelling; every base object uses Size_Z
};

void apply_preferred_keys(ConfigSchema &schema)
{
    for (const PreferredKey &pk : kPreferredKeys)
    {
        CfgFileSchema *file = schema.find(pk.kind);
        CfgSectionSpec *sec = file != nullptr ? file->find_section(pk.section) : nullptr;
        CfgFieldSpec *preferred = sec != nullptr ? sec->find_field(pk.key) : nullptr;
        if (preferred == nullptr || preferred->alias_of.empty())
            continue;
        CfgFieldSpec *old = sec->find_field(preferred->alias_of);
        if (old == nullptr)
            continue;
        old->alias_of = preferred->key;
        preferred->alias_of.clear();
    }
}

void apply_ignored_keys(ConfigSchema &schema)
{
    for (const IgnoredKey &ik : kIgnoredKeys)
    {
        CfgFileSchema *file = schema.find(ik.kind);
        if (file == nullptr)
            continue;
        CfgSectionSpec &sec = section(*file, ik.section, false);
        CfgFieldSpec *spec = sec.find_field(ik.key);
        if (spec == nullptr)
        {
            sec.fields.push_back(CfgFieldSpec());
            spec = &sec.fields.back();
            spec->key = ik.key;
        }
        spec->state = CfgState_Ignored;
        spec->note = ik.note;
    }
}

} // namespace

void describe_creature_schemas(ConfigSchema &schema); // cfgc_schema_creature.cpp
void describe_campaign_schema(ConfigSchema &schema);  // cfgc_schema_campaign.cpp

ConfigSchema build_engine_schema()
{
    ConfigSchema schema;

    {
        CfgFileSchema f;
        f.kind = "trapdoor";
        f.file_name = "trapdoor.cfg";
        add_table_set(f, trapdoor_trap_named_fields_set);
        add_table_set(f, trapdoor_door_named_fields_set);
        schema.files.push_back(std::move(f));
    }
    {
        CfgFileSchema f;
        f.kind = "terrain";
        f.file_name = "terrain.cfg";
        add_table_set(f, terrain_slab_named_fields_set);
        add_table_set(f, terrain_room_named_fields_set);
        add_key_table(section(f, "block_health", false), terrain_health_commands, CfgKind_Number);
        schema.files.push_back(std::move(f));
    }
    {
        CfgFileSchema f;
        f.kind = "objects";
        f.file_name = "objects.cfg";
        add_table_set(f, objects_named_fields_set);
        schema.files.push_back(std::move(f));
    }
    {
        CfgFileSchema f;
        f.kind = "rules";
        f.file_name = "rules.cfg";
        // rules_named_fields_set has no field list of its own; the blocks live in ruleblocks[] (plan 10 §8a.3).
        static const char *const names[] = {"game", "rooms", "magic", "creatures", "computer", "workers", "health", "script_only"};
        for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        {
            CfgSectionSpec &s = section(f, names[i], false);
            add_table_fields(s, ruleblocks[i]);
            if (i == 7)
                s.note = "set by scripts only; not a block of rules.cfg";
        }
        add_key_table(section(f, "research", false), rules_research_commands);
        add_key_table(section(f, "sacrifices", false), rules_sacrifices_commands);
        describe_sacrifices(section(f, "sacrifices", false));
        // A layer that has the block clears the list built so far (config_rules.c parse_rules_*_blocks).
        section(f, "research", false).list_replaces = true;
        section(f, "sacrifices", false).list_replaces = true;
        schema.files.push_back(std::move(f));
    }
    {
        CfgFileSchema f;
        f.kind = "magic";
        f.file_name = "magic.cfg";
        add_table_set(f, magic_shot_named_fields_set);
        add_table_set(f, magic_powers_named_fields_set);
        add_key_table(section(f, "spell", true), magic_spell_commands);
        add_key_table(section(f, "special", true), magic_special_commands);
        schema.files.push_back(std::move(f));
    }
    {
        // One file per creature; the nine sections are read by hand-written parsers whose key tables are exported.
        CfgFileSchema f;
        f.kind = "creaturemodel";
        f.file_name = "<creature>.cfg";
        add_key_table(section(f, "attributes", false), creatmodel_attributes_commands);
        add_key_table(section(f, "jobs", false), creatmodel_jobs_commands);
        add_key_table(section(f, "attraction", false), creatmodel_attraction_commands);
        add_key_table(section(f, "sounds", false), creatmodel_sounds_commands);
        add_key_table(section(f, "sprites", false), creature_graphics_desc);
        add_key_table(section(f, "annoyance", false), creatmodel_annoyance_commands);
        add_key_table(section(f, "experience", false), creatmodel_experience_commands);
        add_key_table(section(f, "senses", false), creatmodel_senses_commands);
        add_key_table(section(f, "appearance", false), creatmodel_appearance_commands);
        schema.files.push_back(std::move(f));
    }

    describe_creature_schemas(schema);
    describe_campaign_schema(schema);
    apply_preferred_keys(schema);
    apply_ignored_keys(schema);
    return schema;
}
