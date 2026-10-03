// Savegames and network resync write/copy these structs as raw bytes. Every integer in the game is an explicit
// int64_t/uint64_t (double for floats) and the state structs hold no pointers, so the byte layout is one and the
// same on every platform and compiler. These numbers pin it: a stray `int`/`long`/pointer, or a reordered field,
// changes a size here (and would make saves and multiplayer resync incompatible between builds).
#include <catch2/catch_test_macros.hpp>

#include "game_saves.h"
#include "game_merge.h"
#include "lvl_script.h"
#include "thing_data.h"
#include "creature_control.h"
#include "player_data.h"
#include "dungeon_data.h"
#include "kfx_sim_state.h"

#include <cstddef>
#include <cstdint>

TEST_CASE("Save-file chunk header is three 64-bit fields on every platform", "[kfx_game][lp64][layout]") {
    CHECK(sizeof(struct FileChunkHeader) == 24);
    CHECK(offsetof(struct FileChunkHeader, id) == 8);
    CHECK(offsetof(struct FileChunkHeader, ver) == 16);
}

TEST_CASE("Serialized game structs have platform-independent sizes", "[kfx_game][lp64][layout]") {
    CHECK(sizeof(struct IntralevelData) == 81747);
    CHECK(sizeof(struct LevelScript) == 439296);
    CHECK(sizeof(struct ScriptValue) == 43);
    CHECK(sizeof(struct PartyTrigger) == 37);
    CHECK(sizeof(struct TunnellerTrigger) == 36);
    CHECK(sizeof(struct Condition) == 39);
    CHECK(sizeof(struct Thing) == 8649); // pass 5 S04: last_turn_drawn removed
    CHECK(sizeof(struct CreatureControl) == 20160); // pass 4 P4-F11, P4-F12: party.tunnel_last_x/_y, tunnel_still_turns; pass 5 S04: thought bubble and footstep fields removed
    CHECK(sizeof(struct PlayerInfo) == 8554);
    CHECK(sizeof(struct Dungeon) == 163496); // S10: camera_deviate_quake/_jump moved out
}
