/******************************************************************************/
// Bullfrog Engine Emulation Library - for use to remake classic games like
// Syndicate Wars, Magic Carpet or Dungeon Keeper.
/******************************************************************************/
/** @file bflib_sound.c
 *     Sound and music related routines.
 * @par Purpose:
 *     Sound and music routines to use in games.
 * @par Comment:
 *     None.
 * @author   KeeperFX Team
 * @date     16 Nov 2008 - 30 Dec 2008
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "bflib_sound.h"

#include <string.h>
#include <stdio.h>
#include <math.h>

#include "bflib_basics.h"
#include "bflib_math.h"
#include "bflib_sndlib.h"
#include "bflib_fileio.h"
#include "bflib_planar.h"
#include "globals.h"
#include "ports/sound_host_port.h"
#include "post_inc.h"

#define INVALID_SOUND_EMITTER (&emitter[0])

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
// Global variables
static int64_t NoSoundEmitters = SOUND_EMITTERS_MAX;
struct SoundEmitter emitter[128];
// MaxNoSounds/SampleList were file-scope static; un-static'd (declared
// extern in bflib_sound.h) so tests can construct "sample already
// playing" scenarios via direct field writes, without ever calling the
// real-OpenAL-touching start_emitter_playing()/play_sample() -- same
// "expose the module-level global" pattern kfx_config's `campaign` used.
int64_t MaxNoSounds;
struct S3DSample SampleList[SOUNDS_MAX_COUNT];
static S3D_LineOfSight_Func LineOfSightFunction;
static int64_t deadzone_radius;

TbBool SoundDisabled;
int64_t atmos_sound_volume = 128;
int64_t MaxSoundDistance;
struct SoundReceiver Receiver;
int64_t Non3DEmitter;
int64_t SpeechEmitter;

// See bf_sound_set_volume_config()/bf_sound_set_atmos_sample_range() and
// docs/refactor/stage-02-decouple-bflib.md.
static unsigned char bf_sound_volume = 127;
static int64_t bf_mentor_volume = 127;
static int64_t bf_atmos_start = 1014;
static int64_t bf_atmos_end = 1034;
static int64_t bf_atmos_repeat = 1013;
static TbBool bf_atmos_enabled = true;

void bf_sound_set_volume_config(unsigned char sound_volume, int64_t mentor_volume)
{
    bf_sound_volume = sound_volume;
    bf_mentor_volume = mentor_volume;
}

double volume_setting_gain(int64_t setting)
{
    const double frac = (double)clamp(setting, 0, VOLUME_SETTING_MAX) / VOLUME_SETTING_MAX;
    return frac * frac;
}

SoundVolume volume_setting_curve(int64_t setting)
{
    return (SoundVolume)lround(VOLUME_SETTING_MAX * volume_setting_gain(setting));
}

SoundVolume volume_setting_scale(int64_t setting)
{
    return (SoundVolume)lround(2.0 * FULL_LOUDNESS * volume_setting_gain(setting));
}

void bf_sound_set_atmos_config(int64_t atmos_start, int64_t atmos_end, int64_t atmos_repeat, TbBool atmos_enabled)
{
    bf_atmos_start = atmos_start;
    bf_atmos_end = atmos_end;
    bf_atmos_repeat = atmos_repeat;
    bf_atmos_enabled = atmos_enabled;
}
/******************************************************************************/
// Internal routines
SoundEmitterID allocate_free_sound_emitter(void);
void delete_sound_emitter(SoundEmitterID idx);
int64_t start_emitter_playing(struct SoundEmitter *emit, SoundSmplTblID smptbl_id, int64_t smpitch, SoundVolume loudness, int64_t fild1D, int64_t ctype, unsigned char flags, int64_t priority);
void init_sample_list(void);
void delete_all_sound_emitters(void);
SoundEmitterID get_emitter_id(struct SoundEmitter *emit);
int64_t get_sample_id(struct S3DSample *sample);
void kick_out_sample(int64_t smpl_id);
TbBool emitter_is_playing(struct SoundEmitter *emit);
TbBool remove_active_samples_from_emitter(struct SoundEmitter *emit);
static void update_atmos_sound_volumes(struct SoundEmitter *emit);
/******************************************************************************/
// Functions

int64_t dummy_line_of_sight_function(int64_t receiver_x, int64_t receiver_y, int64_t receiver_z, int64_t emitter_x, int64_t emitter_y, int64_t emitter_z)
{
    return 1;
}

int64_t S3DInit(void)
{
    // Clear emitters memory
    delete_all_sound_emitters();
    // Reset sound receiver data
    memset(&Receiver, 0, sizeof(struct SoundReceiver));
    S3DSetSoundReceiverPosition(0, 0, 0);
    S3DSetSoundReceiverOrientation(0, 0, 0);
    S3DSetSoundReceiverSensitivity(64);
    S3DSetLineOfSightFunction(dummy_line_of_sight_function);
    S3DSetDeadzoneRadius(0);
    S3DSetNumberOfSounds(SOUNDS_MAX_COUNT);
    init_sample_list();
    return 1;
}

