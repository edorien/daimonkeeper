# Fuzzing and sanitizer builds

Security pass S08. Everything here is for development and CI; none of it is part of the game.

## Two sanitizer trees

`scripts/build-fuzz.sh` builds in trees of its own, with a dependency cache of its own
(`out/scratch/sanitize-deps`): the shared `deps/.cache-lin64` is rebuilt and relinked by every tree configured against
it, so a build with other compiler flags must not use it.

| command | tree | what |
|---|---|---|
| `scripts/build-fuzz.sh asan` | `out/asan` | the Catch2 suite with AddressSanitizer + UndefinedBehaviorSanitizer; run it with `cd out/asan && ASAN_OPTIONS=detect_leaks=0 ctest -j 24` |
| `scripts/build-fuzz.sh fuzz` | `out/fuzz` | `kfx_fuzz`: the harnesses below, same sanitizers, plus edge coverage from the `kfx_*` libraries |

gcc, no clang: UBSan is run without `signed-integer-overflow`, `shift` and `alignment` (the game's hashes rely on
wrapping, and its state structs are `#pragma pack(1)` blobs, so every member access is "misaligned" by design), and with
`-fno-sanitize-recover`, so a finding stops the run.

## kfx_fuzz

gcc has no libFuzzer, so `fuzz_driver.c` is a small coverage-guided mutation fuzzer: a forked child per input,
`-fsanitize-coverage=trace-pc` edges, an AFL-style bit-count map, a corpus directory that several workers share.

```
out/fuzz/fuzz/kfx_fuzz list
out/fuzz/fuzz/kfx_fuzz <target> fuzz <corpus-dir> [-max_total_time=S] [-runs=N] [-jobs=N] [-seed=N] [-timeout=S] [-crashes=DIR]
out/fuzz/fuzz/kfx_fuzz <target> repro <file>...     # in this process: the sanitizer report goes to the terminal
out/fuzz/fuzz/kfx_fuzz <target> run <file>...       # each in a child, says how it ended
```

A crash is saved as `<crashes>/<kind>-<hash>` with the sanitizer report beside it (`.txt`); `scripts/fuzz_triage.py`
groups them by what happened and where. `scripts/fuzz_seeds.py` makes the seed corpora (the repo's `config/` and the
content of `core_files/`, which is git-ignored: the original game's data and the community campaigns).

Environment: `KFX_FUZZ_DATA` (the folder linked in as the runtime directory's `fxdata/`, `creatrs/`...; default: the
repo's `config/`), `KFX_FUZZ_MAP_DIR` (a folder with a level's `map00001.*` files, for `map_file` and `lua_api`; default
`core_files/campgns/ancntkpr`), `KFX_FUZZ_VERBOSE`. The harnesses stage a runtime directory in `/dev/shm/kfx_fuzz_rt`
and per-input scratch folders in it; a crash can leave one behind (`rm -rf /dev/shm/kfx_fuzz_rt`).

## Targets

| target | input | runs |
|---|---|---|
| `config_file` | byte 0 picks a config file, the rest is a campaign's override of it | `load_config()` list-only and full, as a level load does |
| `creature_file` | byte 0 picks a creature, the rest its `creatrs/<name>.cfg` | `load_creaturemodel_config_file()` |
| `campaign_file` | a campaign `.cfg` | `load_campaign()` (common, strings, speech, map blocks) |
| `level_script` | a level's `map*.txt` | `preload_script()`, `load_script()`, then every command's process function as if all conditions held |
| `strings_file` | a language strings `.dat` | `load_campaign_strings_data_from_file()` |
| `rnc_file` | any file | `LbFileLengthRnc()`, `LbFileLoadAt()`, `LbFileLoadAtMax()` (RNC-packed or plain) |
| `map_file` | byte 0 picks one of a level's files, the rest replaces it | `load_map_file()` and `init_navigation()` |
| `lua_serialised` | the Lua game data of a save/resync | `lua_set_serialised_data()` |
| `lua_source` | Lua | the level script's sandbox, within its budget |
| `lua_api` | a list of calls: function, argument count, typed arguments | the Lua API's C functions on a loaded level |
| `api_json` | a message | the remote API (`api_process_message_for_test()`) |
| `packet` | a `struct Packet` | `packet_check()` and `packet_validate_for_user()` |
| `replay_file` | a `.pck` | the chunks, then 40 turns through the packet codec |
| `audio_buffer` | WAV/OGG/FLAC/MP3 | the custom-sound decoders |
| `sprite_zip` | a zip | a map's own sprite/icon/ensign pack |

Not covered here, because a byte stream does not reach them usefully: the state blobs of a save or a resync (140 MB;
the `state_corruption_fuzz` ftest corrupts those with structure, see `docs/security-pass`), and ffmpeg's Smacker
decoder (upstream fuzzes it).

## Adding a target

Write `int one(const uint8_t *data, size_t size)` that does what the game does with that input, and register it with
`FUZZ_TARGET()` (see `harness_content.cpp`). Do the expensive set-up in `init` (it runs once, before the first fork).
Keep a target honest: if it needs state the game would have (a loaded level, players), load it in `init`; a crash that
only exists in an empty state is not a finding.
