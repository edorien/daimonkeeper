/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file sound_host_port_impl.c
 *     kfx_game's SoundHostPort table: the implementations next to their
 *     provider, installed by main.cpp's wire_ports(). Refactor pass 2, S15.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "sound_host_port_impl.h"
#include "sounds.h"
#include "config_creature.h"
#include "kfx_game_state.h"
#include "kfx_net_state.h"
#include "kfx_sim_state.h"
#include "kfx_render_state.h"
#include "kfx_config_state.h"
#include "config_mods.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
// Wrappers registered with ports/sound_host_port.h's SoundHostPort (see
// docs/refactor/stage-13-enforce-and-document.md); bflib_sndlib.cpp/
// sound_manager.cpp can't read struct Game/kfx_*_state directly.
static char *get_music_track(void)
{
    return &kfx_game_state.music_track;
}

static char *get_music_fname(void)
{
    return kfx_game_state.music_fname;
}

static int64_t get_frame_skip(void)
{
    return kfx_net_state.frame_skip;
}

static TbBool get_easter_eggs_enabled(void)
{
    return kfx_sim_state.easter_eggs_enabled;
}

static int64_t get_last_level(void)
{
    return kfx_render_state.last_level;
}

static int64_t get_creature_model_count(void)
{
    return kfx_config_state.conf.crtr_conf.model_count;
}

static struct CreatureSounds *get_creature_sounds(int64_t crmodel)
{
    return &kfx_config_state.conf.crtr_conf.creature_sounds[crmodel];
}

static const struct ModConfigItem *get_mods_after_map(void)
{
    return mods_conf.after_map_item;
}

static int64_t get_mods_after_map_count(void)
{
    return mods_conf.after_map_cnt;
}

static const struct ModConfigItem *get_mods_after_campaign(void)
{
    return mods_conf.after_campaign_item;
}

static int64_t get_mods_after_campaign_count(void)
{
    return mods_conf.after_campaign_cnt;
}

static const struct ModConfigItem *get_mods_after_base(void)
{
    return mods_conf.after_base_item;
}

static int64_t get_mods_after_base_count(void)
{
    return mods_conf.after_base_cnt;
}

static uint32_t *get_sound_random_seed(void)
{
    return &kfx_sim_state.sound_random_seed;
}

static uint32_t *get_unsync_random_seed(void)
{
    return &kfx_sim_state.unsync_random_seed;
}

// Wrapper registered with ports/sound_host_port.h's SoundHostPort; config.c's
// creature_desc[] is a plain array, not a function, so it needs a getter
// to be passed through the callback table. prepare_file_path/_mod/_buf,
// prepare_file_fmtpath, creature_code_name, and thing_is_invalid are
// passed by direct reference below -- their real (config.c/
// config_creature.c/thing_data.c) signatures already match the callback
// fields exactly. See docs/refactor/todo/
// check-layering-symbol-level-blind-spot.md.
static const struct NamedCommand *get_creature_desc(void)
{
    return creature_desc;
}


const struct SoundHostPort kfx_game_sound_host_port = {
    .get_music_track = &get_music_track,
    .get_music_fname = &get_music_fname,
    .get_frame_skip = &get_frame_skip,
    .get_easter_eggs_enabled = &get_easter_eggs_enabled,
    .get_last_level = &get_last_level,
    .get_creature_model_count = &get_creature_model_count,
    .get_creature_sounds = &get_creature_sounds,
    .get_mods_after_map = &get_mods_after_map,
    .get_mods_after_map_count = &get_mods_after_map_count,
    .get_mods_after_campaign = &get_mods_after_campaign,
    .get_mods_after_campaign_count = &get_mods_after_campaign_count,
    .get_mods_after_base = &get_mods_after_base,
    .get_mods_after_base_count = &get_mods_after_base_count,
    .get_sound_random_seed = &get_sound_random_seed,
    .get_unsync_random_seed = &get_unsync_random_seed,
    .init_sound = &init_sound,
    .mute_audio = &mute_audio,
    .creature_code_name = &creature_code_name,
    .get_creature_desc = &get_creature_desc,
};
/******************************************************************************/
#ifdef __cplusplus
}
#endif
