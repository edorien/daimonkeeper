#include "ftest_editor_palette.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include "../ftest.h"
#include "../ftest_util.h"

#include "game_legacy.h"
#include "config_keeperfx.h"
#include "config.h"
#include "kfx_config_state.h"
#include "config_terrain.h"
#include "config_objects.h"
#include "editor_palette.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ftest_editor_palette__variables { int unused; };
struct ftest_editor_palette__variables ftest_editor_palette__vars = { 0 };

FTestActionResult ftest_editor_palette_action001__classify(struct FTestActionArgs* const args);

TbBool ftest_editor_palette_init()
{
    ftest_append_action(ftest_editor_palette_action001__classify, 20, &ftest_editor_palette__vars);
    return true;
}

FTestActionResult ftest_editor_palette_action001__classify(struct FTestActionArgs* const args)
{
    const struct SlabsConfig* slabc = &kfx_config_state.conf.slab_conf;
    int terrain_groups[ETG_Count] = { 0, 0, 0, 0 };
    for (int32_t k = 0; k < slabc->slab_types_count; k++)
    {
        int g = editor_terrain_group_of((SlabKind)k);
        if ((g < 0) || (g >= ETG_Count))
        {
            FTEST_FAIL_TEST("Slab %d (%s) got invalid group %d", (int)k, slab_code_name((SlabKind)k), g);
            return FTRs_Go_To_Next_Action;
        }
        terrain_groups[g]++;
    }
    int rooms_with_slab = 0;
    for (int32_t r = 1; r < slabc->room_types_count; r++)
    {
        SlabKind slab = slabc->room_cfgstats[r].assigned_slab;
        if (slab <= 0)
            continue;
        rooms_with_slab++;
        if (editor_terrain_group_of(slab) != ETG_Rooms)
        {
            FTEST_FAIL_TEST("Room %s's floor slab %s is not in the Rooms group", room_code_name((RoomKind)r), slab_code_name(slab));
            return FTRs_Go_To_Next_Action;
        }
    }
    if ((terrain_groups[ETG_Rooms] == 0) || (terrain_groups[ETG_Terrain] == 0) || (terrain_groups[ETG_Walls] == 0) || (terrain_groups[ETG_Other] == 0))
    {
        FTEST_FAIL_TEST("An empty terrain group: terrain %d, rooms %d, other %d",
            terrain_groups[ETG_Terrain], terrain_groups[ETG_Rooms], terrain_groups[ETG_Other]);
        return FTRs_Go_To_Next_Action;
    }
    if (editor_terrain_group_of(SlbT_ROCK) != ETG_Terrain)
    {
        FTEST_FAIL_TEST("ROCK is not in the Terrain group");
        return FTRs_Go_To_Next_Action;
    }
    JUSTLOG("Palette terrain groups: terrain %d, rooms %d (%d rooms have a slab), walls %d, other %d",
        terrain_groups[ETG_Terrain], terrain_groups[ETG_Rooms], rooms_with_slab, terrain_groups[ETG_Walls], terrain_groups[ETG_Other]);

    int object_groups[EOG_Count] = { 0, 0, 0, 0 };
    int spellbooks_without_power = 0;
    for (ThingModel m = 1; m < (ThingModel)kfx_config_state.conf.object_conf.object_types_count; m++)
    {
        int g = editor_object_group_of(m);
        if ((g < 0) || (g >= EOG_Count))
        {
            FTEST_FAIL_TEST("Object %s got invalid group %d", object_code_name(m), g);
            return FTRs_Go_To_Next_Action;
        }
        object_groups[g]++;
        if ((g == EOG_Spells) && (editor_spellbook_power(m) <= 0))
        {
            spellbooks_without_power++;
            JUSTLOG("Palette: spellbook %s has no power mapping", object_code_name(m));
        }
    }
    JUSTLOG("Palette object groups: spells %d (%d without a power), specials %d, crates %d, decor %d",
        object_groups[EOG_Spells], spellbooks_without_power, object_groups[EOG_Specials],
        object_groups[EOG_Crates], object_groups[EOG_Decor]);
    if ((object_groups[EOG_Spells] == 0) || (object_groups[EOG_Specials] == 0)
        || (object_groups[EOG_Crates] == 0) || (object_groups[EOG_Decor] == 0))
    {
        FTEST_FAIL_TEST("An empty object group: spells %d, specials %d, crates %d, decor %d",
            object_groups[EOG_Spells], object_groups[EOG_Specials], object_groups[EOG_Crates], object_groups[EOG_Decor]);
    }
    return FTRs_Go_To_Next_Action;
}

#endif
