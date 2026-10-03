// kfx_config: every default callback table is complete (no NULL slot),
// and the ScopedPortOverride test helper.
// The default tables are C designated initializers, and C compilers don't
// warn when a member is left out -- it is silently zero-filled. main.cpp's
// ports_verify_wired() checks the real tables at startup; this checks the
// defaults, which the unit-test binaries run against.
// See docs/refactor-pass2/stage-01-callback-hygiene.md.
#include <catch2/catch_test_macros.hpp>

#include "config.h"
#include "port_check.h"
#include "ports/script_port.h"
#include "kfx_config/tests/scoped_port_override.h"
#include "ports/ui_port.h"

TEST_CASE("every kfx_config default callback table has all its slots set", "[kfx_config][ports]") {
    // set_*(nullptr) reinstalls the default table.
    set_ui_port(nullptr);
    CHECK(KFX_TABLE_COMPLETE(ui_port, struct UiPort));
    set_script_port(nullptr);
    CHECK(KFX_TABLE_COMPLETE(script_port, struct ScriptPort));
}

TEST_CASE("ScopedPortOverride installs a copy for the scope and restores the previous table", "[kfx_config][ports]") {
    set_ui_port(nullptr);
    const struct UiPort *before = ui_port;
    {
        ScopedPortOverride<UiPort> port(ui_port, set_ui_port);
        port->timer_enabled = []() -> TbBool { return true; };
        CHECK(ui_port != before);
        CHECK(ui_timer_enabled());
        CHECK(ui_menu_is_active(0) == 0); // untouched entries keep the installed behaviour
    }
    CHECK(ui_port == before);
    CHECK_FALSE(ui_timer_enabled());
}
