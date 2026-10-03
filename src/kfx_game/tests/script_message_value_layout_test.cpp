// QUICK_MESSAGE / DISPLAY_MESSAGE keep a message index (or string id) and a chat-icon type in one
// ScriptValue union (lvl_script.h). The layout was written for a 32-bit `long`; on 64-bit Linux `long` is 8
// bytes, so the icon type written to chars[6] (QUICK_MESSAGE) / chars[7] (DISPLAY_MESSAGE) landed *inside* the
// 8-byte index in longs[0]/ulongs[0]. Any message with a non-blank icon -- e.g. QUICK_MESSAGE(0,"...",PLAYER0),
// as the biervampir faction boxes use -- then indexed kfx_sim_state.quick_messages[] with a huge value and the
// game crashed in message_add() the moment the block ran.
#include <catch2/catch_test_macros.hpp>

#include "lvl_script.h"
#include "lvl_filesdk1.h"
#include "level_script_override.h"
#include "kfx_game_state.h"
#include "ports/ui_port.h"

#include <cstring>
#include <string>
#include <vector>

extern "C" void process_values(void); // lvl_script.c (no public header)

namespace {

const LevelNumber kLevel = 9877;

std::vector<std::string> g_messages;
std::vector<int64_t> g_icon_types;

void capture_message(char type, int64_t idx, const char *text)
{
    g_messages.push_back(text != nullptr ? text : "<null>");
    g_icon_types.push_back((int64_t)type);
}

struct Capture
{
    struct UiPort fake;
    const struct UiPort *saved;
    Capture()
    {
        std::memset(&kfx_game_state, 0, sizeof(kfx_game_state));
        level_script_override_clear();
        g_messages.clear();
        g_icon_types.clear();
        saved = ui_port;
        fake = *ui_port;
        fake.message_add = &capture_message;
        set_ui_port(&fake);
    }
    ~Capture()
    {
        set_ui_port(saved);
        level_script_override_clear();
        std::memset(&kfx_game_state, 0, sizeof(kfx_game_state));
    }
};

} // namespace

TEST_CASE_METHOD(Capture, "QUICK_MESSAGE with a player icon delivers its own text", "[kfx_game][script_message]") {
    level_script_override_set(kLevel, "REM nothing\n",
        "LEVEL_VERSION(1)\nQUICK_MESSAGE(0,\"Red player chose their faction!\",PLAYER0)\n");
    REQUIRE(preload_script(kLevel));
    REQUIRE(load_script(kLevel));
    process_values(); // unconditional commands already ran at load; this covers any that were queued
    REQUIRE(g_messages.size() == 1);
    CHECK(g_messages[0] == "Red player chose their faction!");
    CHECK(g_icon_types[0] == MsgType_Player);
}

TEST_CASE_METHOD(Capture, "QUICK_MESSAGE index and icon do not overlap in the value", "[kfx_game][script_message]") {
    // Two different messages with different icons: each must come back with its own text.
    level_script_override_set(kLevel, "REM nothing\n",
        "LEVEL_VERSION(1)\nQUICK_MESSAGE(0,\"first\",PLAYER0)\nQUICK_MESSAGE(1,\"second\",PLAYER1)\nQUICK_MESSAGE(2,\"third\",None)\n");
    REQUIRE(preload_script(kLevel));
    REQUIRE(load_script(kLevel));
    process_values();
    REQUIRE(g_messages.size() == 3);
    CHECK(g_messages[0] == "first");
    CHECK(g_messages[1] == "second");
    CHECK(g_messages[2] == "third");
    // ...and the icon type (the `None` icon is MsgType_Blank, non-zero: the value that used to corrupt the index).
    CHECK(g_icon_types[2] == MsgType_Blank);
}

TEST_CASE("ScriptValue longs are 32-bit, as the field layout assumes", "[kfx_game][script_message]") {
    struct ScriptValue v;
    CHECK(sizeof(v.longs[0]) == 4);
    CHECK(sizeof(v.ulongs[0]) == 4);
    CHECK(sizeof(v.bytes) == 32); // the union stays 32 bytes
    // The command layouts put small fields right after longs[0] (chars[4] in the action-point commands,
    // chars[6]/chars[7] in the message commands): they must not overlap it.
    v.longs[0] = -1;
    v.chars[4] = 0;
    v.chars[6] = 0;
    v.chars[7] = 0;
    CHECK(v.longs[0] == -1);
}
