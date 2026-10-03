// kfx_config: config_sounds.c's load_sounds_config_file() (the
// keeper_sounds_file_data.load_func, unlike this library's other
// hardcoded-path loaders it DOES take a caller-supplied fname+flags)
// parses three independent sections: [system] (SpeechQueueLimit only),
// [sounds] (parse_sound_line's NAME = numeric_id [count] path -- the
// registered-name branch, no real audio decode), and [speech] (SMsg
// name -> override path string, stored verbatim with no file-existence
// check). Also covers get_sound_id/is_sound_registered/
// cache_common_sound_ids (thin wrappers over kfx_platform's
// SoundManager singleton registry -- reset with sound_manager_clear_registry()
// between tests, since it's real persistent process state, same
// reasoning as this library's *_desc[] NamedCommand tables), and
// sound_id_from_text/value_sound_id/speech_ref_parse's numeric and
// registered-name/named-message resolution branches.
//
// NOT attempted:
// - load_sounds_config()/load_campaign_sounds_config()/
//   load_mod_sounds_config()/load_level_sounds_config()/
//   sound_reset_to_fxdata_baseline(): same class of gap as
//   config_settings_test.cpp's load_settings() -- they construct a real
//   path via prepare_file_path()/prepare_file_fmtpath() and call
//   load_config()/load_sounds_config_file() against it, with no way to
//   redirect to a fixture without a real file-I/O side effect.
// - sound_id_from_text/value_sound_id/speech_ref_parse's file-path
//   branch (a name ending in .wav/.mp3 that isn't already registered):
//   would attempt a real (harmlessly failing, since the path doesn't
//   exist) WAV decode via kfx_platform's SoundManager -- not attempted
//   here to keep this file's scope to the registry-only paths every
//   other function above also uses.
// - value_speech_ref/assign_speech_ref: only reachable through a real
//   NamedField/parse_named_field_blocks table (config_terrain.c's
//   RoomConfigStats.msg_needed/msg_too_small/msg_no_route are the only
//   current users, and config_terrain_test.cpp doesn't exercise those
//   fields either) -- building a synthetic NamedFieldSet just to reach
//   them here isn't attempted; speech_ref_parse (the plain, non-NamedField
//   entry point with the same resolution logic) is tested directly instead.
#include <catch2/catch_test_macros.hpp>
#include "config_settings.h"

#include "kfx_config_test_paths.h" // KFX_CONFIG_TEST_FIXTURES_DIR
#include "config_sounds.h"
#include "config_effects.h" // effects_effectgenerator_named_fields[], reused below as a stand-in NamedField
#include "sound_manager.h"

#include <cstring>

namespace {
struct ResetSoundState {
    ResetSoundState() { reset(); }
    ~ResetSoundState() { reset(); }
    static void reset() {
        sound_manager_clear_registry();
        std::memset(g_speech_overrides, 0, sizeof(g_speech_overrides));
        snd_refusal = 0;
        snd_gold_pickup = 0;
        snd_gold_pickup_count = 0;
    }
};
}

