#include "ftest_editor_thumbs.h"

#ifdef FUNCTESTING

extern "C" {
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
}
#include "editor_thumbs.h"

#include <string>
#include <vector>

extern "C" {

FTestActionResult ftest_editor_thumbs_action001__build(struct FTestActionArgs* const args);

static int s_unused;

TbBool ftest_editor_thumbs_init()
{
    ftest_append_action(ftest_editor_thumbs_action001__build, 20, &s_unused);
    return true;
}

static bool opaque_count_at_least(const std::vector<uint32_t>& pixels, size_t n)
{
    size_t c = 0;
    for (uint32_t p : pixels)
        if ((p >> 24) != 0)
            c++;
    return c >= n;
}

FTestActionResult ftest_editor_thumbs_action001__build(struct FTestActionArgs* const args)
{
    const struct SlabsConfig* slabc = &kfx_config_state.conf.slab_conf;
    int slabs_ok = 0, slabs_total = 0;
    std::string slabs_missing;
    for (int32_t k = 0; k < slabc->slab_types_count; k++)
    {
        int g = editor_terrain_group_of((SlabKind)k);
        if ((g == ETG_Rooms) || (editor_door_of_slab((SlabKind)k) > 0))
            continue; // rooms and doors use their in-game icons
        slabs_total++;
        std::vector<uint32_t> px;
        int w = 0, h = 0;
        if (editor_thumb_slab_pixels((SlabKind)k, px, w, h) && (w > 0) && (h > 0) && opaque_count_at_least(px, 16))
            slabs_ok++;
        else
        {
            slabs_missing += slab_code_name((SlabKind)k);
            slabs_missing += ",";
        }
    }
    JUSTLOG("Thumbnails: %d/%d slabs ok; missing: %s", slabs_ok, slabs_total, slabs_missing.c_str());

    int objs_ok = 0, objs_total = 0;
    std::string objs_missing;
    for (ThingModel m = 1; m < (ThingModel)kfx_config_state.conf.object_conf.object_types_count; m++)
    {
        int g = editor_object_group_of(m);
        if ((g == EOG_Spells) || (g == EOG_Crates))
            continue; // those use power / workshop icons
        objs_total++;
        std::vector<uint32_t> px;
        int w = 0, h = 0;
        if (editor_thumb_object_pixels(m, px, w, h) && (w > 0) && (h > 0) && opaque_count_at_least(px, 4))
            objs_ok++;
        else
        {
            objs_missing += object_code_name(m);
            objs_missing += ",";
        }
    }
    JUSTLOG("Thumbnails: %d/%d objects ok; missing: %s", objs_ok, objs_total, objs_missing.c_str());

    // The shapes we rely on must work: rock (a wall) and a path (a floor).
    std::vector<uint32_t> px;
    int w = 0, h = 0;
    if (!editor_thumb_slab_pixels(SlbT_ROCK, px, w, h) || w != 32 || h != 32)
    {
        FTEST_FAIL_TEST("ROCK has no 32x32 wall-front thumbnail");
        return FTRs_Go_To_Next_Action;
    }
    if (!editor_thumb_slab_is_wall(SlbT_ROCK))
    {
        FTEST_FAIL_TEST("ROCK is not treated as a wall");
        return FTRs_Go_To_Next_Action;
    }
    if (slabs_ok * 10 < slabs_total * 7)
    {
        FTEST_FAIL_TEST("Only %d of %d slab thumbnails built", slabs_ok, slabs_total);
        return FTRs_Go_To_Next_Action;
    }
    if (objs_ok * 10 < objs_total * 4)
    {
        FTEST_FAIL_TEST("Only %d of %d object thumbnails built", objs_ok, objs_total);
    }
    return FTRs_Go_To_Next_Action;
}

} // extern "C"

#endif
