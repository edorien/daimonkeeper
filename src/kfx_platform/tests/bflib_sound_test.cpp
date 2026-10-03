// kfx_platform: bflib_sound.c -- the 3D positional-audio bookkeeping
// layer (emitter/sample allocation, distance/volume/pan/pitch math,
// receiver state), NOT the real-OpenAL device wrapper (that's
// bflib_sndlib.cpp -- stop_sample/play_sample/SetSample*/
// GetCurrentSoundMasterVolume, still declined, the genuine
// "significant harness change" boundary this library has consistently
// held). Reading every function body first (not assumed) found the
// overwhelming majority of this file never touches those real-audio
// functions at all -- it's pure array bookkeeping over its own
// module-level emitter[]/SampleList[] plus pure distance/volume/pan/
// pitch formulas, with the real-audio calls concentrated in a handful
// of specific branches (see the "declined" notes below on each).
//
// MaxNoSounds/SampleList were file-scope `static` with no
// getter/setter beyond the coarse S3DSetNumberOfSounds()/
// init_sample_list(), which made "a sample is already playing on this
// emitter" scenarios unreachable without calling the real
// start_emitter_playing()/play_sample(). Un-static'd both (declared
// extern in bflib_sound.h) so tests can construct that scenario via
// direct field writes instead -- the same "expose the module-level
// global" pattern kfx_config's `campaign` already used. A dozen
// functions with real external linkage had no header declaration at
// all either (only ever called from within this file); all added.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "bflib_sound.h"
#include "bflib_sndlib.h" // GetCurrentSoundMasterVolume

#include <cmath>
#include <cstring>

namespace {
struct SoundFixture {
    SoundFixture() {
        S3DInit(); // pure: clears emitter[]/SampleList[], resets Receiver, MaxNoSounds=SOUNDS_MAX_COUNT
        Non3DEmitter = 0;
        SpeechEmitter = 0;
        MaxSoundDistance = 128;
        SoundDisabled = false;
    }
};

int64_t fake_sight_calls = 0;
int64_t fake_sight_result = 1;
int64_t fake_line_of_sight(int64_t, int64_t, int64_t, int64_t, int64_t, int64_t) {
    fake_sight_calls++;
    return fake_sight_result;
}
}

TEST_CASE_METHOD(SoundFixture, "S3DInit resets emitters, samples, and receiver to a pure known baseline", "[kfx_platform][bflib_sound]") {
    CHECK(MaxNoSounds == SOUNDS_MAX_COUNT);
    CHECK(emitter[1].flags == 0);
    CHECK(SampleList[0].is_playing == 0);
    CHECK(Receiver.pos.val_x == 0);
    CHECK(Receiver.sensivity == 64);
}

TEST_CASE_METHOD(SoundFixture, "allocate_free_sound_emitter finds successive free slots starting at index 1", "[kfx_platform][bflib_sound]") {
    SoundEmitterID first = allocate_free_sound_emitter();
    CHECK(first == 1);
    CHECK((emitter[1].flags & Emi_IsAllocated) != 0);
    CHECK(emitter[1].index == 1);

    SoundEmitterID second = allocate_free_sound_emitter();
    CHECK(second == 2);
}

TEST_CASE_METHOD(SoundFixture, "allocate_free_sound_emitter returns 0 once every slot is allocated", "[kfx_platform][bflib_sound]") {
    for (int64_t i = 1; i < SOUND_EMITTERS_MAX; i++) {
        emitter[i].flags = Emi_IsAllocated;
    }
    CHECK(allocate_free_sound_emitter() == 0);
}

TEST_CASE_METHOD(SoundFixture, "delete_sound_emitter clears an allocated emitter back to zero, is a no-op on an unallocated one", "[kfx_platform][bflib_sound]") {
    SoundEmitterID idx = allocate_free_sound_emitter();
    emitter[idx].pos.val_x = 42;

    delete_sound_emitter(idx);
    CHECK(emitter[idx].flags == 0);
    CHECK(emitter[idx].pos.val_x == 0);

    emitter[5].reserved[0] = 0xFF; // unallocated slot, poisoned directly
    delete_sound_emitter(5);
    CHECK(emitter[5].reserved[0] == 0xFF); // untouched -- not allocated, so the guard skips it
}