int64_t S3DSetNumberOfSounds(int64_t nMaxSounds)
{
    if (nMaxSounds > SOUNDS_MAX_COUNT)
        nMaxSounds = SOUNDS_MAX_COUNT;
    if (nMaxSounds < 1)
        nMaxSounds = 1;
    MaxNoSounds = nMaxSounds;
    return true;
}

struct SoundEmitter* S3DGetSoundEmitter(SoundEmitterID eidx)
{
    if ((eidx < 0) || (eidx >= SOUND_EMITTERS_MAX))
    {
        WARNLOG("Tried to get outranged emitter %" PRId64,(int64_t)(eidx));
        return INVALID_SOUND_EMITTER;
    }
    return &emitter[eidx];
}

TbBool S3DSoundEmitterInvalid(struct SoundEmitter *emit)
{
    if (emit == NULL)
        return true;
    if (emit == INVALID_SOUND_EMITTER)
        return true;
    return false;
}

TbBool S3DEmitterIsPlayingSample(SoundEmitterID eidx, SoundSmplTblID smpl_idx)
{
    struct SoundEmitter* emit = S3DGetSoundEmitter(eidx);
    if (S3DSoundEmitterInvalid(emit))
        return false;
    for (int64_t i = 0; i < MaxNoSounds; i++)
    {
        struct S3DSample* sample = &SampleList[i];
        if ((sample->is_playing != 0) && (sample->emit_ptr == emit))
        {
            if (sample->smptbl_id == smpl_idx) {
                return true;
            }
        }
    }
    return false;
}

TbBool S3DDeleteSampleFromEmitter(SoundEmitterID eidx, SoundSmplTblID smpl_idx)
{
    struct SoundEmitter* emit = S3DGetSoundEmitter(eidx);
    if (S3DSoundEmitterInvalid(emit))
        return false;
    for (int64_t i = 0; i < MaxNoSounds; i++)
    {
        struct S3DSample* sample = &SampleList[i];
        if ((sample->is_playing != 0) && (sample->emit_ptr == emit))
        {
            if (sample->smptbl_id == smpl_idx) {
                sample->is_playing = 0;
                stop_sample(get_emitter_id(emit), sample->smptbl_id);
                return true;
            }
        }
    }
    return false;
}

TbBool S3DDeleteAllSamplesFromEmitter(SoundEmitterID eidx)
{
    struct SoundEmitter* emit = S3DGetSoundEmitter(eidx);
    if (S3DSoundEmitterInvalid(emit)) {
        ERRORLOG("Trying to delete samples from invalid emitter %" PRId64,(int64_t)(eidx));
        return false;
    }
    return stop_emitter_samples(emit);
}

int64_t S3DSetMaximumSoundDistance(int64_t nDistance)
{
    if (nDistance > 65536)
        nDistance = 65536;
    if (nDistance < 1)
        nDistance = 1;
    MaxSoundDistance = nDistance;
    return 1;
}

int64_t S3DSetSoundReceiverPosition(int64_t pos_x, int64_t pos_y, int64_t pos_z)
{
    Receiver.pos.val_x = pos_x;
    Receiver.pos.val_y = pos_y;
    Receiver.pos.val_z = pos_z;
    return 1;
}

int64_t S3DSetSoundReceiverOrientation(int64_t ori_a, int64_t ori_b, int64_t ori_c)
{
    Receiver.rotation_angle_x = ori_a & ANGLE_MASK;
    Receiver.rotation_angle_y = ori_b & ANGLE_MASK;
    Receiver.rotation_angle_z = ori_c & ANGLE_MASK;
    return 1;
}

void S3DSetSoundReceiverSensitivity(int64_t nsensivity)
{
    Receiver.sensivity = nsensivity;
}

/**
 * Destroys sound emitter, without stopping samples which are being played.
 * @param eidx Sound emitter id.
 * @return True if emitter was destroyed, false if it already was.
 */
int64_t S3DDestroySoundEmitter(SoundEmitterID eidx)
{
    struct SoundEmitter* emit = S3DGetSoundEmitter(eidx);
    if (S3DSoundEmitterInvalid(emit)) {
        ERRORLOG("Invalid emitter %" PRId64,(int64_t)(eidx));
        return false;
    }
    remove_active_samples_from_emitter(emit);
    delete_sound_emitter(eidx);
    return true;
}

/**
 * Destroys sound emitter, stopping all samples which are being played.
 * @param eidx Sound emitter id.
 * @return True if emitter was destroyed, false if it already was.
 */
