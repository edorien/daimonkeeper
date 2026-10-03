/******************************************************************************/
// Bullfrog Engine Emulation Library - for use to remake classic games like
// Syndicate Wars, Magic Carpet or Dungeon Keeper.
/******************************************************************************/
/** @file bflib_sound.h
 *     Header file for bflib_sound.c.
 * @par Purpose:
 *     Sound and music related routines.
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
#ifndef BFLIB_SOUND_H
#define BFLIB_SOUND_H

#include "bflib_basics.h"
#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
#define SOUNDS_MAX_COUNT  16
#define SOUND_EMITTERS_MAX 128
// Moved from kfx_game's sounds.h (stage 13.3, docs/refactor/
// stage-13-enforce-and-document.md) -- plain integer literals with no
// type dependencies, used by kfx_platform's own bflib_sound.c/
// bflib_sndlib.cpp as well as every higher-ranked library that plays
// sounds; kfx_platform is the true floor.
#define FULL_LOUDNESS 256
#define NORMAL_PITCH 100
// Same move, same reasoning -- only ever used by bflib_sound.c/
// bflib_sndlib.cpp's own Mix_Playing()/Mix_PlayChannel()/
// Mix_HaltChannel() calls.
#define MIX_SPEECH_CHANNEL 0
/******************************************************************************/
#pragma pack(1)

// Type definitions

/** Sound SFXID parameter from bank table. */
typedef unsigned char SoundSFXID;
/** Sound emitter ID. */
typedef int64_t SoundEmitterID;
/** Sound sample ID in bank table. */
typedef int64_t SoundSmplTblID;
/** Volume level indicator, normal is 256. */
typedef int64_t SoundVolume;
/** Pitch level indicator, normal is 100. */
typedef int64_t SoundPitch;
/** Pan level indicator. */
typedef int64_t SoundPan;
/** Miles Sound ID. */
typedef int64_t SoundMilesID;

enum SoundEmitterFlags {
    Emi_IsAllocated  = 0x01,
    Emi_IsPlaying    = 0x02,
    Emi_IsMoving     = 0x04,
};

enum SoundSampleFlags {
    Smp_NoPitchUpdate  = 0x01,
    Smp_NoVolumeUpdate = 0x02,
};

typedef void *SndData;
typedef int64_t (*S3D_LineOfSight_Func)(int64_t, int64_t, int64_t, int64_t, int64_t, int64_t);

struct SoundCoord3d {
    uint64_t val_x;
    uint64_t val_y;
    uint64_t val_z;
};

struct SoundEmitter {
    unsigned char flags;
    unsigned char emitter_flags;
    int64_t index;
    struct SoundCoord3d pos;
    unsigned char reserved[6];
    int64_t pitch_doppler;
    unsigned char curr_pitch;
    unsigned char target_pitch;
};

struct SoundReceiver { // sizeof = 17
    struct SoundCoord3d pos;
    int64_t rotation_angle_x;
    int64_t rotation_angle_y;
    int64_t rotation_angle_z;
    uint64_t flags;
    unsigned char sensivity; // 0-RECEIVER_FULL_SENSITIVITY; the dungeon view lowers it as the camera zooms out
};
#define RECEIVER_FULL_SENSITIVITY 64

struct S3DSample { // sizeof = 37
  uint64_t priority;
  uint64_t time_turn;
  int64_t smptbl_id;
  int64_t base_pitch;
  int64_t pan;
  int64_t volume;
  SoundMilesID mss_id;
  struct SoundEmitter *emit_ptr;
  int64_t emit_idx;
  char repeat_count; // signed
  unsigned char flags;
  unsigned char is_playing;
  unsigned char sfxid;
  uint64_t base_volume;
};