TEST_CASE_METHOD(SoundFixture, "delete_all_sound_emitters clears every slot including unallocated ones", "[kfx_platform][bflib_sound]") {
    emitter[7].reserved[2] = 0xAA; // never allocated, but delete_all doesn't check the flag
    delete_all_sound_emitters();
    CHECK(emitter[7].reserved[2] == 0);
}

TEST_CASE_METHOD(SoundFixture, "init_sample_list clears every sample slot", "[kfx_platform][bflib_sound]") {
    SampleList[3].priority = 99;
    init_sample_list();
    CHECK(SampleList[3].priority == 0);
}

TEST_CASE_METHOD(SoundFixture, "S3DGetSoundEmitter bounds-checks the index, returning the sentinel slot 0 out of range", "[kfx_platform][bflib_sound]") {
    CHECK(S3DGetSoundEmitter(-1) == &emitter[0]);
    CHECK(S3DGetSoundEmitter(SOUND_EMITTERS_MAX) == &emitter[0]);
    CHECK(S3DGetSoundEmitter(5) == &emitter[5]);
}

TEST_CASE_METHOD(SoundFixture, "S3DSoundEmitterInvalid is true for NULL and the sentinel slot, false for a real slot", "[kfx_platform][bflib_sound]") {
    CHECK(S3DSoundEmitterInvalid(nullptr));
    CHECK(S3DSoundEmitterInvalid(&emitter[0])); // the INVALID_SOUND_EMITTER sentinel
    CHECK_FALSE(S3DSoundEmitterInvalid(&emitter[5]));
}

TEST_CASE_METHOD(SoundFixture, "S3DEmitterIsAllocated/S3DEmitterHasFinishedPlaying read the emitter flags bitmask", "[kfx_platform][bflib_sound]") {
    SoundEmitterID idx = allocate_free_sound_emitter();
    CHECK(S3DEmitterIsAllocated(idx));
    CHECK(S3DEmitterHasFinishedPlaying(idx)); // Emi_IsPlaying not set yet

    emitter[idx].flags |= Emi_IsPlaying;
    CHECK_FALSE(S3DEmitterHasFinishedPlaying(idx));
}

TEST_CASE_METHOD(SoundFixture, "S3DMoveSoundEmitterTo updates position and sets Emi_IsMoving only for an allocated emitter", "[kfx_platform][bflib_sound]") {
    CHECK_FALSE(S3DMoveSoundEmitterTo(9, 1, 2, 3)); // slot 9 never allocated
    CHECK(emitter[9].pos.val_x == 0);

    SoundEmitterID idx = allocate_free_sound_emitter();
    CHECK(S3DMoveSoundEmitterTo(idx, 10, 20, 30));
    CHECK(emitter[idx].pos.val_x == 10);
    CHECK(emitter[idx].pos.val_y == 20);
    CHECK(emitter[idx].pos.val_z == 30);
    CHECK((emitter[idx].flags & Emi_IsMoving) != 0);
}

TEST_CASE_METHOD(SoundFixture, "S3DSetNumberOfSounds clamps to [1, SOUNDS_MAX_COUNT]", "[kfx_platform][bflib_sound]") {
    S3DSetNumberOfSounds(0);
    CHECK(MaxNoSounds == 1);

    S3DSetNumberOfSounds(9999);
    CHECK(MaxNoSounds == SOUNDS_MAX_COUNT);

    S3DSetNumberOfSounds(5);
    CHECK(MaxNoSounds == 5);
}

TEST_CASE_METHOD(SoundFixture, "S3DSetMaximumSoundDistance clamps to [1, 65536]", "[kfx_platform][bflib_sound]") {
    S3DSetMaximumSoundDistance(0);
    CHECK(MaxSoundDistance == 1);

    S3DSetMaximumSoundDistance(999999);
    CHECK(MaxSoundDistance == 65536);

    S3DSetMaximumSoundDistance(500);
    CHECK(MaxSoundDistance == 500);
}

TEST_CASE_METHOD(SoundFixture, "S3DSetSoundReceiverPosition/_Orientation/_Sensitivity write into Receiver", "[kfx_platform][bflib_sound]") {
    S3DSetSoundReceiverPosition(11, 22, 33);
    CHECK(Receiver.pos.val_x == 11);
    CHECK(Receiver.pos.val_y == 22);
    CHECK(Receiver.pos.val_z == 33);

    S3DSetSoundReceiverOrientation(100, 200, 300);
    CHECK(Receiver.rotation_angle_x == 100);
    CHECK(Receiver.rotation_angle_y == 200);
    CHECK(Receiver.rotation_angle_z == 300);

    S3DSetSoundReceiverSensitivity(50);
    CHECK(Receiver.sensivity == 50);
}

