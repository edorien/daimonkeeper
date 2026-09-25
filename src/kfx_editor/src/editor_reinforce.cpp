/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_reinforce.cpp
 *     See editor_reinforce.h.
 * @par Comment:
 *     The wall placed is choose_pretty_type()'s pick (the engine's own choice
 *     for a reinforced wall, which varies with the neighbours), followed by
 *     fill_in_reinforced_corners(), exactly as an imp finishing a
 *     reinforcement does (place_and_process_pretty_wall_slab()).
 */
#include "pre_inc.h"
#include "editor_reinforce.h"
#include "editor_journal.h"
#include "kfx_editor.h"
#include "kfx_sim_state.h"
#include "slab_data.h"
#include "map_blocks.h"
#include "map_utils.h"
#include "config_terrain.h"
#include <vector>
#include "post_inc.h"

namespace {

bool is_owned_open_ground(MapSlabCoord x, MapSlabCoord y, PlayerNumber owner)
{
    if (x < 0 || y < 0 || x >= kfx_sim_state.map_tiles_x || y >= kfx_sim_state.map_tiles_y)
        return false;
    const struct SlabMap *slb = get_slabmap_block(x, y);
    if (slabmap_owner(slb) != owner)
        return false;
    const struct SlabConfigStats *st = get_slab_stats(slb);
    // Room floors and doors are tested by kind: the config's block flags do
    // not reliably carry IsRoom for every room's floor slab.
    return (st->category == SlbAtCtg_FortifiedGround)
        || ((st->block_flags & (SlbAtFlg_IsRoom | SlbAtFlg_IsDoor)) != 0)
        || slab_kind_is_room(slb->kind) || slab_kind_is_door(slb->kind);
}

} // namespace

int64_t editor_reinforce_perimeter(PlayerNumber owner)
{
    struct Target { MapSlabCoord x, y; };
    std::vector<Target> targets;
    for (MapSlabCoord y = 0; y < kfx_sim_state.map_tiles_y; y++)
    {
        for (MapSlabCoord x = 0; x < kfx_sim_state.map_tiles_x; x++)
        {
            const struct SlabMap *slb = get_slabmap_block(x, y);
            if (slb->kind != SlbT_EARTH && slb->kind != SlbT_TORCHDIRT)
                continue;
            for (int64_t n = 0; n < SMALL_AROUND_LENGTH; n++)
            {
                if (is_owned_open_ground(x + small_around[n].delta_x, y + small_around[n].delta_y, owner))
                {
                    targets.push_back({x, y});
                    break;
                }
            }
        }
    }
    if (targets.empty())
        return 0;
    editor_journal_stroke_begin();
    for (const Target &t : targets)
    {
        const SlabKind pretty = choose_pretty_type(owner, t.x, t.y);
        place_slab_type_on_map(pretty, slab_subtile_center(t.x), slab_subtile_center(t.y), owner, 0);
        do_slab_efficiency_alteration(t.x, t.y);
    }
    for (const Target &t : targets)
        fill_in_reinforced_corners(owner, t.x, t.y);
    editor_journal_stroke_end("Reinforce");
    editor_mark_dirty();
    return (int64_t)targets.size();
}
