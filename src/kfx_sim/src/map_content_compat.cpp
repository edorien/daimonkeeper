/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file map_content_compat.cpp
 *     See map_content_compat.h.
 * @par Comment:
 *     None.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "map_content_compat.h"

#include "map_data.h"

#include "post_inc.h"

namespace {

void flag(std::vector<std::string> *reasons, const char *msg)
{
    if (reasons != nullptr)
        reasons->push_back(msg);
}

} // namespace

bool map_is_legacy_compatible(const MapContent &content, std::vector<std::string> *reasons)
{
    bool compatible = true;

    // Required for the classic formats' u16 coordinate narrowing
    // (ClassicMapContentWriter) to be lossless at all -- not just a size
    // nicety, see map_content_writer.cpp's own comment on this.
    if ((content.map_tiles_x != DEFAULT_MAP_SIZE) || (content.map_tiles_y != DEFAULT_MAP_SIZE))
    {
        compatible = false;
        flag(reasons, "Map size is not 85x85 (classic map size)");
    }

    // >255 creatures overflows the classic format's own assumptions
    // (F18/07-investigation-findings.md) -- not a byte-layout limit like
    // the ones below (LegacyInitThing's owner/model bytes are u8 either
    // way), but a real content-scale one.
    int64_t creature_count = 0;
    for (const MapThingRecord &t : content.things)
    {
        if (t.thing_class != TCls_Creature)
            continue;
        creature_count++;
        // Found live while building ClassicMapContentWriter: the classic
        // .tng format has no field for any of these three at all --
        // thing_create_thing() only ever reads params[1] as exp_level for
        // a creature, nothing else. A KFX-native map using them would
        // silently lose the data if force-saved as classic.
        if (t.creature_gold != 0)
        {
            compatible = false;
            flag(reasons, "A creature has custom gold carried (not representable in classic .tng)");
        }
        if (t.creature_health_percent != 0)
        {
            compatible = false;
            flag(reasons, "A creature has a custom initial health percentage (not representable in classic .tng)");
        }
        if (!t.creature_name.empty())
        {
            compatible = false;
            flag(reasons, "A creature has a custom name (not representable in classic .tng)");
        }
        // orientation checked below, alongside Object/Trap.
    }
    if (creature_count > 255)
    {
        compatible = false;
        flag(reasons, "More than 255 creatures (classic engine limit)");
    }

    for (const MapThingRecord &t : content.things)
    {
        // Found live: thing_create_thing()'s switch never sets
        // move_angle_xy for any class -- Orientation has no classic
        // representation at all, for Object/Creature/Trap alike (the only
        // classes the TOML path lets it apply to).
        if (((t.thing_class == TCls_Object) || (t.thing_class == TCls_Creature) || (t.thing_class == TCls_Trap))
            && (t.orientation != 0))
        {
            compatible = false;
            flag(reasons, "A thing has a custom orientation (not representable in classic .tng)");
        }
        // Classic object creation has no gold-pile-amount case at all
        // (unlike the TOML path's GoldValue handling) -- a custom amount
        // would silently revert to the model's own config default.
        if ((t.thing_class == TCls_Object) && (t.gold_value != 0))
        {
            compatible = false;
            flag(reasons, "A gold pile has a custom value (not representable in classic .tng)");
        }
    }

    // Not implemented this slice, deliberately: classic vanilla ID-range
    // checks for slab kinds / creature / object / trap / door models
    // (D5/F20's own "outside the classic ID range" criteria). No reliable
    // "here's where the base roster ends and mods start" marker was found
    // in the config structs while building this -- rather than hardcode
    // unverified magic numbers that could silently misclassify a map
    // either direction, this is left as a known, documented gap (see
    // docs/refactor/editor/phase3/01-slice2-classic-save.md) rather than
    // guessed at. A map using only modded IDs but otherwise legacy-shaped
    // content will be (wrongly) reported compatible until this lands.

    return compatible;
}
