// kfx_config: every port's unwired defaults (refactor pass 2, S15,
// docs/refactor-pass2/stage-15-ports-and-events.md). Generated from each
// port's .def: every default is called with value-initialised arguments and
// must return the default the .def lists. Replaces the hand-written
// "every default is a no-op" tests of the old callback tables.
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstring>
#include <string>

template <typename R, typename... A>
static R call_default(R (*f)(A...)) { return f(A{}...); }

// Defaults compare by value; strings by content, not address.
template <typename T, typename U>
static bool same_default(T a, U b) { return a == b; }
static bool same_default(const char *a, const char *b) { return a == b || (a && b && std::strcmp(a, b) == 0); }
template <typename T>
static bool same_default(T *a, long b) { return b == 0 && a == nullptr; }

#include "ports/script_port.h"
#include "ports/ui_port.h"
#include "ports/audio_port.h"
#include "ports/game_port.h"
#include "ports/session_loop_port.h"
#include "ports/net_port.h"
#include "ports/render_port.h"
#include "ports/sim_port.h"
#include "ports/pathfinding_world_port.h"
#include "ports/ai_port.h"
#include "ports/editor_port.h"
// PORTS-INCLUDES

TEST_CASE("ScriptPort defaults are unwired no-ops returning their listed defaults", "[kfx_config][ports][script_port]") {
#define KFX_PORT_VOID(name, params, args) call_default(script_port_defaults.name);
#define KFX_PORT_RET(ret, name, params, args, dflt) CHECK(same_default(call_default(script_port_defaults.name), (dflt)));
#define KFX_PORT_VOIDX(name, params, args, stmt) call_default(script_port_defaults.name);
#define KFX_PORT_RETX(ret, name, params, args, dflt, stmt) CHECK(same_default(call_default(script_port_defaults.name), (dflt)));
#define KFX_PORT_RETS(ret, name, params, args, body) call_default(script_port_defaults.name);
#include "ports/script_port.def"
#undef KFX_PORT_VOID
#undef KFX_PORT_RET
#undef KFX_PORT_VOIDX
#undef KFX_PORT_RETX
#undef KFX_PORT_RETS
}

TEST_CASE("ScriptPort's unwired lua_resync_export reports an empty payload", "[kfx_config][ports][script_port]") {
    size_t len = 999;
    const char *data = script_port_defaults.lua_resync_export(&len);
    REQUIRE(data != nullptr);
    CHECK(data[0] == '\0');
    CHECK(len == 0);
}

TEST_CASE("UiPort defaults are unwired no-ops returning their listed defaults", "[kfx_config][ports][ui_port]") {
#define KFX_PORT_VOID(name, params, args) call_default(ui_port_defaults.name);
#define KFX_PORT_RET(ret, name, params, args, dflt) CHECK(same_default(call_default(ui_port_defaults.name), (dflt)));
#define KFX_PORT_VOIDX(name, params, args, stmt) call_default(ui_port_defaults.name);
#define KFX_PORT_RETX(ret, name, params, args, dflt, stmt) CHECK(same_default(call_default(ui_port_defaults.name), (dflt)));
#define KFX_PORT_RETS(ret, name, params, args, body) call_default(ui_port_defaults.name);
#include "ports/ui_port.def"
#undef KFX_PORT_VOID
#undef KFX_PORT_RET
#undef KFX_PORT_VOIDX
#undef KFX_PORT_RETX
#undef KFX_PORT_RETS
}

TEST_CASE("UiPort's unwired defaults leave their out-parameters safe", "[kfx_config][ports][ui_port]") {
    char dest[1] = {'Y'};
    ui_port_defaults.get_high_score_entry(dest, 0);
    CHECK(dest[0] == 'Y'); // the dest_size>0 guard skips the write
    char dest2[4] = {'Y', 'Y', 'Y', 'Y'};
    ui_port_defaults.get_high_score_entry(dest2, sizeof(dest2));
    CHECK(dest2[0] == '\0');
    struct GameTime gt = {};
    gt.Hours = 5;
    ui_port_defaults.get_game_time(&gt, 0, 0);
    CHECK(gt.Hours == 0);
    size_t len = 7;
    CHECK(std::string(ui_port_defaults.resync_export_frontend_state(&len)).empty());
    CHECK(len == 0);
}