TbBool S3DDestroySoundEmitterAndSamples(SoundEmitterID eidx)
{
    struct SoundEmitter* emit = S3DGetSoundEmitter(eidx);
    if (S3DSoundEmitterInvalid(emit)) {
        ERRORLOG("Invalid emitter %" PRId64,(int64_t)(eidx));
        return false;
    }
    stop_emitter_samples(emit);
    delete_sound_emitter(eidx);
    return true;
}

TbBool S3DEmitterIsAllocated(SoundEmitterID eidx)
{
    SYNCDBG(17,"Starting");
    struct SoundEmitter* emit = S3DGetSoundEmitter(eidx);
    if (S3DSoundEmitterInvalid(emit))
        return false;
    return ((emit->flags & Emi_IsAllocated) != 0);
}

TbBool S3DEmitterHasFinishedPlaying(SoundEmitterID eidx)
{
    struct SoundEmitter* emit = S3DGetSoundEmitter(eidx);
    if (S3DSoundEmitterInvalid(emit))
        return true;
    return ((emit->flags & Emi_IsPlaying) == 0);
}

TbBool S3DMoveSoundEmitterTo(SoundEmitterID eidx, int64_t x, int64_t y, int64_t z)
{
    if (!S3DEmitterIsAllocated(eidx))
        return false;
    struct SoundEmitter* emit = S3DGetSoundEmitter(eidx);
    emit->pos.val_x = x;
    emit->pos.val_y = y;
    emit->pos.val_z = z;
    emit->flags |= Emi_IsMoving;
    return true;
}

TbBool S3DAddSampleToEmitterPri(SoundEmitterID eidx, SoundSmplTblID smptbl_id, SoundPitch pitch, SoundVolume loudness, int64_t repeats, char ctype, int64_t flags, int64_t priority)
{
    struct SoundEmitter* emit = S3DGetSoundEmitter(eidx);
    return start_emitter_playing(emit, smptbl_id, pitch, loudness, repeats, ctype, flags, priority) != 0;
}

int64_t S3DCreateSoundEmitterPri(int64_t x, int64_t y, int64_t z, SoundSmplTblID smptbl_id, SoundPitch pitch, SoundVolume loudness, int64_t repeats, int64_t flags, int64_t priority)
{
    int64_t eidx = allocate_free_sound_emitter();
    struct SoundEmitter* emit = S3DGetSoundEmitter(eidx);
    if (S3DSoundEmitterInvalid(emit))
        return 0;
    emit->pos.val_x = x;
    emit->pos.val_y = y;
    emit->pos.val_z = z;
    emit->emitter_flags = flags;
    emit->curr_pitch = 100;
    emit->target_pitch = 100;
    if (start_emitter_playing(emit, smptbl_id, pitch, loudness, repeats, 3, flags, priority))
        return eidx;
    delete_sound_emitter(eidx);
    return 0;
}

TbBool S3DEmitterIsPlayingAnySample(SoundEmitterID eidx)
{
    SYNCDBG(17,"Starting");
    if (MaxNoSounds <= 0)
        return false;
    struct SoundEmitter* emit = S3DGetSoundEmitter(eidx);
    if (S3DSoundEmitterInvalid(emit))
    {
        ERRORLOG("Invalid emiter %" PRId64,(int64_t)(eidx));
        return false;
    }
    TbBool is_playing = emitter_is_playing(emit);
    SYNCDBG(17,"Emitter %" PRId64 " %s playing",(int64_t)(eidx),is_playing?"is":"not");
    return is_playing;
}

void S3DSetLineOfSightFunction(S3D_LineOfSight_Func callback)
{
    LineOfSightFunction = callback;
}

void S3DSetDeadzoneRadius(int64_t dzradius)
{
    deadzone_radius = dzradius;
}

SoundEmitterID get_emitter_id(struct SoundEmitter *emit)
{
    return (int64_t)emit->index + 4000;
}

int64_t get_sample_id(struct S3DSample *sample)
{
    return (int64_t)sample->emit_idx + 4000;
}

int64_t sound_emitter_in_use(SoundEmitterID eidx)
{
    return S3DEmitterIsAllocated(eidx);
}

int64_t get_sound_distance(const struct SoundCoord3d *pos1, const struct SoundCoord3d *pos2)
{
    int64_t dist_x = max(pos1->val_x, pos2->val_x) - min(pos1->val_x, pos2->val_x);
    int64_t dist_y = max(pos1->val_y, pos2->val_y) - min(pos1->val_y, pos2->val_y);
    int64_t dist_z = max(pos1->val_z, pos2->val_z) - min(pos1->val_z, pos2->val_z);
    // Make sure we're not exceeding sqrt(INT32_MAX/3), to fit the final result in long
    if (dist_x > 26754)
        dist_x = 26754;
    if (dist_y > 26754)
        dist_y = 26754;
    if (dist_z > 26754)
        dist_z = 26754;
    return LbSqrL( dist_y*dist_y + dist_x*dist_x + dist_z*dist_z );
}

