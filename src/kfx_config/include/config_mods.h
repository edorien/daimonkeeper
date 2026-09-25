/******************************************************************************/
// Free implementation of KeeperFx.
/******************************************************************************/
/* @par  Purpose:
 *     The original intention of the design is that users can customize any data, and can overwrite the default settings of KeeperFX.
 *     Typical, when upgrading to a new version, simply copying a mods directory can complete the annoying reconfiguration.
 *
 * @par  Log:
 *     hzzdev - 07 Sep 2025
 *     First version mainly implements functions related to basic configuration files and creature configuration files.
 *     If more types of data can be loaded later(eg. effects), the functionality will become very powerful.
 *     More information can be referred to https://github.com/dkfans/keeperfx/issues/3027
 *
 *     hzzdev - 18 Sep 2025, Add sprite loading for mods.
 *     hzzdev - 30 Oct 2025, Add multi-lang string loading for mods.
 *     hzzdev - 23 Feb 2026, Add texture loading for mods.
 *     hzzdev - 20 Apr 2026, Add lua loading for mods.
 *     cerwym - 15 Jun 2026, Add sound loading for mods.
 *     hzzdev - 27 Jun 2026, Add music loading for mods.
 *
 */
/******************************************************************************/

#ifndef DK_CFG_MODS_H
#define DK_CFG_MODS_H

#include "globals.h"
#include "bflib_basics.h"
#include "mod_config_types.h"

#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MODS_LOAD_ORDER_FILE_NAME "load_order.cfg"

#define MODS_AFTER_BASE_BLOCK_NAME "after_base"
#define MODS_AFTER_CAMPAIGN_BLOCK_NAME "after_campaign"
#define MODS_AFTER_MAP_BLOCK_NAME "after_map"

// struct ModExistState/struct ModConfigItem/MOD_ITEM_MAX moved to
// kfx_platform's mod_config_types.h (stage 13.3, docs/refactor/
// stage-13-enforce-and-document.md) -- bflib_sndlib.cpp/sound_manager.cpp
// need them by value to walk a mod list for music/sound path
// resolution; kfx_platform is the lowest-ranked of their consumers.

struct ModsConfig {
    int64_t after_base_cnt;
    struct ModConfigItem after_base_item[MOD_ITEM_MAX];

    int64_t after_campaign_cnt;
    struct ModConfigItem after_campaign_item[MOD_ITEM_MAX];

    int64_t after_map_cnt;
    struct ModConfigItem after_map_item[MOD_ITEM_MAX];
};

const struct ModsConfig *get_loaded_mods_conf(void);
#define mods_conf (*get_loaded_mods_conf())
void recheck_all_mod_exist();
TbBool load_mods_order_config_file();

#ifdef __cplusplus
}
#endif

#endif // DK_CFG_MODS_H
