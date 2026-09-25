/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file content_names.h
 *     docs/refactor/editor/fx-plans/10-config-content-model-and-writers.md §4.2 (NameRegistry) -- the names
 *     a target's configuration defines, gathered across files: traps, doors, objects, slabs, rooms, shots,
 *     spells, powers, specials, and the creature list, each layer applied in order. What the form editors
 *     offer in their drop-downs and what validation checks names against.
 * @par Comment:
 *     Internal to kfx_editor; reads files through the kfx_config content layer, no engine state.
 */
#ifndef DK_CONTENT_NAMES_H
#define DK_CONTENT_NAMES_H

#include <string>
#include <vector>

#include "cfgc_stack.h"

/** Names from the layers of `target` up to and including `upto`. */
CfgNameSets content_collect_names(const ConfigTarget &target, CfgLayer upto);

/** The names of one registry, sorted, for a drop-down; empty if the registry is unknown. */
std::vector<std::string> content_registry_list(const CfgNameSets &names, const std::string &registry);

#endif