int64_t get_sound_squareedge_distance(const struct SoundCoord3d *pos1, const struct SoundCoord3d *pos2)
{
    int64_t dist_x = max(pos1->val_x, pos2->val_x) - min(pos1->val_x, pos2->val_x);
    int64_t dist_y = max(pos1->val_y, pos2->val_y) - min(pos1->val_y, pos2->val_y);
    int64_t dist_z = max(pos1->val_z, pos2->val_z) - min(pos1->val_z, pos2->val_z);
    // Make sure we're not exceeding INT32_MAX/3
    if (dist_x > INT32_MAX/3)
        dist_x = INT32_MAX/3;
    if (dist_y > INT32_MAX/3)
        dist_y = INT32_MAX/3;
    if (dist_z > INT32_MAX/3)
        dist_z = INT32_MAX/3;
    return dist_x + dist_y + dist_z;
}

int64_t get_emitter_distance(struct SoundReceiver *recv, struct SoundEmitter *emit)
{
    int64_t dist = get_sound_distance(&recv->pos, &emit->pos);
    if (dist > MaxSoundDistance-1)
        dist = MaxSoundDistance-1;
    if (dist < 0)
        dist = 0;
    return dist;
}

int64_t get_emitter_sight(struct SoundReceiver *recv, struct SoundEmitter *emit)
{
    return LineOfSightFunction(recv->pos.val_x, recv->pos.val_y, recv->pos.val_z, emit->pos.val_x, emit->pos.val_y, emit->pos.val_z);
}

int64_t get_emitter_volume(const struct SoundReceiver *recv, const struct SoundEmitter *emit, int64_t dist)
{
    int64_t i = dist - deadzone_radius;
    if (i < 0) i = 0;
    int64_t n = MaxSoundDistance - deadzone_radius;
    long long sens = recv->sensivity;
    long long vol = (127 - 127 * i / n) * sens;
    return (vol >> 6);
}

int64_t get_emitter_pan(const struct SoundReceiver *recv, const struct SoundEmitter *emit)
{
    if ((recv->flags & Emi_IsAllocated) != 0) {
      return 64;
    }
    int64_t diff_x = emit->pos.val_x - (int64_t)recv->pos.val_x;
    int64_t diff_y = emit->pos.val_y - (int64_t)recv->pos.val_y;
    // Faster way of doing simple thing: radius = sqrt(dist_x*dist_y);
    int64_t radius = LbDiagonalLength(llabs(diff_x), llabs(diff_y));
    if (radius < deadzone_radius) {
      return 64;
    }
    int64_t angle_b = LbArcTanAngle(diff_x, diff_y);
    int64_t angle_a = recv->rotation_angle_x;
    int64_t angdiff = get_angle_difference(angle_a, angle_b);
    int64_t angsign = get_angle_sign(angle_a, angle_b);
    int64_t i = (radius - deadzone_radius) * LbSinL(angsign * angdiff) >> 16;
    int64_t pan = (i << 6) / (MaxSoundDistance - deadzone_radius) + 64;
    if (pan > 127)
        pan = 127;
    if (pan < 0)
        pan = 0;
    return pan;
}

int64_t get_emitter_pitch_from_doppler(const struct SoundReceiver *recv, struct SoundEmitter *emit)
{
    int64_t target_pitch;
    int64_t doppler_distance = get_sound_squareedge_distance(&emit->pos, &recv->pos);
    int64_t delta = doppler_distance - emit->pitch_doppler;
    if (delta > 256)
        delta = 256;
    if (delta < 0)
        delta = 0;
    if (delta <= 0)
        target_pitch = 100;
    else
        target_pitch = 100 - 20 * delta / 256;
    int64_t next_pitch = emit->curr_pitch;
    if (next_pitch != target_pitch)
    {
        next_pitch += (llabs(target_pitch - next_pitch) >> 1);
    }
    emit->target_pitch = target_pitch;
    emit->curr_pitch = next_pitch;
    emit->pitch_doppler = doppler_distance;
    //texty += 16; // I have no idea what is this.. garbage.
    return emit->curr_pitch;
}

