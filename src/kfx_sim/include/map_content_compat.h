/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file map_content_compat.h
 *     map_is_legacy_compatible() -- the Auto-format predicate.
 * @par Purpose:
 *     docs/refactor/editor/phase3/01-slice2-classic-save.md /
 *     docs/refactor/editor/07-investigation-findings.md F20 (decision D5).
 *     True iff `content` can be saved via ClassicMapContentWriter without
 *     silently losing any data that writer genuinely cannot represent --
 *     not just "is this map small/simple", but specifically "does this
 *     content use any feature the classic .tng/.lgt/.apt byte formats have
 *     no field for at all" (found by reading thing_create_thing()'s own
 *     switch directly, not assumed from the TOML path's schema).
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_MAP_CONTENT_COMPAT_H
#define DK_MAP_CONTENT_COMPAT_H

#include "map_content.h"

#include <string>
#include <vector>

// Returns true iff `content` is legacy-compatible. When false and `reasons`
// is non-null, appends a short human-readable string per blocking marker
// found (for a future Save dialog's "why not" list, and for tests) --
// `reasons` is left untouched (not cleared) when true, matching "only
// write what you found" rather than asserting an empty-vector contract on
// every caller.
bool map_is_legacy_compatible(const MapContent &content, std::vector<std::string> *reasons = nullptr);

#endif