TEST_CASE_METHOD(SoundFixture, "get_emitter_id/get_sample_id offset the index by 4000", "[kfx_platform][bflib_sound]") {
    emitter[3].index = 3;
    CHECK(get_emitter_id(&emitter[3]) == 4003);

    struct S3DSample sample;
    std::memset(&sample, 0, sizeof(sample));
    sample.emit_idx = 7;
    CHECK(get_sample_id(&sample) == 4007);
}

TEST_CASE_METHOD(SoundFixture, "sound_emitter_in_use delegates to S3DEmitterIsAllocated", "[kfx_platform][bflib_sound]") {
    CHECK_FALSE(sound_emitter_in_use(1));
    SoundEmitterID idx = allocate_free_sound_emitter();
    CHECK(sound_emitter_in_use(idx));
}

TEST_CASE_METHOD(SoundFixture, "get_sound_distance computes 3D Euclidean distance, clamped per axis at 26754", "[kfx_platform][bflib_sound]") {
    struct SoundCoord3d a{0, 0, 0};
    struct SoundCoord3d b{3, 4, 0};
    CHECK(get_sound_distance(&a, &b) == 5); // classic 3-4-5 triangle

    struct SoundCoord3d far_point{30000, 0, 0}; // exceeds the 26754 per-axis clamp
    CHECK(get_sound_distance(&a, &far_point) == 26754);
}

TEST_CASE_METHOD(SoundFixture, "get_sound_squareedge_distance sums per-axis deltas, clamped per axis at INT32_MAX/3", "[kfx_platform][bflib_sound]") {
    struct SoundCoord3d a{0, 0, 0};
    struct SoundCoord3d b{3, 4, 5};
    CHECK(get_sound_squareedge_distance(&a, &b) == 12);

    struct SoundCoord3d huge_point{800000000, 0, 0}; // exceeds INT32_MAX/3 (~715,827,882)
    CHECK(get_sound_squareedge_distance(&a, &huge_point) == INT32_MAX / 3);
}

TEST_CASE_METHOD(SoundFixture, "get_emitter_distance clamps to [0, MaxSoundDistance-1]", "[kfx_platform][bflib_sound]") {
    MaxSoundDistance = 100;
    struct SoundReceiver recv;
    std::memset(&recv, 0, sizeof(recv));
    struct SoundEmitter emit;
    std::memset(&emit, 0, sizeof(emit));

    emit.pos.val_x = 30; // within range: distance 30
    CHECK(get_emitter_distance(&recv, &emit) == 30);

    emit.pos.val_x = 1000; // far beyond MaxSoundDistance
    CHECK(get_emitter_distance(&recv, &emit) == MaxSoundDistance - 1);
}

TEST_CASE_METHOD(SoundFixture, "get_emitter_sight delegates to the registered LineOfSightFunction", "[kfx_platform][bflib_sound]") {
    fake_sight_calls = 0;
    fake_sight_result = 0;
    S3DSetLineOfSightFunction(fake_line_of_sight);

    struct SoundReceiver recv;
    std::memset(&recv, 0, sizeof(recv));
    struct SoundEmitter emit;
    std::memset(&emit, 0, sizeof(emit));
    emit.pos.val_x = 7;

    CHECK(get_emitter_sight(&recv, &emit) == 0);
    CHECK(fake_sight_calls == 1);

    fake_sight_result = 1;
    CHECK(get_emitter_sight(&recv, &emit) == 1);
    CHECK(fake_sight_calls == 2);
}

