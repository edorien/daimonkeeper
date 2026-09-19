/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file map_content_verify.h
 *     verify_map_content() -- the map validation report.
 * @par Purpose:
 *     docs/refactor/editor/phase3/06-slice7-verify-map.md. Runs a fixed set
 *     of checks against a MapContent snapshot and returns a flat issue
 *     list a dialog can render -- purely informational, never blocks
 *     anything (matches map_is_legacy_compatible()'s own "advisory, not a
 *     gate" precedent). No room/portal checks (skirmish maps with
 *     pre-placed creatures, or maps with imperfect rooms, are legitimately
 *     valid) and no classic-vs-KeeperFX "target mode" (map_is_legacy_
 *     compatible() already answers that on demand; this surfaces its
 *     result as one INFO line instead of duplicating the concept).
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_MAP_CONTENT_VERIFY_H
#define DK_MAP_CONTENT_VERIFY_H

#include "map_content.h"

#include <string>
#include <vector>

enum MapVerifyIssueSeverity
{
    MVI_Info = 0,
    MVI_Warn,
    MVI_Error,
};

struct MapVerifyIssue
{
    MapVerifyIssueSeverity severity = MVI_Info;
    std::string message;
    // Set (has_pos = true) when this issue is tied to a specific map
    // location -- a dialog can offer to zoom there (PckA_ZoomToPosition
    // takes exactly this subtile-precision MapCoord pair, no conversion
    // needed on the caller's side).
    MapCoord pos_x = 0;
    MapCoord pos_y = 0;
    bool has_pos = false;
};

std::vector<MapVerifyIssue> verify_map_content(const MapContent &content);

#endif