int64_t get_emitter_pan_volume_pitch(struct SoundReceiver *recv, struct SoundEmitter *emit, int64_t *pan, int64_t *volume, int64_t *pitch)
{
    TbBool on_sight;
    if ((emit->emitter_flags & 0x08) != 0)
    {
        *volume = 127;
        *pan = 64;
        *pitch = 100;
        return 1;
    }
    int64_t dist = get_emitter_distance(recv, emit);
    if ((emit->emitter_flags & 0x04) != 0) {
        on_sight = 1;
    } else {
        on_sight = get_emitter_sight(recv, emit);
    }
    int64_t i = get_emitter_volume(recv, emit, dist);
    if (on_sight) {
        *volume = i;
    } else {
        *volume = i >> 1;
    }
    i = (dist - deadzone_radius);
    if (i >= 128) {
        *pan = get_emitter_pan(recv, emit);
    } else {
        *pan = 64;
    }
    if ((emit->flags & Emi_IsMoving) != 0) {
        *pitch = get_emitter_pitch_from_doppler(recv, emit);
    } else {
        *pitch = 100;
    }
    //ERRORLOG("emit%2d expected %3d,%3d,%3d got %3d,%3d,%3d dist %3d",(int)emit->index ,(int64_t)(opan), (int64_t)(ovolume), (int64_t)(opitch), (int64_t)(*pan), (int64_t)(*volume), (int64_t)(*pitch), (int64_t)(dist));
    return 1;
}

int64_t set_emitter_pan_volume_pitch(struct SoundEmitter *emit, int64_t pan, int64_t volume, int64_t pitch)
{
    for (int64_t i = 0; i < MaxNoSounds; i++)
    {
        struct S3DSample* sample = &SampleList[i];
        if ((sample->is_playing != 0) && (sample->emit_ptr == emit))
        {
            if ((sample->flags & Smp_NoVolumeUpdate) == 0) {
              SetSampleVolume(get_emitter_id(emit), sample->smptbl_id, volume * (int64_t)sample->base_volume / 256);
              SetSamplePan(get_emitter_id(emit), sample->smptbl_id, pan);
            }
            if ((sample->flags & Smp_NoPitchUpdate) == 0) {
              SetSamplePitch(get_emitter_id(emit), sample->smptbl_id, pitch * (int64_t)sample->base_pitch / 100);
            }
        }
    }
    return 1;
}

TbBool process_sound_emitters(void)
{
    struct SoundEmitter *emit;
    int64_t pan;
    int64_t volume;
    int64_t pitch;
    int64_t i;
    for (i = 0; i < NoSoundEmitters; i++)
    {
        emit = S3DGetSoundEmitter(i);
        if ( ((emit->flags & Emi_IsAllocated) != 0) && ((emit->flags & Emi_IsPlaying) != 0) )
        {
            if ( emitter_is_playing(emit) )
            {
                if (i == Non3DEmitter) {
                    update_atmos_sound_volumes(emit); // its other samples are short; don't touch them
                    continue;
                } else if (i == SpeechEmitter) {
                    continue; // don't touch
                } else {
                    get_emitter_pan_volume_pitch(&Receiver, emit, &pan, &volume, &pitch);
                    set_emitter_pan_volume_pitch(emit, pan, volume, pitch);
                }
            } else
            {
                emit->flags ^= Emi_IsPlaying;
            }
        }
    }
    return true;
}

TbBool emitter_is_playing(struct SoundEmitter *emit)
{
    for (int64_t i = 0; i < MaxNoSounds; i++)
    {
        struct S3DSample* sample = &SampleList[i];
        if ((sample->is_playing != 0) && (sample->emit_ptr == emit))
        {
            return true;
        }
    }
    return false;
}

TbBool remove_active_samples_from_emitter(struct SoundEmitter *emit)
{
    for (int64_t i = 0; i < MaxNoSounds; i++)
    {
        struct S3DSample* sample = &SampleList[i];
        if ( (sample->is_playing != 0) && (sample->emit_ptr == emit) )
        {
            if (sample->repeat_count == -1)
            {
                stop_sample(get_emitter_id(emit), sample->smptbl_id);
                sample->is_playing = 0;
            }
            sample->emit_ptr = NULL;
        }
    }
    return true;
}

int64_t stop_emitter_samples(struct SoundEmitter *emit)
{
    int64_t num_stopped = 0;
    for (int64_t i = 0; i < MaxNoSounds; i++)
    {
        struct S3DSample* sample = &SampleList[i];
        if ((sample->is_playing != 0) && (sample->emit_ptr == emit))
        {
            stop_sample(get_emitter_id(emit), sample->smptbl_id);
            sample->is_playing = 0;
            num_stopped++;
        }
    }
    return num_stopped;
}

