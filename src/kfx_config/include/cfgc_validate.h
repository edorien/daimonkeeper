/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file cfgc_validate.h
 *     Header file for cfgc_validate.cpp.
 * @par Purpose:
 *     docs/refactor/editor/fx-plans/10-config-content-model-and-writers.md
 *     §4.5 -- whole-file validation: what the loader would silently clamp,
 *     ignore or skip in a configuration file, reported with the line it is
 *     on. Used by the raw config editor (plan 03 F3), Verify Map and the JSON
 *     `validate` operation (docs/refactor/AI/LLM/00-config-json-spec.md §7.5).
 * @par Comment:
 *     Never blocks anything by itself; the caller decides. Engine-decoupled.
 */
/******************************************************************************/
#ifndef DK_CFGC_VALIDATE_H
#define DK_CFGC_VALIDATE_H

#include <vector>

#include "cfgc_document.h"
#include "cfgc_schema.h"

// Checks every line of `doc`. `schema` may be null (a file kind with no schema): only the structural
// checks run. `names` may be null: names are then not checked against registries.
//   duplicate_block_ignored  a block repeats an earlier block's exact name (the loader never reads it)
//   unknown_section          a block that is not part of this file kind
//   unknown_key              a key that is not part of its block
//   key_outside_block        a key before the first "[block]" (the loader skips it)
//   unrecognised_line        text that is not a key, block or comment (the loader skips it)
// plus everything cfgc_validate_value() reports for each key line.
std::vector<CfgDiagnostic> cfgc_validate_document(const ConfigDocument &doc, const CfgFileSchema *schema,
    const CfgNameSets *names);

#endif
