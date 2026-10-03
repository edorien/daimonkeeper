/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file cfgc_schema_shapes.h
 *     Header file for cfgc_schema_shapes.cpp.
 * @par Purpose:
 *     The compact "shape" language the curated schemas (creature files, creature.cfg, campaign files) describe a key's
 *     values with, turned into CfgFieldSpec parts. One token per value: `N` number, `S` text, `I` an icon, `E:reg` a name from a
 *     registry, `E:reg|NULL|X` the same with literal extras, `F:reg` / `F:table` a list of names (flags), `T:table`
 *     one name from a static table, `R:table` a number that may be written as a name, `X*n` repeat a token n times, a
 *     trailing `...` lets the last token repeat, and `text` alone means the loader takes the whole line as one string.
 */
/******************************************************************************/
#ifndef DK_CFGC_SCHEMA_SHAPES_H
#define DK_CFGC_SCHEMA_SHAPES_H

#include <functional>
#include <string>
#include <vector>

#include "cfgc_schema.h"

struct NamedField;

/** Names of a static table by its id (`F:creature_props`, `T:deathkind`); an unknown id gives an empty list, which
 *  makes `F:` treat the id as a registry name instead. */
typedef std::function<std::vector<std::string>(const std::string &table)> CfgTableNames;

void cfgc_apply_shape(CfgFieldSpec &f, const std::string &shape, const CfgTableNames &table_names);

/** From cfgc_schema_engine.cpp: a NamedField table's rows as fields of a section (keys, value kinds, bounds, names). */
void cfgc_add_table_fields(CfgSectionSpec &sec, const struct NamedField *fields);
/** The section of that name, added if missing. */
CfgSectionSpec &cfgc_schema_section(CfgFileSchema &file, const std::string &basename, bool numbered);
/** The value kind a table row's parse function gives; CfgKind_Custom when the schema can't read it (then the key
 *  needs a shape). */
CfgFieldKind cfgc_table_row_kind(const struct NamedField &row);
/** From cfgc_schema_creature.cpp: whether a creature, creature model or magic key has a hand-kept shape (only keys
 *  whose parse function the schema can't read should). */
bool cfgc_creature_shape_override(const std::string &kind, const std::string &section, const std::string &key);
struct CfgShapeOverride
{
    std::string kind, section, key;
};
/** Every hand-kept creature, creature model and magic shape, for tests. */
std::vector<CfgShapeOverride> cfgc_creature_shape_overrides();

#endif