TEST_CASE("AudioFeedbackPort defaults are unwired no-ops returning their listed defaults", "[kfx_config][ports][audio_port]") {
#define KFX_PORT_VOID(name, params, args) call_default(audio_port_defaults.name);
#define KFX_PORT_RET(ret, name, params, args, dflt) CHECK(same_default(call_default(audio_port_defaults.name), (dflt)));
#define KFX_PORT_VOIDX(name, params, args, stmt) call_default(audio_port_defaults.name);
#define KFX_PORT_RETX(ret, name, params, args, dflt, stmt) CHECK(same_default(call_default(audio_port_defaults.name), (dflt)));
#define KFX_PORT_RETS(ret, name, params, args, body) call_default(audio_port_defaults.name);
#include "ports/audio_port.def"
#undef KFX_PORT_VOID
#undef KFX_PORT_RET
#undef KFX_PORT_VOIDX
#undef KFX_PORT_RETX
#undef KFX_PORT_RETS
}

TEST_CASE("GamePort defaults are unwired no-ops returning their listed defaults", "[kfx_config][ports][game_port]") {
#define KFX_PORT_VOID(name, params, args) call_default(game_port_defaults.name);
#define KFX_PORT_RET(ret, name, params, args, dflt) CHECK(same_default(call_default(game_port_defaults.name), (dflt)));
#define KFX_PORT_VOIDX(name, params, args, stmt) call_default(game_port_defaults.name);
#define KFX_PORT_RETX(ret, name, params, args, dflt, stmt) CHECK(same_default(call_default(game_port_defaults.name), (dflt)));
#define KFX_PORT_RETS(ret, name, params, args, body) call_default(game_port_defaults.name);
#include "ports/game_port.def"
#undef KFX_PORT_VOID
#undef KFX_PORT_RET
#undef KFX_PORT_VOIDX
#undef KFX_PORT_RETX
#undef KFX_PORT_RETS
}

TEST_CASE("SessionLoopPort defaults are unwired no-ops returning their listed defaults", "[kfx_config][ports][session_loop_port]") {
#define KFX_PORT_VOID(name, params, args) call_default(session_loop_port_defaults.name);
#define KFX_PORT_RET(ret, name, params, args, dflt) CHECK(same_default(call_default(session_loop_port_defaults.name), (dflt)));
#define KFX_PORT_VOIDX(name, params, args, stmt) call_default(session_loop_port_defaults.name);
#define KFX_PORT_RETX(ret, name, params, args, dflt, stmt) CHECK(same_default(call_default(session_loop_port_defaults.name), (dflt)));
#define KFX_PORT_RETS(ret, name, params, args, body) call_default(session_loop_port_defaults.name);
#include "ports/session_loop_port.def"
#undef KFX_PORT_VOID
#undef KFX_PORT_RET
#undef KFX_PORT_VOIDX
#undef KFX_PORT_RETX
#undef KFX_PORT_RETS
}

TEST_CASE("NetPort defaults are unwired no-ops returning their listed defaults", "[kfx_config][ports][net_port]") {
#define KFX_PORT_VOID(name, params, args) call_default(net_port_defaults.name);
#define KFX_PORT_RET(ret, name, params, args, dflt) CHECK(same_default(call_default(net_port_defaults.name), (dflt)));
#define KFX_PORT_VOIDX(name, params, args, stmt) call_default(net_port_defaults.name);
#define KFX_PORT_RETX(ret, name, params, args, dflt, stmt) CHECK(same_default(call_default(net_port_defaults.name), (dflt)));
#define KFX_PORT_RETS(ret, name, params, args, body) call_default(net_port_defaults.name);
#include "ports/net_port.def"
#undef KFX_PORT_VOID
#undef KFX_PORT_RET
#undef KFX_PORT_VOIDX
#undef KFX_PORT_RETX
#undef KFX_PORT_RETS
}

TEST_CASE("RenderPort defaults are unwired no-ops returning their listed defaults", "[kfx_config][ports][render_port]") {
#define KFX_PORT_VOID(name, params, args) call_default(render_port_defaults.name);
#define KFX_PORT_RET(ret, name, params, args, dflt) CHECK(same_default(call_default(render_port_defaults.name), (dflt)));
#define KFX_PORT_VOIDX(name, params, args, stmt) call_default(render_port_defaults.name);
#define KFX_PORT_RETX(ret, name, params, args, dflt, stmt) CHECK(same_default(call_default(render_port_defaults.name), (dflt)));
#define KFX_PORT_RETS(ret, name, params, args, body) call_default(render_port_defaults.name);
#include "ports/render_port.def"
#undef KFX_PORT_VOID
#undef KFX_PORT_RET
#undef KFX_PORT_VOIDX
#undef KFX_PORT_RETX
#undef KFX_PORT_RETS
}