TEST_CASE_METHOD(ResetSoundState, "load_sounds_config_file's [sounds] section registers NAME = numeric_id [count] mappings", "[kfx_config][config_sounds]") {
    REQUIRE(keeper_sounds_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/sounds_minimal.cfg", 0));

    CHECK(is_sound_registered("REFUSAL"));
    CHECK(get_sound_id("REFUSAL") == 100);
    CHECK(is_sound_registered("GOLD_PICKUP"));
    CHECK(get_sound_id("GOLD_PICKUP") == 200);
    CHECK(sound_manager_get_count("GOLD_PICKUP") == 3);
}

TEST_CASE_METHOD(ResetSoundState, "get_sound_id/is_sound_registered report a name as unregistered before any load", "[kfx_config][config_sounds]") {
    CHECK_FALSE(is_sound_registered("REFUSAL"));
    CHECK(get_sound_id("REFUSAL") == 0);
}

TEST_CASE_METHOD(ResetSoundState, "load_sounds_config_file's [speech] section stores an SMsg override path verbatim, with no file-existence check", "[kfx_config][config_sounds]") {
    REQUIRE(keeper_sounds_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/sounds_minimal.cfg", 0));

    CHECK(std::strcmp(g_speech_overrides[SMsg_LevelWon], "speech/custom_levelwon.wav") == 0);
}

TEST_CASE_METHOD(ResetSoundState, "load_sounds_config_file's [system] section sets kfx_runtime_settings.speech_queue_limit", "[kfx_config][config_sounds]") {
    ResetSoundState::reset();
    const int64_t saved_limit = kfx_runtime_settings.speech_queue_limit;
    kfx_runtime_settings.speech_queue_limit = -1;

    REQUIRE(keeper_sounds_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/sounds_minimal.cfg", 0));
    CHECK(kfx_runtime_settings.speech_queue_limit == 7);
    kfx_runtime_settings.speech_queue_limit = saved_limit;
}

TEST_CASE_METHOD(ResetSoundState, "cache_common_sound_ids caches only already-registered names, leaving others untouched", "[kfx_config][config_sounds]") {
    REQUIRE(keeper_sounds_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/sounds_minimal.cfg", 0));
    snd_refusal = -1;       // poison first, so a no-op cache call is distinguishable from a real one
    snd_room_claim = -1;    // "ROOM_CLAIM" isn't in the fixture -- should stay untouched

    CHECK(cache_common_sound_ids());
    CHECK(snd_refusal == 100);
    CHECK(snd_gold_pickup == 200);
    CHECK(snd_gold_pickup_count == 3);
    CHECK(snd_room_claim == -1);
}

TEST_CASE_METHOD(ResetSoundState, "sound_id_from_text has no numeric-passthrough branch, unlike value_sound_id -- a plain digit string is just looked up as a (typically unregistered) name", "[kfx_config][config_sounds]") {
    // A real, verified-by-test-run difference from value_sound_id (which
    // checks parameter_is_number() first): sound_id_from_text() always
    // tries sound_manager_get_id() first, so "42" resolves to 0 unless a
    // sound is literally registered under the name "42".
    CHECK(sound_id_from_text("42") == 0);

    CHECK(sound_manager_register("42", 42, 1));
    CHECK(sound_id_from_text("42") == 42);
}

TEST_CASE_METHOD(ResetSoundState, "sound_id_from_text resolves an already-registered name, and returns 0 for an unrecognized non-numeric, non-filepath name", "[kfx_config][config_sounds]") {
    REQUIRE(keeper_sounds_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/sounds_minimal.cfg", 0));

    CHECK(sound_id_from_text("REFUSAL") == 100);
    CHECK(sound_id_from_text("NOT_A_REAL_SOUND") == 0);
}

TEST_CASE_METHOD(ResetSoundState, "value_sound_id treats a numeric value like value_default, ignoring the registry", "[kfx_config][config_sounds]") {
    // Reuses a real NamedField entry (config_effects.c's EffectConfigStats
    // "Sound" field) purely as a stand-in NamedField/NamedFieldSet pair --
    // value_sound_id's numeric branch never dereferences named_fields_set
    // or idx, only named_field->min/max/type, so this is a safe, minimal
    // way to call it without constructing a synthetic table.
    const struct NamedField *sound_field = &effects_effectgenerator_named_fields_set.named_fields[13];
    REQUIRE(sound_field->parse_func == value_sound_id); // guards the hardcoded index above
    CHECK(value_sound_id(sound_field, "99", nullptr, 0, "test", 0) == 99);
}

TEST_CASE_METHOD(ResetSoundState, "value_sound_id resolves an already-registered name to its ID", "[kfx_config][config_sounds]") {
    REQUIRE(keeper_sounds_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/sounds_minimal.cfg", 0));

    const struct NamedField *sound_field = &effects_effectgenerator_named_fields_set.named_fields[13];
    REQUIRE(sound_field->parse_func == value_sound_id);
    CHECK(value_sound_id(sound_field, "REFUSAL", nullptr, 0, "test", 0) == 100);
}

TEST_CASE("speech_ref_parse resolves a numeric SMsg_* id, clamping out-of-range ids to the zero default", "[kfx_config][config_sounds]") {
    SpeechRef ref;
    speech_ref_parse(&ref, "5");
    CHECK(ref.id == 5);
    CHECK(ref.path[0] == '\0');

    speech_ref_parse(&ref, "99999");
    CHECK(ref.id == 0); // out of [0, SMsg_MAX) -- warned and left at the reset default
}

TEST_CASE("speech_ref_parse resolves a named speech message to its SMsg_* id", "[kfx_config][config_sounds]") {
    SpeechRef ref;
    speech_ref_parse(&ref, "LevelWon");
    CHECK(ref.id == SMsg_LevelWon);
    CHECK(ref.path[0] == '\0');
}

TEST_CASE("speech_ref_parse stores an unrecognized .wav/.mp3-looking value as a path, leaving id at 0", "[kfx_config][config_sounds]") {
    SpeechRef ref;
    speech_ref_parse(&ref, "speech/custom.wav");
    CHECK(ref.id == 0);
    CHECK(std::strcmp(ref.path, "speech/custom.wav") == 0);
}

TEST_CASE("speech_ref_parse leaves both id and path at their reset defaults for an empty or null value", "[kfx_config][config_sounds]") {
    SpeechRef ref = {1, "stale"};
    speech_ref_parse(&ref, "");
    CHECK(ref.id == 0);
    CHECK(ref.path[0] == '\0');

    ref = {1, "stale"};
    speech_ref_parse(&ref, nullptr);
    CHECK(ref.id == 0);
    CHECK(ref.path[0] == '\0');
}

TEST_CASE("load_sounds_config_file returns false for a missing file", "[kfx_config][config_sounds]") {
    CHECK_FALSE(keeper_sounds_file_data.load_func(KFX_CONFIG_TEST_FIXTURES_DIR "/does_not_exist.cfg", CnfLd_IgnoreErrors));
}

TEST_CASE("keeper_sounds_file_data has a post_load_func (cache_common_sound_ids) but no pre_load_func", "[kfx_config][config_sounds]") {
    CHECK(keeper_sounds_file_data.pre_load_func == nullptr);
    CHECK(keeper_sounds_file_data.post_load_func == cache_common_sound_ids);
    CHECK(std::strcmp(keeper_sounds_file_data.filename, "sounds.cfg") == 0);
}