TEST_CASE_METHOD(SoundFixture, "get_emitter_volume applies the deadzone-and-sensitivity falloff formula, clamping i at 0", "[kfx_platform][bflib_sound]") {
    S3DSetDeadzoneRadius(0);
    MaxSoundDistance = 128;
    struct SoundReceiver recv;
    std::memset(&recv, 0, sizeof(recv));
    recv.sensivity = 64;
    struct SoundEmitter emit;
    std::memset(&emit, 0, sizeof(emit));

    // i=64, n=128: vol = (127 - 127*64/128) * 64 = (127-63)*64 = 4096; >>6 = 64
    CHECK(get_emitter_volume(&recv, &emit, 64) == 64);

    // dist < deadzone_radius clamps i to 0, same as dist==deadzone_radius
    S3DSetDeadzoneRadius(50);
    // i=max(10-50,0)=0, n=128-50=78: vol=(127-0)*64=8128; >>6=127
    CHECK(get_emitter_volume(&recv, &emit, 10) == 127);
}

TEST_CASE_METHOD(SoundFixture, "get_emitter_pan short-circuits to 64 for the receiver-flags-set and within-deadzone cases", "[kfx_platform][bflib_sound]") {
    // A real quirk found while testing, not fixed: SoundReceiver::flags is
    // checked against Emi_IsAllocated -- an emitter-flag bit reused here,
    // not a receiver-specific flag of its own.
    struct SoundReceiver recv;
    std::memset(&recv, 0, sizeof(recv));
    recv.flags = Emi_IsAllocated;
    struct SoundEmitter emit;
    std::memset(&emit, 0, sizeof(emit));
    CHECK(get_emitter_pan(&recv, &emit) == 64);

    recv.flags = 0;
    S3DSetDeadzoneRadius(1000); // larger than any position delta below
    emit.pos.val_x = 5;
    CHECK(get_emitter_pan(&recv, &emit) == 64); // within deadzone radius
    // The general (outside-deadzone) formula depends on LbSinL/
    // LbArcTanAngle's fixed-point lookup tables -- not asserted here for
    // the same reason bflib_planar_test.cpp declines to: a hard-coded
    // expected value would just be a second, easier-to-get-wrong copy of
    // the table data.
}

TEST_CASE_METHOD(SoundFixture, "get_emitter_pitch_from_doppler ramps toward target_pitch by half the delta, target_pitch=100 once caught up", "[kfx_platform][bflib_sound]") {
    struct SoundReceiver recv;
    std::memset(&recv, 0, sizeof(recv));
    struct SoundEmitter emit;
    std::memset(&emit, 0, sizeof(emit));
    emit.curr_pitch = 100;
    emit.pitch_doppler = 0;
    recv.pos.val_x = 100; // squaredge distance from emit(0,0,0) to recv = 100

    // delta=100, target_pitch = 100 - 20*100/256 = 100-7 = 93.
    // A real quirk found while testing, not fixed: the ramp always ADDS
    // abs(target-next)>>1 regardless of sign, so it moves further FROM a
    // lower target rather than toward it (looks like a missing sign, not
    // exercised behavior anyone relies on being "correct" ramping).
    int64_t pitch = get_emitter_pitch_from_doppler(&recv, &emit);
    CHECK(emit.target_pitch == 93);
    CHECK(pitch == 103); // 100 + (|93-100|>>1) = 100+3
    CHECK(emit.curr_pitch == 103);
    CHECK(emit.pitch_doppler == 100);

    // pitch_doppler already ahead of the new distance -> delta clamps to
    // 0 -> target_pitch resets to 100 (the "caught up" default).
    emit.pitch_doppler = 200;
    int64_t pitch2 = get_emitter_pitch_from_doppler(&recv, &emit);
    CHECK(emit.target_pitch == 100);
    (void)pitch2;
}

TEST_CASE_METHOD(SoundFixture, "get_emitter_pan_volume_pitch returns full volume/centre pan/normal pitch for the 'always audible' emitter_flags bit", "[kfx_platform][bflib_sound]") {
    struct SoundReceiver recv;
    std::memset(&recv, 0, sizeof(recv));
    struct SoundEmitter emit;
    std::memset(&emit, 0, sizeof(emit));
    emit.emitter_flags = 0x08;

    int64_t pan, volume, pitch;
    CHECK(get_emitter_pan_volume_pitch(&recv, &emit, &pan, &volume, &pitch) == 1);
    CHECK(volume == 127);
    CHECK(pan == 64);
    CHECK(pitch == 100);
}