TEST_CASE("SimPort defaults are unwired no-ops returning their listed defaults", "[kfx_config][ports][sim_port]") {
#define KFX_PORT_VOID(name, params, args) call_default(sim_port_defaults.name);
#define KFX_PORT_RET(ret, name, params, args, dflt) CHECK(same_default(call_default(sim_port_defaults.name), (dflt)));
#define KFX_PORT_VOIDX(name, params, args, stmt) call_default(sim_port_defaults.name);
#define KFX_PORT_RETX(ret, name, params, args, dflt, stmt) CHECK(same_default(call_default(sim_port_defaults.name), (dflt)));
#define KFX_PORT_RETS(ret, name, params, args, body) call_default(sim_port_defaults.name);
#include "ports/sim_port.def"
#undef KFX_PORT_VOID
#undef KFX_PORT_RET
#undef KFX_PORT_VOIDX
#undef KFX_PORT_RETX
#undef KFX_PORT_RETS
}

TEST_CASE("PathfindingWorldPort defaults are unwired no-ops returning their listed defaults", "[kfx_config][ports][pathfinding_world_port]") {
#define KFX_PORT_VOID(name, params, args) call_default(pathfinding_world_port_defaults.name);
#define KFX_PORT_RET(ret, name, params, args, dflt) CHECK(same_default(call_default(pathfinding_world_port_defaults.name), (dflt)));
#define KFX_PORT_VOIDX(name, params, args, stmt) call_default(pathfinding_world_port_defaults.name);
#define KFX_PORT_RETX(ret, name, params, args, dflt, stmt) CHECK(same_default(call_default(pathfinding_world_port_defaults.name), (dflt)));
#define KFX_PORT_RETS(ret, name, params, args, body) call_default(pathfinding_world_port_defaults.name);
#include "ports/pathfinding_world_port.def"
#undef KFX_PORT_VOID
#undef KFX_PORT_RET
#undef KFX_PORT_VOIDX
#undef KFX_PORT_RETX
#undef KFX_PORT_RETS
}

TEST_CASE("AiPort defaults are unwired no-ops returning their listed defaults", "[kfx_config][ports][ai_port]") {
#define KFX_PORT_VOID(name, params, args) call_default(ai_port_defaults.name);
#define KFX_PORT_RET(ret, name, params, args, dflt) CHECK(same_default(call_default(ai_port_defaults.name), (dflt)));
#define KFX_PORT_VOIDX(name, params, args, stmt) call_default(ai_port_defaults.name);
#define KFX_PORT_RETX(ret, name, params, args, dflt, stmt) CHECK(same_default(call_default(ai_port_defaults.name), (dflt)));
#define KFX_PORT_RETS(ret, name, params, args, body) call_default(ai_port_defaults.name);
#include "ports/ai_port.def"
#undef KFX_PORT_VOID
#undef KFX_PORT_RET
#undef KFX_PORT_VOIDX
#undef KFX_PORT_RETX
#undef KFX_PORT_RETS
}

TEST_CASE("EditorPort defaults are unwired no-ops returning their listed defaults", "[kfx_config][ports][editor_port]") {
#define KFX_PORT_VOID(name, params, args) call_default(editor_port_defaults.name);
#define KFX_PORT_RET(ret, name, params, args, dflt) CHECK(same_default(call_default(editor_port_defaults.name), (dflt)));
#define KFX_PORT_VOIDX(name, params, args, stmt) call_default(editor_port_defaults.name);
#define KFX_PORT_RETX(ret, name, params, args, dflt, stmt) CHECK(same_default(call_default(editor_port_defaults.name), (dflt)));
#define KFX_PORT_RETS(ret, name, params, args, body) call_default(editor_port_defaults.name);
#include "ports/editor_port.def"
#undef KFX_PORT_VOID
#undef KFX_PORT_RET
#undef KFX_PORT_VOIDX
#undef KFX_PORT_RETX
#undef KFX_PORT_RETS
}

// PORTS-TESTS
