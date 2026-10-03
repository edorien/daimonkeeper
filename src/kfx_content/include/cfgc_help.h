/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file cfgc_help.h
 *     Header file for cfgc_help.cpp.
 * @par Purpose:
 *     docs/refactor/editor/fx-plans/10-config-content-model-and-writers.md §4.2 -- help text for
 *     keys, lifted from the comments the base files carry above each key:
 *
 *         ; Game turns between pay days.
 *         PayDayGap = 10000
 *
 *     Used by the structured editors for tooltips and by the JSON `describe` (help member).
 */
/******************************************************************************/
#ifndef DK_CFGC_HELP_H
#define DK_CFGC_HELP_H

#include <map>
#include <string>

#include "cfgc_document.h"

// Help text by "section/key" (both lower-case, the section without its number: "trap/health"). Text is
// the comment lines directly above the key (no blank line between), joined by a blank, without the ";".
// The first block that documents a key wins; a key with no such comment is absent.
std::map<std::string, std::string> cfgc_extract_help(const ConfigDocument &doc);

// The lookup key for (section id or name, key).
std::string cfgc_help_key(const std::string &section, const std::string &key);

#endif
