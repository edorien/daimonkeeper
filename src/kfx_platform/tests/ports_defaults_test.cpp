// kfx_platform: every port's unwired defaults (refactor pass 2, S15,
// docs/refactor-pass2/stage-15-ports-and-events.md). Generated from each
// port's .def: every default is called with value-initialised arguments and
// must return the default the .def lists. Replaces the hand-written
// "every default is a no-op" tests of the old callback tables.
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstring>

template <typename R, typename... A>
static R call_default(R (*f)(A...)) { return f(A{}...); }

// Defaults compare by value; strings by content, not address.
template <typename T, typename U>
static bool same_default(T a, U b) { return a == b; }
static bool same_default(const char *a, const char *b) { return a == b || (a && b && std::strcmp(a, b) == 0); }
template <typename T>
static bool same_default(T *a, long b) { return b == 0 && a == nullptr; }

#include "ports/file_path_port.h"
#include "ports/sound_host_port.h"
#include "ports/input_focus_port.h"
#include "ports/display_host_port.h"
// PORTS-INCLUDES

TEST_CASE("FilePathPort defaults are unwired no-ops returning their listed defaults", "[kfx_platform][ports][file_path_port]") {
#define KFX_PORT_VOID(name, params, args) call_default(file_path_port_defaults.name);
#define KFX_PORT_RET(ret, name, params, args, dflt) CHECK(same_default(call_default(file_path_port_defaults.name), (dflt)));
#define KFX_PORT_VOIDX(name, params, args, stmt) call_default(file_path_port_defaults.name);
#define KFX_PORT_RETX(ret, name, params, args, dflt, stmt) CHECK(same_default(call_default(file_path_port_defaults.name), (dflt)));
#define KFX_PORT_RETS(ret, name, params, args, body) call_default(file_path_port_defaults.name);
#include "ports/file_path_port.def"
#undef KFX_PORT_VOID
#undef KFX_PORT_RET
#undef KFX_PORT_VOIDX
#undef KFX_PORT_RETX
#undef KFX_PORT_RETS
}

TEST_CASE("SoundHostPort defaults are unwired no-ops returning their listed defaults", "[kfx_platform][ports][sound_host_port]") {
#define KFX_PORT_VOID(name, params, args) call_default(sound_host_port_defaults.name);
#define KFX_PORT_RET(ret, name, params, args, dflt) CHECK(same_default(call_default(sound_host_port_defaults.name), (dflt)));
#define KFX_PORT_VOIDX(name, params, args, stmt) call_default(sound_host_port_defaults.name);
#define KFX_PORT_RETX(ret, name, params, args, dflt, stmt) CHECK(same_default(call_default(sound_host_port_defaults.name), (dflt)));
#define KFX_PORT_RETS(ret, name, params, args, body) call_default(sound_host_port_defaults.name);
#include "ports/sound_host_port.def"
#undef KFX_PORT_VOID
#undef KFX_PORT_RET
#undef KFX_PORT_VOIDX
#undef KFX_PORT_RETX
#undef KFX_PORT_RETS
}

TEST_CASE("InputFocusPort defaults are unwired no-ops returning their listed defaults", "[kfx_platform][ports][input_focus_port]") {
#define KFX_PORT_VOID(name, params, args) call_default(input_focus_port_defaults.name);
#define KFX_PORT_RET(ret, name, params, args, dflt) CHECK(same_default(call_default(input_focus_port_defaults.name), (dflt)));
#define KFX_PORT_VOIDX(name, params, args, stmt) call_default(input_focus_port_defaults.name);
#define KFX_PORT_RETX(ret, name, params, args, dflt, stmt) CHECK(same_default(call_default(input_focus_port_defaults.name), (dflt)));
#define KFX_PORT_RETS(ret, name, params, args, body) call_default(input_focus_port_defaults.name);
#include "ports/input_focus_port.def"
#undef KFX_PORT_VOID
#undef KFX_PORT_RET
#undef KFX_PORT_VOIDX
#undef KFX_PORT_RETX
#undef KFX_PORT_RETS
}

TEST_CASE("DisplayHostPort defaults are unwired no-ops returning their listed defaults", "[kfx_platform][ports][display_host_port]") {
#define KFX_PORT_VOID(name, params, args) call_default(display_host_port_defaults.name);
#define KFX_PORT_RET(ret, name, params, args, dflt) CHECK(same_default(call_default(display_host_port_defaults.name), (dflt)));
#define KFX_PORT_VOIDX(name, params, args, stmt) call_default(display_host_port_defaults.name);
#define KFX_PORT_RETX(ret, name, params, args, dflt, stmt) CHECK(same_default(call_default(display_host_port_defaults.name), (dflt)));
#define KFX_PORT_RETS(ret, name, params, args, body) call_default(display_host_port_defaults.name);
#include "ports/display_host_port.def"
#undef KFX_PORT_VOID
#undef KFX_PORT_RET
#undef KFX_PORT_VOIDX
#undef KFX_PORT_RETX
#undef KFX_PORT_RETS
}

// PORTS-TESTS