TEST_CASE_METHOD(SoundFixture, "get_emitter_pan_volume_pitch halves volume when not on sight, and passes through pan/pitch defaults", "[kfx_platform][bflib_sound]") {
    S3DSetDeadzoneRadius(0);
    MaxSoundDistance = 128;
    fake_sight_result = 0; // "not on sight"
    S3DSetLineOfSightFunction(fake_line_of_sight);

    struct SoundReceiver recv;
    std::memset(&recv, 0, sizeof(recv));
    recv.sensivity = 64;
    struct SoundEmitter emit;
    std::memset(&emit, 0, sizeof(emit));
    // emitter_flags doesn't have 0x04 (force-on-sight) or 0x08 (always audible)

    int64_t pan, volume, pitch;
    CHECK(get_emitter_pan_volume_pitch(&recv, &emit, &pan, &volume, &pitch) == 1);
    CHECK(volume == 63); // get_emitter_volume(dist=0) is 127, halved for "not on sight"
    CHECK(pan == 64);    // dist(0) - deadzone(0) < 128, so the cheap default applies
    CHECK(pitch == 100); // Emi_IsMoving not set
}

TEST_CASE_METHOD(SoundFixture, "set_emitter_pan_volume_pitch is a safe no-op when no sample is bound to the emitter", "[kfx_platform][bflib_sound]") {
    struct SoundEmitter emit;
    std::memset(&emit, 0, sizeof(emit));
    // SampleList is all is_playing=0 from S3DInit() -- the loop body
    // (which would call the real SetSampleVolume/Pan/Pitch) never runs.
    CHECK(set_emitter_pan_volume_pitch(&emit, 64, 100, 100) == 1);
}

TEST_CASE_METHOD(SoundFixture, "process_sound_emitters toggles Emi_IsPlaying off for an allocated+playing emitter with no actual playing sample bound", "[kfx_platform][bflib_sound]") {
    emitter[3].flags = Emi_IsAllocated | Emi_IsPlaying;
    // SampleList empty -- emitter_is_playing(&emitter[3]) is false, so
    // this takes the pure "flag was stale, clear it" branch.
    CHECK(process_sound_emitters());
    CHECK((emitter[3].flags & Emi_IsPlaying) == 0);
    CHECK((emitter[3].flags & Emi_IsAllocated) != 0);
}

TEST_CASE_METHOD(SoundFixture, "process_sound_emitters skips Non3DEmitter/SpeechEmitter even when a real playing sample is bound, never touching real-audio volume/pan/pitch", "[kfx_platform][bflib_sound]") {
    Non3DEmitter = 3;
    emitter[3].flags = Emi_IsAllocated | Emi_IsPlaying;
    SampleList[0].is_playing = 1;
    SampleList[0].emit_ptr = &emitter[3]; // makes emitter_is_playing(&emitter[3]) true
    CHECK(process_sound_emitters());
    // The `continue` for i==Non3DEmitter fires before set_emitter_pan_volume_pitch
    // (and therefore before any real SetSample*/audio call) -- flags untouched.
    CHECK((emitter[3].flags & Emi_IsPlaying) != 0);
}

TEST_CASE_METHOD(SoundFixture, "process_sound_samples sweeps with no playing samples without touching real audio", "[kfx_platform][bflib_sound]") {
    CHECK(process_sound_samples());
}

TEST_CASE_METHOD(SoundFixture, "find_slot returns the first free slot directly when one exists", "[kfx_platform][bflib_sound]") {
    CHECK(find_slot(100, &emitter[1], 0, 999) == 0);
}

TEST_CASE_METHOD(SoundFixture, "find_slot reuses an already-playing sample on the same emitter for ctype 2/3", "[kfx_platform][bflib_sound]") {
    emitter[5].index = 5;
    SampleList[3].is_playing = 1;
    SampleList[3].emit_ptr = &emitter[5];
    SampleList[3].smptbl_id = 42;

    CHECK(find_slot(42, &emitter[5], 2, 999) == 3);
}

TEST_CASE_METHOD(SoundFixture, "find_slot returns -1 when every slot is playing at or above spcmax priority, never evicting", "[kfx_platform][bflib_sound]") {
    S3DSetNumberOfSounds(SOUNDS_MAX_COUNT);
    for (int64_t i = 0; i < SOUNDS_MAX_COUNT; i++) {
        SampleList[i].is_playing = 1;
        SampleList[i].priority = 1000;
    }
    // The eviction branch (kick_out_sample(), spcval < spcmax) would call
    // the real stop_sample() -- deliberately not exercised here; spcmax
    // is kept at or below every slot's priority so find_slot always takes
    // the "can't evict" early return instead.
    CHECK(find_slot(1, &emitter[1], 0, 500) == -1);
}