/******************************************************************************/
// Exported variables
// The volume settings (sound/music/mentor in settings.toml, ATMOS_VOLUME in
// the base config) are stored 0-VOLUME_SETTING_MAX and shown as 0-100.
#define VOLUME_SETTING_MAX 255
// ATMOS_VOLUME: 0-ATMOS_VOLUME_MAX, a volume setting like the others.
extern int64_t atmos_sound_volume;
#define ATMOS_VOLUME_MAX VOLUME_SETTING_MAX
extern TbBool SoundDisabled;
extern int64_t MaxSoundDistance;
extern struct SoundReceiver Receiver;
extern int64_t Non3DEmitter;
extern int64_t SpeechEmitter;
extern struct SoundEmitter emitter[128];
// Un-static'd for the same reason as MaxNoSounds/SampleList above --
// direct fixture access, no real-OpenAL-touching call needed.
extern int64_t MaxNoSounds;
extern struct S3DSample SampleList[SOUNDS_MAX_COUNT];
#pragma pack()
/******************************************************************************/
// Exported functions
int64_t S3DSetSoundReceiverPosition(int64_t pos_x, int64_t pos_y, int64_t pos_z);
int64_t S3DSetSoundReceiverOrientation(int64_t ori_a, int64_t ori_b, int64_t ori_c);
// The following had real external linkage but no header declaration at
// all (only ever called from within bflib_sound.c itself) -- added so
// tests can call them directly, the usual "add the missing declaration"
// fix.
TbBool S3DSoundEmitterInvalid(struct SoundEmitter *emit);
TbBool emitter_is_playing(struct SoundEmitter *emit);
TbBool remove_active_samples_from_emitter(struct SoundEmitter *emit);
SoundEmitterID allocate_free_sound_emitter(void);
void delete_sound_emitter(SoundEmitterID idx);
void delete_all_sound_emitters(void);
void init_sample_list(void);
int64_t get_sample_id(struct S3DSample *sample);
int64_t get_sound_distance(const struct SoundCoord3d *pos1, const struct SoundCoord3d *pos2);
int64_t get_sound_squareedge_distance(const struct SoundCoord3d *pos1, const struct SoundCoord3d *pos2);
int64_t get_emitter_distance(struct SoundReceiver *recv, struct SoundEmitter *emit);
int64_t get_emitter_sight(struct SoundReceiver *recv, struct SoundEmitter *emit);
int64_t get_emitter_volume(const struct SoundReceiver *recv, const struct SoundEmitter *emit, int64_t dist);
int64_t get_emitter_pan(const struct SoundReceiver *recv, const struct SoundEmitter *emit);
int64_t get_emitter_pitch_from_doppler(const struct SoundReceiver *recv, struct SoundEmitter *emit);
int64_t get_emitter_pan_volume_pitch(struct SoundReceiver *recv, struct SoundEmitter *emit, int64_t *pan, int64_t *volume, int64_t *pitch);
int64_t set_emitter_pan_volume_pitch(struct SoundEmitter *emit, int64_t pan, int64_t volume, int64_t pitch);
int64_t find_slot(int64_t fild8, struct SoundEmitter *emit, int64_t ctype, int64_t spcmax);
int64_t dummy_line_of_sight_function(int64_t receiver_x, int64_t receiver_y, int64_t receiver_z, int64_t emitter_x, int64_t emitter_y, int64_t emitter_z);
void S3DSetSoundReceiverSensitivity(int64_t nsensivity);
int64_t S3DDestroySoundEmitter(SoundEmitterID);
TbBool S3DEmitterHasFinishedPlaying(SoundEmitterID);
TbBool S3DMoveSoundEmitterTo(SoundEmitterID, int64_t x, int64_t y, int64_t z);
int64_t S3DInit(void);
int64_t S3DSetNumberOfSounds(int64_t nMaxSounds);
int64_t S3DSetMaximumSoundDistance(int64_t nDistance);
TbBool S3DAddSampleToEmitterPri(SoundEmitterID, SoundSmplTblID, SoundPitch, SoundVolume, int64_t repeats, char ctype, int64_t flags, int64_t priority);
int64_t S3DCreateSoundEmitterPri(int64_t x, int64_t y, int64_t z, SoundSmplTblID, SoundPitch, SoundVolume, int64_t repeats, int64_t flags, int64_t priority);
TbBool S3DEmitterIsAllocated(SoundEmitterID);
TbBool S3DEmitterIsPlayingAnySample(SoundEmitterID);
TbBool S3DEmitterIsPlayingSample(SoundEmitterID, SoundSmplTblID);
TbBool S3DDeleteSampleFromEmitter(SoundEmitterID, SoundSmplTblID);
TbBool S3DDeleteAllSamplesFromEmitter(SoundEmitterID);
TbBool S3DDestroySoundEmitterAndSamples(SoundEmitterID);
void S3DSetLineOfSightFunction(S3D_LineOfSight_Func);
void S3DSetDeadzoneRadius(int64_t dzradius);

