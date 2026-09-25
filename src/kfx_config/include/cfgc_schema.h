/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file cfgc_schema.h
 *     Header file for cfgc_schema.cpp and cfgc_schema_engine.cpp.
 * @par Purpose:
 *     docs/refactor/editor/fx-plans/10-config-content-model-and-writers.md
 *     §4.2 / §4.5 -- ConfigSchema: per file kind, the sections and fields a
 *     configuration file may hold, with each field's value grammar, range and
 *     enum source, plus a validator that reports what the loader would
 *     silently change (clamp a number, ignore a name).
 * @par Comment:
 *     cfgc_schema.cpp is plain data and logic (no engine). The reflection that
 *     fills a schema from the engine's static field tables lives in
 *     cfgc_schema_engine.cpp (build_engine_schema) and reads only const
 *     tables, never loaded config state.
 */
/******************************************************************************/
#ifndef DK_CFGC_SCHEMA_H
#define DK_CFGC_SCHEMA_H

#include <cstdint>
#include <set>
#include <string>
#include <vector>

struct ConfigContent;

// How one value of a field is written.
enum CfgFieldKind
{
    CfgKind_Number = 0,   // integer (range in min/max), or an enum name where names are given
    CfgKind_Enum,         // one name from a list (static names, or a registry of names defined by other files)
    CfgKind_Flags,        // "none", a number, or a list of names OR-ed together
    CfgKind_Text,         // free text (a Name, a path)
    CfgKind_Icon,         // sprite name
    CfgKind_AnimId,       // animation name
    CfgKind_StringId,     // string alias or number
    CfgKind_EffectRef,    // effect or effect element name or number
    CfgKind_Function,     // Lua function name
    CfgKind_Coord,        // number of subtiles, converted to coordinates by the loader
    CfgKind_Custom,       // engine-specific grammar; not checked beyond "present"
    CfgKind_Unspecified   // key known (from a key table) but its grammar is not described yet
};

// Whether this build's loader reads a key.
enum CfgFieldState
{
    CfgState_Known = 0,
    CfgState_Ignored // present in shipped files, no effect in this build (plan 10 §8a)
};

// One whitespace-separated value of a key ("SymbolSprites = big med" has two).
struct CfgValueSpec
{
    CfgFieldKind kind = CfgKind_Number;
    int64_t min = INT64_MIN;
    int64_t max = INT64_MAX;
    int64_t default_value = 0;
    std::string enum_registry;           // names come from the NameRegistry of this kind ("door", "object", ...)
    std::vector<std::string> enum_names; // static names
};

struct CfgFieldSpec
{
    std::string key;     // canonical spelling
    CfgFieldState state = CfgState_Known;
    bool whole_string = false; // the loader takes the rest of the line as one string
    // The last part applies to every further value too (a recipe's victims: "MkCreature = HORNY TROLL SPIDER ...").
    bool repeat_last = false;
    std::string note;    // reason for CfgState_Ignored, or a hint
    // Non-empty: this key writes the same struct field as `alias_of` (a backward-compatibility spelling,
    // like SIZE_YZ for SIZE_Z). Whichever of the two the file sets last wins; editors write the canonical one.
    std::string alias_of;
    std::vector<CfgValueSpec> parts;
};

// A "[basename]" or "[basenameN]" block.
struct CfgSectionSpec
{
    std::string basename;
    bool numbered = false; // [trap3] rather than [common]
    std::string note;
    // A block that replaces the lower layers' whole block when a layer contains it
    // ([research], [sacrifices]); other blocks merge key by key.
    bool list_replaces = false;
    std::vector<CfgFieldSpec> fields;

    const CfgFieldSpec *find_field(const std::string &key) const; // case-insensitive
    CfgFieldSpec *find_field(const std::string &key);
};

struct CfgFileSchema
{
    std::string kind;      // "trapdoor", "terrain", ...
    std::string file_name; // "trapdoor.cfg"
    std::vector<CfgSectionSpec> sections;

    const CfgSectionSpec *find_section(const std::string &basename) const;
    CfgSectionSpec *find_section(const std::string &basename);
};

struct ConfigSchema
{
    std::vector<CfgFileSchema> files;
    const CfgFileSchema *find(const std::string &kind) const;
    CfgFileSchema *find(const std::string &kind);
};

// Reflects the engine's static field tables and exported key tables into a
// schema, then applies the curated overlays (bespoke blocks, ignored keys).
ConfigSchema build_engine_schema();

// "[####### MELEE BASED #######]": a comment written as a section header. The loader finds no
// such block, so it is not a real section of any kind.
bool cfgc_is_banner_section(const std::string &name);

enum CfgSeverity { CfgSev_Info = 0, CfgSev_Warning, CfgSev_Error };

struct CfgDiagnostic
{
    CfgSeverity severity = CfgSev_Warning;
    std::string code;    // machine readable: "number_range", "unknown_name", "unknown_key", ...
    std::string message; // human readable
    // Where, when the diagnostic comes from a document (cfgc_validate_document); 0 / empty otherwise.
    int64_t line = 0;    // 1-based
    std::string section; // canonical block id
    std::string key;
};

// Names known from other files, by registry kind ("door" -> {"DOOR1", ...}).
// Names compare case-insensitively. Null or a missing registry disables the
// existence check for that kind.
struct CfgNameSets
{
    std::vector<std::pair<std::string, std::set<std::string>>> registries; // names stored upper-case

    void add(const std::string &registry, const std::string &name);
    // Replaces a registry's names (a layer's list that replaces the lower one: creature.cfg Creatures).
    void set(const std::string &registry, const std::vector<std::string> &list);
    const std::set<std::string> *find(const std::string &registry) const;
};

// Adds the Name of every numbered section whose basename is a name registry
// ("trap", "door", "slab", "room", "object", "shot", "spell", "power", "special")
// to `names`, under a registry called by the basename. Call once per layer,
// base file first; later layers add to the set (a level file may add items).
void cfgc_collect_names(const struct ConfigContent &content, CfgNameSets &names);

// creature.cfg keeps its creatures as one list, `[common] Creatures = WIZARD BARBARIAN ...`; adds those to the
// "creature" registry, replacing what an earlier layer gave.
void cfgc_collect_creature_names(const struct ConfigContent &content, CfgNameSets &names);

// Checks one value text against a field spec. Never throws; appends what the
// loader would clamp, ignore or misread.
void cfgc_validate_value(const CfgFieldSpec &spec, const std::string &text, const CfgNameSets *names,
    std::vector<CfgDiagnostic> &out);

#endif