TEST_CASE_METHOD(SoundFixture, "S3DEmitterIsPlayingSample matches on emitter and sample id", "[kfx_platform][bflib_sound]") {
    SoundEmitterID idx = allocate_free_sound_emitter();
    SampleList[0].is_playing = 1;
    SampleList[0].emit_ptr = &emitter[idx];
    SampleList[0].smptbl_id = 7;

    CHECK(S3DEmitterIsPlayingSample(idx, 7));
    CHECK_FALSE(S3DEmitterIsPlayingSample(idx, 8)); // wrong sample id
}

TEST_CASE_METHOD(SoundFixture, "emitter_is_playing scans SampleList for any sample bound to the given emitter", "[kfx_platform][bflib_sound]") {
    SoundEmitterID idx = allocate_free_sound_emitter();
    CHECK_FALSE(emitter_is_playing(&emitter[idx]));

    SampleList[2].is_playing = 1;
    SampleList[2].emit_ptr = &emitter[idx];
    CHECK(emitter_is_playing(&emitter[idx]));
}

TEST_CASE_METHOD(SoundFixture, "remove_active_samples_from_emitter always clears emit_ptr, but only stops+clears is_playing when repeat_count is -1", "[kfx_platform][bflib_sound]") {
    SoundEmitterID idx = allocate_free_sound_emitter();
    SampleList[0].is_playing = 1;
    SampleList[0].emit_ptr = &emitter[idx];
    SampleList[0].repeat_count = 0; // NOT -1 -- avoids the real stop_sample() call

    CHECK(remove_active_samples_from_emitter(&emitter[idx]));
    CHECK(SampleList[0].emit_ptr == nullptr); // unconditional
    CHECK(SampleList[0].is_playing == 1);      // untouched -- repeat_count != -1
}

TEST_CASE_METHOD(SoundFixture, "S3DDeleteSampleFromEmitter returns false for an invalid emitter without scanning", "[kfx_platform][bflib_sound]") {
    // The "found a match" success branch always calls the real
    // stop_sample() -- deliberately not exercised here, only the
    // early-return guard.
    CHECK_FALSE(S3DDeleteSampleFromEmitter(-1, 7));
}

TEST_CASE_METHOD(SoundFixture, "SoundDisabled short-circuits every play_*/speech_sample_playing entry point before any real-audio call", "[kfx_platform][bflib_sound]") {
    SoundDisabled = true;
    play_non_3d_sample(1);
    play_non_3d_sample_no_overlap(1);
    play_atmos_sound(1);
    CHECK(Non3DEmitter == 0); // none of the three touched it -- all returned immediately

    CHECK_FALSE(speech_sample_playing());
    CHECK_FALSE(play_speech_sample(1));
    CHECK(SpeechEmitter == 0);
}

TEST_CASE_METHOD(SoundFixture, "stop_atmos_sounds is a safe no-op when Non3DEmitter is still the unset sentinel", "[kfx_platform][bflib_sound]") {
    // bf_atmos_enabled defaults to true (no setter exercised here), but
    // Non3DEmitter==0 resolves to the INVALID_SOUND_EMITTER sentinel via
    // S3DGetSoundEmitter, so S3DSoundEmitterInvalid short-circuits the
    // whole loop before it can reach the real stop_sample().
    stop_atmos_sounds();
    CHECK(true); // must not crash
}

// The non-3D emitter is skipped by process_sound_emitters()'s re-volume
// pass, so an atmospheric sound (often many seconds long) used to keep the
// loudness it started at: turning the sound or atmos volume down, or muting,
// didn't reach it. process_sound_emitters() now re-volumes just the atmos
// samples on it, to atmos_sound_loudness(). SetSampleVolume() only walks the
// (here empty) OpenAL source list, so no real audio is touched. The sound
// master volume can't be raised without a real OpenAL context, so the
// process-level case is the muted one (master volume 0, as in this binary).
TEST_CASE("volume_setting_gain is a squared curve: 10% of the slider is about -40 dB, 50% about -12 dB", "[kfx_platform][bflib_sound]") {
    CHECK(volume_setting_gain(0) == 0.0);
    CHECK(volume_setting_gain(VOLUME_SETTING_MAX) == 1.0);
    CHECK(20.0 * std::log10(volume_setting_gain(26)) == Catch::Approx(-39.7).margin(0.1));   // 10%
    CHECK(20.0 * std::log10(volume_setting_gain(128)) == Catch::Approx(-12.0).margin(0.1));  // 50%
    CHECK(volume_setting_gain(-3) == 0.0); // clamped
    CHECK(volume_setting_gain(300) == 1.0);
    CHECK(volume_setting_gain(256) == 1.0); // mentor_volume's own clamp allows FULL_LOUDNESS
}