int64_t find_slot(int64_t fild8, struct SoundEmitter *emit, int64_t ctype, int64_t spcmax)
{
    struct S3DSample *sample;
    int64_t i;
    int64_t spcval = INT32_MAX;
    int64_t min_sample_id = SOUNDS_MAX_COUNT;
    if ((ctype == 2) || (ctype == 3))
    {
        for (i=0; i < MaxNoSounds; i++)
        {
            sample = &SampleList[i];
            if ( (sample->is_playing) && (sample->emit_ptr != NULL) )
            {
                if ( (sample->emit_ptr->index == emit->index)
                  && (sample->smptbl_id == fild8) )
                    return i;
            }
        }
    }
    for (i=0; i < MaxNoSounds; i++)
    {
        sample = &SampleList[i];
        if (sample->is_playing == 0)
            return i;
        if (spcval > (int64_t) sample->priority)
        {
            min_sample_id = i;
            spcval = sample->priority;
        }
    }
    if (spcval >= spcmax)
    {
        return -1;
    }
    kick_out_sample(min_sample_id);
    return min_sample_id;
}

void play_non_3d_sample(SoundSmplTblID sample_idx)
{
    if (SoundDisabled)
        return;
    if (GetCurrentSoundMasterVolume() <= 0)
        return;

    // Set sound volume setting
    SoundVolume adjusted_volume = volume_setting_scale(bf_sound_volume);

    if (Non3DEmitter != 0)
      if (!sound_emitter_in_use(Non3DEmitter))
      {
          ERRORLOG("Non 3d Emitter has been deleted!");
          Non3DEmitter = 0;
      }
    if (Non3DEmitter == 0)
    {
        Non3DEmitter = S3DCreateSoundEmitterPri(0, 0, 0, sample_idx, 100, adjusted_volume, 0, 8, 2147483646);
    } else
    {
        S3DAddSampleToEmitterPri(Non3DEmitter, sample_idx, 100, adjusted_volume, 0, 3, 8, 2147483646);
    }
}

void play_non_3d_sample_no_overlap(SoundSmplTblID smpl_idx)
{
    if (SoundDisabled)
        return;
    if (GetCurrentSoundMasterVolume() <= 0)
        return;

    // Set sound volume setting
    SoundVolume adjusted_volume = volume_setting_scale(bf_sound_volume);

    if (Non3DEmitter != 0)
    {
        if (!sound_emitter_in_use(Non3DEmitter))
        {
            ERRORLOG("Non 3d Emitter has been deleted!");
            Non3DEmitter = 0;
        }
    }
    if (Non3DEmitter == 0)
    {
        Non3DEmitter = S3DCreateSoundEmitterPri(0, 0, 0, smpl_idx, 100, adjusted_volume, 0, 8, 0x7FFFFFFE);
    } else
    if (!S3DEmitterIsPlayingSample(Non3DEmitter, smpl_idx))
    {
        S3DAddSampleToEmitterPri(Non3DEmitter, smpl_idx, 100, adjusted_volume, 0, 3, 8, 0x7FFFFFFE);
    }
}

static TbBool is_atmos_sample(SoundSmplTblID smptbl_id)
{
    return ((smptbl_id >= bf_atmos_start) && (smptbl_id <= bf_atmos_end)) || (smptbl_id == bf_atmos_repeat);
}

/**
 * The loudness an atmospheric sound plays at: the atmos volume scaled by the
 * sound effects volume, like every other effect, and by the receiver's
 * sensitivity -- which the dungeon view lowers as the camera zooms out
 * (update_3d_sound_receiver()), quietening every 3D sound the same way,
 * down to 1/4. Atmos sounds have no position, so they don't get the 3D
 * sounds' distance or line-of-sight fall-off; without this they stayed at
 * full level while everything else faded. 0 while the sound master volume
 * is 0 (muted).
 */
SoundVolume atmos_sound_loudness(int64_t atmos_volume, int64_t sound_volume, SoundVolume master_volume, int64_t sensitivity)
{
    if (master_volume <= 0)
        return 0;
    sensitivity = clamp(sensitivity, 0, RECEIVER_FULL_SENSITIVITY);
    const double loudness = VOLUME_SETTING_MAX * volume_setting_gain(atmos_volume) * volume_setting_scale(sound_volume) / FULL_LOUDNESS;
    return (SoundVolume)lround(loudness * sensitivity / RECEIVER_FULL_SENSITIVITY);
}

static SoundVolume get_atmos_sound_loudness(void)
{
    return atmos_sound_loudness(atmos_sound_volume, bf_sound_volume, GetCurrentSoundMasterVolume(), Receiver.sensivity);
}

/**
 * Brings the atmospheric sounds still playing on the non-3D emitter to the
 * current get_atmos_sound_loudness(). The non-3D emitter is never
 * re-volumed by process_sound_emitters(), so an atmos sample kept the
 * loudness it started with: some run for many seconds (longer still at the
 * lowered pitches play_atmos_sound() uses), and turning the sound or atmos
 * volume down, or muting, left them playing at the old level.
 */
