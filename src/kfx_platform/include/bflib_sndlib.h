/******************************************************************************/
// Bullfrog Engine Emulation Library - for use to remake classic games like
// Syndicate Wars, Magic Carpet or Dungeon Keeper.
/******************************************************************************/
/** @file bflib_sndlib.h
 *     Header file for bflib_sndlib.c.
 * @par Purpose:
 *     Low-level sound and music related routines.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   KeeperFX Team
 * @date     16 Nov 2008 - 30 Dec 2008
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef BFLIB_SNDLIB_H
#define BFLIB_SNDLIB_H

#include "bflib_basics.h"
#include "bflib_sound.h"
#include "globals.h"

#define FIRST_REDBOOK_TRACK 2
#define LAST_REDBOOK_TRACK 7

#ifdef __cplusplus
extern "C" {
#endif

void FreeAudio(void);
void SetSoundMasterVolume(SoundVolume);
TbBool GetSoundInstalled(void);
void MonitorStreamedSoundTrack(void);
void * GetSoundDriver(void);
void StopAllSamples(void);
// Narrowed from a `const struct SoundSettings *` (kfx_game-owned,
// sounds.h) to the one field this actually reads. See
// docs/refactor/stage-13-enforce-and-document.md.
TbBool InitAudio(unsigned char max_number_of_samples);

// Defined in bflib_sndlib.cpp; declared here (not sounds.h, kfx_game)
// since this is their true implementation home. sounds.c (kfx_game)
// calls InitialiseSDLAudio() as part of its own init_sound() sequence;
// game_session_loop.cpp (kfx_apploop) calls ShutDownSDLAudio() at
// shutdown. See docs/refactor/stage-13-enforce-and-document.md.
int64_t InitialiseSDLAudio(void);
void ShutDownSDLAudio(void);

// Registers the resolved (lowercase, 3-char) language code and the
// "use OGG files instead of CD music" flag, so this file doesn't need
// config_keeperfx.h's install_info/features_enabled/get_language_lwrstr()
// directly. See docs/refactor/stage-02-decouple-bflib.md.
void bf_sndlib_set_audio_config(const char *language_lwrstr, TbBool no_cd_music);

// Injected pointer/value accessors for struct-Game-derived state this
// file's music/sound playback logic needs (see the "Real (not dead)
// coupling" comment at the top of bflib_sndlib.cpp and
// sound_manager.cpp's is_running_under_wine-style precedent). kfx_platform
// is the lowest-ranked library, so it can't reach kfx_game_state/
// kfx_net_state/kfx_sim_state/kfx_render_state/kfx_config_state
// directly -- the struct is defined here instead and implemented by
// small wrapper functions registered from main.cpp, mirroring
// bflib_inputctrl.h's InputFocusPredicates. get_music_track/
// get_music_fname return pointers into the real (kfx_game_state-owned)
// storage for direct read/write, since this file both reads and writes
// them. See docs/refactor/stage-13-enforce-and-document.md.
struct Thing;
struct CreatureSounds;
struct ModConfigItem;
struct SoundStateCallbacks {
    char *(*get_music_track)(void);
    char *(*get_music_fname)(void);
    int64_t (*get_frame_skip)(void);
    TbBool (*get_easter_eggs_enabled)(void);
    int64_t (*get_last_level)(void);
    // Narrowed from an opaque `struct CreatureConfig *(*get_creature_config)`
    // to these 2 entries (stage 13.3, docs/refactor/
    // stage-13-enforce-and-document.md) -- sound_manager.cpp only ever
    // read model_count and indexed creature_sounds[crmodel]; struct
    // CreatureSounds is already kfx_platform-owned (creature_sounds.h),
    // so this avoids sound_manager.cpp needing struct CreatureConfig
    // (the whole kfx_config creature-config aggregate) visible at all.
    int64_t (*get_creature_model_count)(void);
    struct CreatureSounds *(*get_creature_sounds)(int64_t crmodel);
    // config_mods.h (kfx_config) -- bflib_sndlib.cpp/sound_manager.cpp
    // both walk a mod list by value to resolve music/sound file paths;
    // struct ModConfigItem is kfx_platform-owned (mod_config_types.h),
    // struct ModsConfig/mods_conf stays kfx_config-owned. See
    // docs/refactor/stage-13-enforce-and-document.md.
    const struct ModConfigItem *(*get_mods_after_map)(void);
    int64_t (*get_mods_after_map_count)(void);
    const struct ModConfigItem *(*get_mods_after_campaign)(void);
    int64_t (*get_mods_after_campaign_count)(void);
    const struct ModConfigItem *(*get_mods_after_base)(void);
    int64_t (*get_mods_after_base_count)(void);
    // Backs SOUND_RANDOM(range) (game_merge.h, kfx_game) -- inlined here
    // as LbRandomSeries(range, get_sound_random_seed(), ...) instead,
    // since that macro itself only touches kfx_sim_state (kfx_sim), not
    // anything kfx_game-specific.
    uint32_t *(*get_sound_random_seed)(void);
    // Backs UNSYNC_RANDOM(range) (game_merge.h, kfx_game), used by
    // sound_manager.cpp -- same reasoning as get_sound_random_seed above.
    uint32_t *(*get_unsync_random_seed)(void);

    // sounds.h (kfx_game) -- init_sound() is kfx_game's orchestration of
    // platform audio init (reads kfx_game_state config, then calls back
    // down into InitAudio()/InitialiseSDLAudio()); sound_manager.cpp's
    // SoundManager::initialize() needs to trigger it as a lazy-init
    // fallback for callers that reach it before main() has already
    // called init_sound() directly. mute_audio() is bflib_inputctrl.cpp's
    // focus-lost/focus-gained handler.
    TbBool (*init_sound)(void);
    void (*mute_audio)(TbBool mute);

    // creature_control.h (kfx_sim) -- SoundManager::playCreatureSound()
    // bridges to kfx_sim's own creature-sound-index-to-sample resolution.
    void (*play_creature_sound)(struct Thing *thing, int64_t snd_idx, int64_t priority, int64_t use_flags);

    // config.h (kfx_config) file-path resolution -- both this file and
    // sound_manager.cpp need to locate sound/level/config asset files,
    // which requires kfx_config's install_info/mods-list state they
    // can't otherwise reach. Replaces a same-file bare-extern
    // forward-declaration precedent. See docs/refactor/todo/
    // check-layering-symbol-level-blind-spot.md.
    char *(*prepare_file_path)(int64_t fgroup, const char *fname);
    char *(*prepare_file_path_mod)(const char *mod_dir, int64_t fgroup, const char *fname);
    char *(*prepare_file_path_buf)(char *dst, int64_t dst_size, int64_t fgroup, const char *fname);
    char *(*prepare_file_fmtpath)(int64_t fgroup, const char *fmt_str, ...);

    // config_creature.h (kfx_config) -- creature model name lookup and
    // the creature-model name registry, used by sound_manager.cpp's
    // per-creature sound override commands and error logging.
    const char *(*creature_code_name)(ThingModel crmodel);
    const struct NamedCommand *(*get_creature_desc)(void);

    // thing_data.h (kfx_sim) -- bounds-checks a Thing pointer against
    // kfx_sim_state.things_data; sound_manager.cpp can't reach
    // kfx_sim_state directly.
    int64_t (*thing_is_invalid)(const struct Thing *thing);
};
void set_sound_state_callbacks(const struct SoundStateCallbacks *callbacks);
extern const struct SoundStateCallbacks *sound_state_callbacks;

TbBool IsSamplePlaying(SoundMilesID);
SoundVolume GetCurrentSoundMasterVolume(void);
void SetSampleVolume(SoundEmitterID, SoundSmplTblID, SoundVolume);
void SetSamplePan(SoundEmitterID, SoundSmplTblID, SoundPan);
void SetSamplePitch(SoundEmitterID, SoundSmplTblID, SoundPitch);
void toggle_bbking_mode(void);

/**
 * @brief Register a raw sound.dat effect ID to be transparently redirected to a custom bank ID.
 *
 * Called by config_sounds.c when a numeric-key entry (e.g. "777 = custom/boom.wav") is parsed.
 * The redirect is applied in play_sample() before bank dispatch; it only affects IDs in the
 * effect bank (0..g_speech_offset-1) and has no effect on speech or already-custom IDs.
 *
 * @param from_id  Raw effect ID as it would appear in sound.dat (e.g. 777)
 * @param to_id    Unified custom bank ID returned by sound_manager_load_named_sound()
 */
