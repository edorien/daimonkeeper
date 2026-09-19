/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file map_content_verify.cpp
 *     See map_content_verify.h.
 * @par Comment:
 *     None.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "map_content_verify.h"

#include "map_content_compat.h"
#include "config_terrain.h" // get_slab_kind_stats/SlbAtFlg_Blocking -- solid-slab check
#include "config_objects.h" // get_object_model_stats/OMF_Heart -- Dungeon Heart check
#include "thing_list.h" // THINGS_COUNT
#include "thing_data.h" // CREATURES_COUNT
#include "actionpt.h" // ACTN_POINTS_COUNT

#include <cstdio>
#include <set>

#include "post_inc.h"

namespace {

void add(std::vector<MapVerifyIssue> &issues, MapVerifyIssueSeverity severity, const std::string &message)
{
    MapVerifyIssue issue;
    issue.severity = severity;
    issue.message = message;
    issues.push_back(issue);
}

void add_at(std::vector<MapVerifyIssue> &issues, MapVerifyIssueSeverity severity, const std::string &message,
    MapCoord pos_x, MapCoord pos_y)
{
    MapVerifyIssue issue;
    issue.severity = severity;
    issue.message = message;
    issue.pos_x = pos_x;
    issue.pos_y = pos_y;
    issue.has_pos = true;
    issues.push_back(issue);
}

bool slab_is_blocking(SlabKind kind)
{
    struct SlabConfigStats *stats = get_slab_kind_stats(kind);
    return (stats != nullptr) && ((stats->block_flags & SlbAtFlg_Blocking) != 0);
}

// thing_is_dungeon_heart()'s own model-identification logic
// (src/kfx_sim/src/thing_objects.c), inlined here rather than exported as
// a shared helper -- it's a one-line config lookup, no live struct Thing*
// needed, and MapContent-only callers (this file) are the only other
// consumer so far.
bool object_model_is_dungeon_heart(ThingModel model)
{
    struct ObjectConfigStats *stats = get_object_model_stats(model);
    return (stats != nullptr) && ((stats->model_flags & OMF_Heart) != 0);
}

// docs/refactor/editor/phase3/06-slice7-verify-map.md -- every player
// referenced anywhere on the map (owns a slab, or owns a thing), not just
// ones with a Heart already -- so a Keeper with claimed territory or
// placed creatures but genuinely no Heart is still flagged, even if
// nothing else about them looks obviously wrong.
void check_dungeon_hearts(const MapContent &content, std::vector<MapVerifyIssue> &issues)
{
    std::set<PlayerNumber> owners_seen;
    for (PlayerNumber owner : content.slab_owner)
        if (owner < PLAYER_GOOD)
            owners_seen.insert(owner);
    for (const MapThingRecord &t : content.things)
        if (t.owner < PLAYER_GOOD)
            owners_seen.insert(t.owner);

    for (PlayerNumber owner : owners_seen)
    {
        bool has_heart = false;
        for (const MapThingRecord &t : content.things)
        {
            if ((t.thing_class == TCls_Object) && (t.owner == owner) && object_model_is_dungeon_heart(t.model))
            {
                has_heart = true;
                break;
            }
        }
        if (!has_heart)
        {
            char msg[80];
            snprintf(msg, sizeof(msg), "Player %d has no Dungeon Heart", (int)owner);
            add(issues, MVI_Error, msg);
        }
    }
}

// A thing's pos_x/pos_y is subtile-precision (COORD_PER_STL=256 per
// subtile, same convention as every other MapCoord field) -- >>8 to a
// whole subtile, then /STL_PER_SLB to the slab it falls in.
void check_embedded_things(const MapContent &content, std::vector<MapVerifyIssue> &issues)
{
    for (const MapThingRecord &t : content.things)
    {
        long sx = (t.pos_x >> 8) / STL_PER_SLB;
        long sy = (t.pos_y >> 8) / STL_PER_SLB;
        if ((sx < 0) || (sx >= content.map_tiles_x) || (sy < 0) || (sy >= content.map_tiles_y))
            continue;
        if (slab_is_blocking(content.slab_kind[content.slab_index(sx, sy)]))
        {
            char msg[96];
            snprintf(msg, sizeof(msg), "A thing (class %d) is embedded in an impenetrable slab at slab (%ld,%ld)",
                (int)t.thing_class, sx, sy);
            add_at(issues, MVI_Error, msg, t.pos_x, t.pos_y);
        }
    }
}

// WARN_THRESHOLD_NUM/DEN: 90% of cap -- a judgment call (F18 states the
// hard caps, not a "getting close" threshold), chosen so a mapmaker gets
// some runway to notice before actually hitting the wall.
void check_cap(std::vector<MapVerifyIssue> &issues, long count, long cap, const char *what)
{
    if (count >= cap)
    {
        char msg[96];
        snprintf(msg, sizeof(msg), "%ld %s exceeds the engine limit of %ld", count, what, cap);
        add(issues, MVI_Error, msg);
    }
    else if (count * 10 >= cap * 9)
    {
        char msg[112];
        snprintf(msg, sizeof(msg), "%ld %s is close to the engine limit of %ld", count, what, cap);
        add(issues, MVI_Warn, msg);
    }
}

// LIGHTS_COUNT (kfx_render/include/light_data.h) -- not reachable from
// kfx_sim, which ranks below kfx_render, so mirrored here. Dynamic lights
// (spell effects, creature glows) share these slots at runtime, so hitting
// the cap on saved static lights alone already leaves nothing for them.
const long kLightSlots = 2048;

void check_counts(const MapContent &content, std::vector<MapVerifyIssue> &issues)
{
    long creature_count = 0;
    for (const MapThingRecord &t : content.things)
        if (t.thing_class == TCls_Creature)
            creature_count++;

    check_cap(issues, (long)content.things.size(), THINGS_COUNT, "things");
    check_cap(issues, creature_count, CREATURES_COUNT, "creatures");
    check_cap(issues, (long)content.action_points.size(), ACTN_POINTS_COUNT, "action points");
    check_cap(issues, (long)content.lights.size(), kLightSlots, "lights");
}

void check_duplicate_action_points(const MapContent &content, std::vector<MapVerifyIssue> &issues)
{
    std::set<long> seen, duplicates;
    for (const MapActionPointRecord &a : content.action_points)
    {
        if (!seen.insert(a.point_number).second)
            duplicates.insert(a.point_number);
    }
    for (long num : duplicates)
    {
        char msg[80];
        snprintf(msg, sizeof(msg), "Action point number %ld is used more than once", num);
        add(issues, MVI_Warn, msg);
    }
}

void check_duplicate_herogates(const MapContent &content, std::vector<MapVerifyIssue> &issues)
{
    std::set<long> seen, duplicates;
    for (const MapThingRecord &t : content.things)
    {
        if ((t.thing_class != TCls_Object) || (t.herogate_number == 0))
            continue;
        if (!seen.insert(t.herogate_number).second)
            duplicates.insert(t.herogate_number);
    }
    for (long num : duplicates)
    {
        char msg[80];
        snprintf(msg, sizeof(msg), "Hero gate number %ld is used more than once", num);
        add(issues, MVI_Warn, msg);
    }
}

void check_border(const MapContent &content, std::vector<MapVerifyIssue> &issues)
{
    for (long y = 0; y < content.map_tiles_y; y++)
    {
        for (long x = 0; x < content.map_tiles_x; x++)
        {
            bool on_border = (x == 0) || (y == 0) || (x == content.map_tiles_x - 1) || (y == content.map_tiles_y - 1);
            if (!on_border)
                continue;
            if (!slab_is_blocking(content.slab_kind[content.slab_index(x, y)]))
            {
                add(issues, MVI_Warn, "The map border isn't fully impenetrable -- players may be able to dig/build off the edge of the map");
                return; // one summary issue, not one per breached border slab
            }
        }
    }
}

void check_classic_compatibility(const MapContent &content, std::vector<MapVerifyIssue> &issues)
{
    std::vector<std::string> reasons;
    bool compatible = map_is_legacy_compatible(content, &reasons);
    if (compatible)
    {
        add(issues, MVI_Info, "Classic-compatible: yes (can be saved as classic .tng/.lgt/.apt with nothing lost)");
        return;
    }
    std::string msg = "Classic-compatible: no";
    for (const std::string &reason : reasons)
    {
        msg += " -- ";
        msg += reason;
    }
    add(issues, MVI_Info, msg);
}

} // namespace

std::vector<MapVerifyIssue> verify_map_content(const MapContent &content)
{
    std::vector<MapVerifyIssue> issues;
    check_dungeon_hearts(content, issues);
    check_embedded_things(content, issues);
    check_counts(content, issues);
    check_duplicate_action_points(content, issues);
    check_duplicate_herogates(content, issues);
    check_border(content, issues);
    check_classic_compatibility(content, issues);
    return issues;
}
