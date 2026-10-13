// Security pass S06 (docs/security-pass/README.md): the level script's tables in a game state that came from a file
// or a peer (game_state_validate.h). Found by the corruption fuzzer (ftest state_corruption_fuzz): a win-condition
// count of INT64_MIN made the game loop over, and index, a 12-entry array.
#include <catch2/catch_test_macros.hpp>

#include "game_state_validate.h"
#include "kfx_game_state.h"

#include <cstring>
#include <string>

namespace {

struct Fresh {
    struct KfxGameState state;
    Fresh() { std::memset(&state, 0, sizeof(state)); }
    std::string why() const
    {
        char buf[256] = "";
        return game_state_validate(&state, buf, sizeof(buf)) ? std::string() : std::string(buf);
    }
    bool refused_for(const char *field) const
    {
        const std::string w = why();
        INFO(w);
        return !w.empty() && w.find(field) != std::string::npos;
    }
};

} // namespace

TEST_CASE_METHOD(Fresh, "an empty game state is fine", "[kfx_game][game_state_validate]") {
    CHECK(why().empty());
    CHECK(game_state_validate(&state, nullptr, 0));
}

TEST_CASE_METHOD(Fresh, "every count stays within its table", "[kfx_game][game_state_validate]") {
    state.script.win_conditions_num = 13;
    CHECK(refused_for("win_conditions_num"));
    state.script.win_conditions_num = (uint64_t)INT64_MIN; // the fuzzer's find
    CHECK(refused_for("win_conditions_num"));
    state.script.win_conditions_num = 12;
    CHECK(why().empty());
    state.script.lose_conditions_num = 13;
    CHECK(refused_for("lose_conditions_num"));
    state.script.lose_conditions_num = 0;
    state.script.values_num = 2049;
    CHECK(refused_for("values_num"));
    state.script.values_num = 2048;
    CHECK(why().empty());
    state.script.conditions_num = 513;
    CHECK(refused_for("conditions_num"));
    state.script.conditions_num = 0;
    state.script.tunneller_triggers_num = 257;
    CHECK(refused_for("tunneller_triggers_num"));
    state.script.tunneller_triggers_num = 0;
    state.script.party_triggers_num = 257;
    CHECK(refused_for("party_triggers_num"));
    state.script.party_triggers_num = 0;
    state.script.creature_partys_num = 257;
    CHECK(refused_for("creature_partys_num"));
    state.script.creature_partys_num = 0;
    state.active_script_var_count = 8;
    CHECK(refused_for("active_script_var_count"));
}

TEST_CASE_METHOD(Fresh, "entries name conditions, parties and strings that exist", "[kfx_game][game_state_validate]") {
    state.script.conditions_num = 3;
    state.script.win_conditions_num = 1;
    state.script.win_conditions[0] = 2;
    CHECK(why().empty());
    state.script.win_conditions[0] = 512;
    CHECK(refused_for("win_conditions"));
    state.script.win_conditions[0] = -1;
    CHECK(refused_for("win_conditions"));
    state.script.win_conditions[0] = 0;
    state.script.values_num = 1;
    state.script.values[0].condit_idx = 9999;
    CHECK(refused_for("values.condit_idx"));
    state.script.values[0].condit_idx = 0;
    state.script.creature_partys_num = 1;
    state.script.creature_partys[0].members_num = 31;
    CHECK(refused_for("members_num"));
    state.script.creature_partys[0].members_num = 30;
    CHECK(why().empty());
    state.script.strings[sizeof(state.script.strings) - 1] = 'x';
    CHECK(refused_for("strings"));
    state.script.strings[sizeof(state.script.strings) - 1] = '\0';
    state.script.next_string_offset = (int64_t)sizeof(state.script.strings) + 1;
    CHECK(refused_for("next_string_offset"));
}

TEST_CASE_METHOD(Fresh, "entries beyond a table's count are not looked at", "[kfx_game][game_state_validate]") {
    state.script.values[100].condit_idx = 123456789; // unused slot
    CHECK(why().empty());
}
