/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file cfgc_schema_shapes.h
 *     Header file for cfgc_schema_shapes.cpp.
 * @par Purpose:
 *     The compact "shape" language the curated schemas (creature files, creature.cfg, campaign files) describe a key's
 *     values with, turned into CfgFieldSpec parts. One token per value: `N` number, `S` text, `E:reg` a name from a
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

/** Names of a static table by its id (`F:creature_props`, `T:deathkind`); an unknown id gives an empty list, which
 *  makes `F:` treat the id as a registry name instead. */
typedef std::function<std::vector<std::string>(const std::string &table)> CfgTableNames;

void cfgc_apply_shape(CfgFieldSpec &f, const std::string &shape, const CfgTableNames &table_names);

#endif