TEST_CASE("volume_setting_curve/_scale put the curve back on the setting and effect-loudness scales", "[kfx_platform][bflib_sound]") {
    CHECK(volume_setting_curve(0) == 0);
    CHECK(volume_setting_curve(VOLUME_SETTING_MAX) == VOLUME_SETTING_MAX);
    CHECK(volume_setting_curve(128) == 64);
    CHECK(volume_setting_scale(0) == 0);
    CHECK(volume_setting_scale(VOLUME_SETTING_MAX) == 2 * FULL_LOUDNESS); // full slider: twice sample loudness, as before
    CHECK(volume_setting_scale(127) == 127);                              // the default: about half what it was
}

TEST_CASE("atmos_sound_loudness puts both the atmos and the sound effects volume on the curve, and is 0 when muted", "[kfx_platform][bflib_sound]") {
    const int64_t full = RECEIVER_FULL_SENSITIVITY;
    CHECK(atmos_sound_loudness(255, 255, 127, full) == 510);
    CHECK(atmos_sound_loudness(255, 127, 127, full) == 127);
    CHECK(atmos_sound_loudness(128, 255, 127, full) == 129); // 255 * (128/255)^2 * 2 = 128.5
    CHECK(atmos_sound_loudness(128, 0, 127, full) == 0);   // sound effects slider at 0
    CHECK(atmos_sound_loudness(0, 255, 127, full) == 0);
    CHECK(atmos_sound_loudness(255, 255, 0, full) == 0);   // mute_audio(true)
}

TEST_CASE("atmos_sound_loudness follows the receiver sensitivity, so zooming out quietens it like the 3D sounds", "[kfx_platform][bflib_sound]") {
    CHECK(atmos_sound_loudness(255, 255, 127, RECEIVER_FULL_SENSITIVITY / 2) == 255);
    CHECK(atmos_sound_loudness(255, 255, 127, RECEIVER_FULL_SENSITIVITY / 4) == 128); // update_3d_sound_receiver()'s furthest zoom: 1/4
    CHECK(atmos_sound_loudness(255, 255, 127, 0) == 0);
    CHECK(atmos_sound_loudness(255, 255, 127, 255) == 510); // clamped: never louder than full sensitivity
}

TEST_CASE_METHOD(SoundFixture, "process_sound_emitters silences a playing atmos sound while muted, leaving other non-3D samples alone", "[kfx_platform][bflib_sound]") {
    REQUIRE(GetCurrentSoundMasterVolume() == 0); // never set in this binary: muted
    bf_sound_set_atmos_config(1014, 1034, 1013, true);
    Non3DEmitter = 3;
    emitter[3].flags = Emi_IsAllocated | Emi_IsPlaying;
    emitter[3].emitter_flags = 0x08; // the non-3D emitter: full volume, no distance
    // An atmos sample started at ATMOS_VOLUME=MEDIUM, and a UI sample.
    SampleList[0].is_playing = 1;
    SampleList[0].emit_ptr = &emitter[3];
    SampleList[0].smptbl_id = 1013;
    SampleList[0].base_volume = 128;
    SampleList[0].volume = 63;
    SampleList[1].is_playing = 1;
    SampleList[1].emit_ptr = &emitter[3];
    SampleList[1].smptbl_id = 61;
    SampleList[1].base_volume = 256;
    SampleList[1].volume = 127;

    CHECK(process_sound_emitters());
    CHECK(SampleList[0].base_volume == 0);
    CHECK(SampleList[0].volume == 0);
    CHECK(SampleList[1].base_volume == 256);
    CHECK(SampleList[1].volume == 127);
}