void sound_register_id_redirect(SoundSmplTblID from_id, SoundSmplTblID to_id);

/**
 * @brief Clear all registered raw-ID redirects.
 *
 * Called during audio teardown and alongside custom_sound_bank_clear() on level reload.
 */
void sound_clear_id_redirects(void);

/**
 * @brief Save a snapshot of the ID-redirect table and custom-bank watermark.
 *
 * Call after campaign + mod sounds finish loading. Pair with
 * sound_restore_id_redirect_snapshot() at the start of each level load.
 */
void sound_save_id_redirect_snapshot(void);

/**
 * @brief Restore the ID-redirect table and truncate the custom bank to the
 * saved watermark, freeing any sounds added at level scope.
 */
void sound_restore_id_redirect_snapshot(void);

void set_music_volume(SoundVolume);
TbBool play_music(const char * fname);
TbBool play_music_fgroup(int64_t fgroup, const char * fname);
TbBool play_music_track(int64_t);
void pause_music(void);
void resume_music(void);
void stop_music(TbBool fade_out);

/**
 * @brief Concurrency policy applied to a unified sound sample ID when it is
 * triggered by more than one emitter (Thing/UI source) at the same time.
 *
 */
enum SoundStackMode {
	SStack_Limit = 0, // at most max_instances concurrent instances; extra triggers are dropped
	SStack_Duck  = 1, // gain of every active instance is scaled down as concurrency rises
};