static void update_atmos_sound_volumes(struct SoundEmitter *emit)
{
    const SoundVolume loudness = get_atmos_sound_loudness();
    for (int64_t i = 0; i < MaxNoSounds; i++)
    {
        struct S3DSample* sample = &SampleList[i];
        if ((sample->is_playing == 0) || (sample->emit_ptr != emit) || !is_atmos_sample(sample->smptbl_id))
            continue;
        if (sample->base_volume == (uint64_t)loudness)
            continue;
        int64_t pan;
        int64_t volume;
        int64_t pitch;
        get_emitter_pan_volume_pitch(&Receiver, emit, &pan, &volume, &pitch);
        volume = (volume * loudness) / 256; // as start_emitter_playing() does
        SetSampleVolume(get_emitter_id(emit), sample->smptbl_id, volume);
        sample->volume = volume;
        sample->base_volume = loudness;
    }
}

void play_atmos_sound(SoundSmplTblID smpl_idx)
{
    if (SoundDisabled)
        return;
    if (GetCurrentSoundMasterVolume() <= 0)
        return;

    SoundVolume adjusted_volume = get_atmos_sound_loudness();

    int64_t ATMOS_SOUND_PITCH = (73 + (LbRandomSeries(10, soundhost_get_sound_random_seed(), __func__, __LINE__) * 6));
    // ATMOS0 has bigger range in pitch than other atmos sounds.
    if (smpl_idx == 1013)
    {
        ATMOS_SOUND_PITCH = (54 + (LbRandomSeries(16, soundhost_get_sound_random_seed(), __func__, __LINE__) * 4));
    }
    if (Non3DEmitter != 0)
    {
        if (!sound_emitter_in_use(Non3DEmitter))
        {
            ERRORLOG("Non 3d Emitter has been deleted!");
            Non3DEmitter = 0;
        }
    }
    if (Non3DEmitter == 0)
    {
        Non3DEmitter = S3DCreateSoundEmitterPri(0, 0, 0, smpl_idx, ATMOS_SOUND_PITCH, adjusted_volume, 0, 8, 0x7FFFFFFE);
    } else
    if (!S3DEmitterIsPlayingSample(Non3DEmitter, smpl_idx))
    {
        S3DAddSampleToEmitterPri(Non3DEmitter, smpl_idx, ATMOS_SOUND_PITCH, adjusted_volume, 0, 3, 8, 0x7FFFFFFE);
        SYNCDBG(9,"Playing atmos sound %" PRId64 " with pitch %" PRId64,(int64_t)smpl_idx,(int64_t)ATMOS_SOUND_PITCH);
    }
}

/**
 * Initializes and returns sound emitter structure.
 * Returns its index; if no free emitter is found, returns 0.
 */
SoundEmitterID allocate_free_sound_emitter(void)
{
    for (int64_t i = 1; i < NoSoundEmitters; i++)
    {
        if (!S3DEmitterIsAllocated(i))
        {
            struct SoundEmitter* emit = S3DGetSoundEmitter(i);
            emit->flags = Emi_IsAllocated;
            emit->index = i;
            return i;
        }
    }
    return 0;
}

/**
 * Clears sound emitter structure and marks it as unused.
 */
void delete_sound_emitter(SoundEmitterID idx)
{
    if (S3DEmitterIsAllocated(idx))
    {
        struct SoundEmitter* emit = S3DGetSoundEmitter(idx);
        memset(emit, 0, sizeof(struct SoundEmitter));
    }
}

/**
 * Drastic emitter clearing. Resets memory of all emitters, even unallocated ones.
 * Does not stop any playing samples - these should be cleared before this call.
 */
void delete_all_sound_emitters(void)
{
    for (int64_t i = 0; i < SOUND_EMITTERS_MAX; i++)
    {
        struct SoundEmitter* emit = &emitter[i];
        memset(emit, 0, sizeof(struct SoundEmitter));
    }
}

void init_sample_list(void)
{
    for (int64_t i = 0; i < SOUNDS_MAX_COUNT; i++)
    {
        struct S3DSample* sample = &SampleList[i];
        memset(sample, 0, sizeof(struct S3DSample));
    }
}

void increment_sample_times(void)
{
    for (int64_t i = 0; i < MaxNoSounds; i++)
    {
        struct S3DSample* sample = &SampleList[i];
        sample->time_turn++;
    }
}

void kick_out_sample(int64_t smpl_id)
{
    struct S3DSample* sample = &SampleList[smpl_id];
    stop_sample(get_sample_id(sample), sample->smptbl_id);
    sample->is_playing = 0;
}

TbBool process_sound_samples(void)
{
    for (int64_t i = 0; i < MaxNoSounds; i++)
    {
        struct S3DSample* sample = &SampleList[i];
        if (sample->is_playing != 0)
        {
            if (sample->mss_id <= 0)
            {
                ERRORLOG("Attempt to query invalid sample");
                continue;
            }
            if (!IsSamplePlaying(sample->mss_id))
            {
                sample->is_playing = 0;
            }
            if (sample->emit_ptr != NULL)
            {
              if ( (sample->volume == 0) ||
                 ( ((sample->emit_ptr->emitter_flags & 0x08) == 0) && (get_sound_distance(&sample->emit_ptr->pos, &Receiver.pos) > MaxSoundDistance) ) )
                kick_out_sample(i);
            }
        }
    }
    return true;
}

