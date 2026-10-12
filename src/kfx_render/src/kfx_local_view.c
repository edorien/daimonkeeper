/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file kfx_local_view.c
 *     This machine's view of the game (kfx_local_view.h).
 */
/******************************************************************************/
#include "pre_inc.h"
#include "kfx_local_view.h"

#include "config_magic.h"
#include "state_versions.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
struct KfxLocalView kfx_local_view;

_Static_assert(sizeof(struct KfxLocalView) == KFX_LOCAL_VIEW_SIZE, "struct KfxLocalView changed size: bump KFX_LOCAL_VIEW_VER and update KFX_LOCAL_VIEW_SIZE in state_versions.h");

/**
 * Sets keeper power selected by local human player.
 *
 * @param pwkind Power to select.
 * @param sptooltip Tooltip string index.
 * @note Was set_chosen_spell()
 */
void set_chosen_power(PowerKind pwkind, TextStringId sptooltip)
{
    const struct PowerConfigStats* powerst = get_power_model_stats(pwkind);
    if (power_model_stats_invalid(powerst))
      pwkind = 0;
    SYNCDBG(6,"Setting to %" PRIu64,(uint64_t)(pwkind));
    kfx_local_view.chosen_spell_type = pwkind;
    kfx_local_view.chosen_spell_spridx = powerst->bigsym_sprite_idx;
    kfx_local_view.chosen_spell_tooltip = sptooltip;
}

void set_chosen_power_none(void)
{
    SYNCDBG(6,"Setting to %" PRId64,(int64_t)(0));
    kfx_local_view.chosen_spell_type = 0;
    kfx_local_view.chosen_spell_spridx = 0;
    kfx_local_view.chosen_spell_tooltip = 0;
}

void local_view_power_removed(PowerKind pwkind)
{
    if (kfx_local_view.chosen_spell_type == pwkind)
    {
        set_chosen_power_none();
    }
}

char local_view_active_panel_menu(void)
{
    return kfx_local_view.active_panel_mnu_idx;
}

void local_view_battles_restart(BattleIndex first)
{
    kfx_local_view.visible_battles[0] = first;
    kfx_local_view.visible_battles[1] = 0;
    kfx_local_view.visible_battles[2] = 0;
}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