void play_non_3d_sample(SoundSmplTblID);
void play_non_3d_sample_no_overlap(SoundSmplTblID);
void play_atmos_sound(SoundSmplTblID);
// The loudness play_atmos_sound() uses, and keeps playing atmos sounds at:
// atmos_volume (ATMOS_VOLUME) scaled by the sound effects volume and by the
// receiver sensitivity (zooming out lowers it), 0 when muted.
SoundVolume atmos_sound_loudness(int64_t atmos_volume, int64_t sound_volume, SoundVolume master_volume, int64_t sensitivity);

// Registers the current sound/mentor volume settings (0-VOLUME_SETTING_MAX, matching
// struct GameSettings), so this file doesn't need config_settings.h's
// mutable `settings` global directly. Called whenever the settings change.
// See docs/refactor/stage-02-decouple-bflib.md.
void bf_sound_set_volume_config(unsigned char sound_volume, int64_t mentor_volume);

// Volume settings turn into gain on a curve, gain = (setting/max)^2, so the
// sliders are even to the ear: a linear gain puts 10% at only -20 dB, which
// still sounds about a quarter as loud as full. On the curve 10% is -40 dB,
// 50% is -12 dB. Every place a volume setting becomes a gain goes through
// one of these (music and streamed speech inside set_music_volume() and
// play_streamed_sample()/set_streamed_sample_volume(), which take a setting,
// possibly faded, and apply it themselves).
double volume_setting_gain(int64_t setting);        // 0.0-1.0
SoundVolume volume_setting_curve(int64_t setting);  // the same, back on the 0-VOLUME_SETTING_MAX scale
// A sound effect's (or OpenAL speech's) loudness scale, 0-2*FULL_LOUDNESS:
// FULL_LOUDNESS * 2 * gain, keeping the old headroom where a full slider
// plays effects at twice their sample loudness.
SoundVolume volume_setting_scale(int64_t setting);

// Registers the configured atmospheric-sound sample ID range and enabled
// flag (resolved once from config at startup), so this file doesn't need
// config_keeperfx.h's AtmosStart/AtmosEnd/AtmosRepeat/atmos_sounds_enabled()
// directly. See docs/refactor/stage-02-decouple-bflib.md.
void bf_sound_set_atmos_config(int64_t atmos_start, int64_t atmos_end, int64_t atmos_repeat, TbBool atmos_enabled);
int64_t sound_emitter_in_use(SoundEmitterID);
SoundMilesID play_sample(SoundEmitterID, SoundSmplTblID, SoundVolume, SoundPan, SoundPitch, char repeats, unsigned char ctype);
void stop_sample(SoundEmitterID, SoundSmplTblID);
int64_t speech_sample_playing(void);
TbBool is_streamed_sample_playing(void);
int64_t play_speech_sample(SoundSmplTblID);
int64_t stop_emitter_samples(struct SoundEmitter *emit);
TbBool process_sound_emitters(void);
void increment_sample_times(void);
TbBool process_sound_samples(void);
void stop_atmos_sounds(void);

struct SoundEmitter* S3DGetSoundEmitter(SoundEmitterID);
SoundEmitterID get_emitter_id(struct SoundEmitter *);
void kick_out_sample(SoundSmplTblID);
SoundSFXID get_sample_sfxid(SoundSmplTblID smptbl_id);
SoundSmplTblID get_speech_offset(void);
SoundSmplTblID get_custom_offset(void);

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