int64_t speech_sample_playing(void)
{
    if (SoundDisabled) {
         SYNCDBG(7,"Disabled");
         return false;
     }
     if (GetCurrentSoundMasterVolume() <= 0) {
         SYNCDBG(17,"Volume zero");
         return false;
     }
     SYNCDBG(17,"Starting");
     if (is_streamed_sample_playing())
     {
         return true;
     }
     int64_t sp_emiter = SpeechEmitter;
     if (sp_emiter != 0)
     {
         if (S3DEmitterIsAllocated(SpeechEmitter))
         {
           sp_emiter = SpeechEmitter;
         } else
         {
           ERRORLOG("Speech Emitter has been deleted");
           sp_emiter = 0;
         }
     }
     SpeechEmitter = sp_emiter;
     if (sp_emiter == 0)
       return false;
     return S3DEmitterIsPlayingAnySample(sp_emiter);
}

int64_t play_speech_sample(SoundSmplTblID smptbl_id)
{
    if (SoundDisabled)
      return false;
    if (bf_mentor_volume <= 0)
      return false;
    int64_t sp_emiter = SpeechEmitter;
    if (sp_emiter != 0)
    {
      if (S3DEmitterIsAllocated(SpeechEmitter))
      {
        sp_emiter = SpeechEmitter;
      } else
      {
        ERRORLOG("Speech Emitter has been deleted");
        sp_emiter = 0;
      }
    }
    SpeechEmitter = sp_emiter;
    int64_t adjusted_volume = volume_setting_scale(bf_mentor_volume);
    SoundSmplTblID unified_id = get_speech_offset() + smptbl_id;

    if (sp_emiter != 0)
    {
      if (S3DEmitterHasFinishedPlaying(sp_emiter))
        if (S3DAddSampleToEmitterPri(SpeechEmitter, unified_id, 100, adjusted_volume, 0, 3, 8, 2147483647))
          return true;
      return false;
    }
    sp_emiter = S3DCreateSoundEmitterPri(0, 0, 0, unified_id, 100, adjusted_volume, 0, 8, 2147483647);
    SpeechEmitter = sp_emiter;
    if (sp_emiter == 0)
    {
      ERRORLOG("Cannot create speech emitter.");
      return false;
    }
    return true;
}

int64_t start_emitter_playing(struct SoundEmitter *emit, SoundSmplTblID smptbl_id, int64_t smpitch, SoundVolume loudness, int64_t fild1D, int64_t ctype, unsigned char flags, int64_t priority)
{
    int64_t pan;
    int64_t volume;
    int64_t pitch;
    get_emitter_pan_volume_pitch(&Receiver, emit, &pan, &volume, &pitch);
    int64_t smpl_idx = find_slot(smptbl_id, emit, ctype, priority);
    volume = (volume * loudness) / 256;
    if (smpl_idx < 0)
        return 0;
    SoundMilesID mss_id = play_sample(get_emitter_id(emit), smptbl_id, volume, pan, smpitch, fild1D, ctype);
    if (mss_id <= 0) {
        return 0;
    }
    struct S3DSample* sample = &SampleList[smpl_idx];
    sample->priority = priority;
    sample->smptbl_id = smptbl_id;
    sample->emit_ptr = emit;
    sample->repeat_count = fild1D;
    sample->volume = volume;
    sample->pan = pan;
    sample->base_pitch = smpitch;
    sample->is_playing = 1;
    sample->mss_id = mss_id;
    sample->flags = flags;
    sample->time_turn = 0;
    sample->emit_idx = emit->index;
    sample->sfxid = get_sample_sfxid(smptbl_id);
    sample->base_volume = loudness;
    emit->flags |= Emi_IsPlaying;
    return 1;
}

void stop_atmos_sounds(void)
{
    if (bf_atmos_enabled)
    {
        struct SoundEmitter* emit = S3DGetSoundEmitter(Non3DEmitter);
        if (!S3DSoundEmitterInvalid(emit)) 
        {
            for (int64_t i = 0; i < MaxNoSounds; i++)
            {
                struct S3DSample* sample = &SampleList[i];
                if (is_atmos_sample(sample->smptbl_id))
                {
                    if ((sample->is_playing != 0) && (sample->emit_ptr == emit))
                    {
                        stop_sample(get_emitter_id(emit), sample->smptbl_id);
                        sample->is_playing = 0;
                    }
                }
            }
        }
    }
}

/******************************************************************************/
#ifdef __cplusplus
}
#endif
