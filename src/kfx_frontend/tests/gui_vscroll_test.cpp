// kfx_frontend: gui_vscroll.c, per docs/refactor/testing/comprehensive/
// stage-08-comprehensive-library-passes.md's kfx_frontend row --
// gui_vscroll_total/gui_vscroll_max_offset were flagged as candidates
// "if a save_game_catalogue[] fixture is built first"; this is that
// fixture. save_game_catalogue (kfx_game's game_saves.c) is a
// dynamically-allocated array behind a raw `extern` pointer, not a
// state-struct field, so the fixture points it at a local static array
// directly rather than memset-resetting a struct. menu_is_active()
// (kfx_frontend's own frontend.cpp) reaches into kfx_platform's
// active_menus[]/struct GuiMenu (bflib_guibtns.h) -- pattern A on that
// array too.
#include <catch2/catch_test_macros.hpp>

#include "gui_vscroll.h"
#include "game_saves.h"
#include "frontend.h"
#include "bflib_guibtns.h"
#include "globals.h"

#include <cstring>

namespace {
struct CatalogueFixture {
    static const int64_t kCapacity = 32;
    struct CatalogueEntry entries[kCapacity]{};

    CatalogueFixture() {
        std::memset(entries, 0, sizeof(entries));
        std::memset(active_menus, 0, sizeof(active_menus));
        save_game_catalogue = entries;
        save_game_catalogue_count = 0;
    }
    ~CatalogueFixture() {
        save_game_catalogue = nullptr;
        save_game_catalogue_count = 0;
    }

    void mark_in_use(int64_t slot) { entries[slot].flags |= CEF_InUse; }

    void activate_save_menu() {
        active_menus[0].ident = GMnu_SAVE;
        active_menus[0].visual_state = 1; // menu_id_to_number() only matches a nonzero visual_state
    }
};
}

TEST_CASE_METHOD(CatalogueFixture, "gui_vscroll_total never drops below GUI_VSCROLL_VISIBLE, even with an empty catalogue", "[kfx_frontend][gui_vscroll]") {
    save_game_catalogue_count = 20;
    CHECK(gui_vscroll_total() == GUI_VSCROLL_VISIBLE);
}

TEST_CASE_METHOD(CatalogueFixture, "gui_vscroll_total tracks one past the highest in-use slot once that exceeds the visible floor", "[kfx_frontend][gui_vscroll]") {
    save_game_catalogue_count = 20;
    mark_in_use(10);
    CHECK(gui_vscroll_total() == 11); // Load menu: last_used(10) + 1
}

TEST_CASE_METHOD(CatalogueFixture, "gui_vscroll_total reveals one extra free slot while the Save menu is active", "[kfx_frontend][gui_vscroll]") {
    save_game_catalogue_count = 20;
    mark_in_use(10);
    activate_save_menu();
    CHECK(gui_vscroll_total() == 12); // last_used(10) + 2, not +1
}

TEST_CASE_METHOD(CatalogueFixture, "gui_vscroll_total never exceeds the catalogue's own slot count", "[kfx_frontend][gui_vscroll]") {
    save_game_catalogue_count = 20;
    mark_in_use(19); // the last slot
    activate_save_menu();
    CHECK(gui_vscroll_total() == 20); // last_used(19) + 2 == 21, clamped down to 20
}

TEST_CASE_METHOD(CatalogueFixture, "gui_vscroll_max_offset is zero once the total is at or below the visible floor", "[kfx_frontend][gui_vscroll]") {
    save_game_catalogue_count = 20;
    CHECK(gui_vscroll_max_offset() == 0);
}

TEST_CASE_METHOD(CatalogueFixture, "gui_vscroll_max_offset is total minus the visible floor once there's anything to scroll", "[kfx_frontend][gui_vscroll]") {
    save_game_catalogue_count = 20;
    mark_in_use(10);
    CHECK(gui_vscroll_total() == 11);
    CHECK(gui_vscroll_max_offset() == 3); // 11 - GUI_VSCROLL_VISIBLE(8)
}