/**
 * @brief Register the stacking policy for a unified sound sample ID.
 *
 * If a sample ID has no registered policy, it does NOT use this Limit/Duck struct at all —
 * it instead falls back to a separate once-per-tick gate in play_sample() (see
 * g_tick_samples_last_tick in bflib_sndlib.cpp), which reproduces the OG behaviour (at most
 * one instance of a given sample plays at a time, regardless of how many emitters trigger
 * it). The gate is keyed by an internal tick counter advanced in MonitorStreamedSoundTrack()
 * rather than the in-game turn counter, so it still works correctly for UI/menu sounds
 * triggered outside active gameplay (e.g. the frontend/main menu), where the game turn
 * counter never advances.
 * That legacy gate and this policy table are mutually exclusive per sample ID: registering
 * an explicit policy here (even {Limit, 1}) opts the sample out of the legacy gate and into
 * the duration-based concurrency tracking this struct describes instead.
 *
 * @param smptbl_id      Unified sample ID (effect, speech, or custom bank).
 * @param mode           SStack_Limit or SStack_Duck.
 * @param max_instances  For SStack_Limit: hard cap (clamped to >= 1).
 *                        For SStack_Duck: 0 means uncapped, >0 also caps concurrency.
 */
void sound_register_stack_policy(SoundSmplTblID smptbl_id, unsigned char mode, int64_t max_instances);

/**
 * @brief Clear all registered stacking policies (samples with no policy revert to the
 * legacy once-per-turn gate described above).
 */
void sound_clear_stack_policies(void);

#ifdef __cplusplus
}
#endif
#endif
