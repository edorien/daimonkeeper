// kfx_frontend: frontmenu_selectlist.c's pure pagination arithmetic,
// extracted from what used to be four copies of the same functions in
// frontmenu_select.c (one per campaign/level/mappack/mp-mappack select
// screen -- see docs/refactor/gui/00-overview.md Phase 1). Exercised
// against a fake item_count() and a local GuiButton, so none of this
// needs real game/campaign state. frontend_selectlist_scroll_to_offset()
// and frontend_selectlist_draw_scroll_tab() aren't covered here: both
// delegate to frontend_scroll_tab_to_offset()/frontend_draw_scroll_tab()
// (frontend.cpp), which read live mouse position / do real text
// rendering -- not pure, and already exercised indirectly wherever
// those two are used elsewhere.
#include <catch2/catch_test_macros.hpp>

#include "frontmenu_selectlist.h"
#include "bflib_guibtns.h"

#include <cstring>

namespace {
int64_t fake_item_count_value = 0;
int64_t fake_item_count() { return fake_item_count_value; }

struct SelectListFixture {
    struct FrontendSelectList list{};
    struct GuiButton gbtn{};

    SelectListFixture() {
        std::memset(&list, 0, sizeof(list));
        std::memset(&gbtn, 0, sizeof(gbtn));
        list.item_count = fake_item_count;
        list.items_visible_max = 7;
        list.row_base = FE_SELECTLIST_ROW_BASE;
        fake_item_count_value = 0;
    }
};
}

TEST_CASE_METHOD(SelectListFixture, "frontend_selectlist_set_visible shows count+1 rows while the list is shorter than the max", "[kfx_frontend][frontmenu_selectlist]") {
    fake_item_count_value = 3;
    frontend_selectlist_set_visible(&list);
    CHECK(list.items_visible == 4);
}

TEST_CASE_METHOD(SelectListFixture, "frontend_selectlist_set_visible caps rows at items_visible_max once the list is longer", "[kfx_frontend][frontmenu_selectlist]") {
    fake_item_count_value = 20;
    frontend_selectlist_set_visible(&list);
    CHECK(list.items_visible == 7);
}

TEST_CASE_METHOD(SelectListFixture, "frontend_selectlist_scroll_up decrements while above zero", "[kfx_frontend][frontmenu_selectlist]") {
    list.scroll_offset = 2;
    frontend_selectlist_scroll_up(&list);
    CHECK(list.scroll_offset == 1);
}

TEST_CASE_METHOD(SelectListFixture, "frontend_selectlist_scroll_up stays at zero", "[kfx_frontend][frontmenu_selectlist]") {
    list.scroll_offset = 0;
    frontend_selectlist_scroll_up(&list);
    CHECK(list.scroll_offset == 0);
}

TEST_CASE_METHOD(SelectListFixture, "frontend_selectlist_scroll_down advances while more rows are below", "[kfx_frontend][frontmenu_selectlist]") {
    fake_item_count_value = 20;
    list.items_visible = 7;
    list.scroll_offset = 0;
    frontend_selectlist_scroll_down(&list);
    CHECK(list.scroll_offset == 1);
}

TEST_CASE_METHOD(SelectListFixture, "frontend_selectlist_scroll_down stops at the last page", "[kfx_frontend][frontmenu_selectlist]") {
    fake_item_count_value = 20;
    list.items_visible = 7;
    list.scroll_offset = 14; // count(20) - items_visible(7) + 1 == 14, already at the max
    frontend_selectlist_scroll_down(&list);
    CHECK(list.scroll_offset == 14);
}

TEST_CASE_METHOD(SelectListFixture, "frontend_selectlist_up_maintain enables the button once scrolled away from the top", "[kfx_frontend][frontmenu_selectlist]") {
    list.scroll_offset = 1;
    frontend_selectlist_up_maintain(&list, &gbtn);
    CHECK((gbtn.flags & LbBtnF_Enabled) != 0);
}

TEST_CASE_METHOD(SelectListFixture, "frontend_selectlist_up_maintain disables the button at the top", "[kfx_frontend][frontmenu_selectlist]") {
    list.scroll_offset = 0;
    gbtn.flags |= LbBtnF_Enabled;
    frontend_selectlist_up_maintain(&list, &gbtn);
    CHECK((gbtn.flags & LbBtnF_Enabled) == 0);
}

TEST_CASE_METHOD(SelectListFixture, "frontend_selectlist_down_maintain disables the button at the last page", "[kfx_frontend][frontmenu_selectlist]") {
    fake_item_count_value = 20;
    list.items_visible = 7;
    list.scroll_offset = 14;
    gbtn.flags |= LbBtnF_Enabled;
    frontend_selectlist_down_maintain(&list, &gbtn);
    CHECK((gbtn.flags & LbBtnF_Enabled) == 0);
}

TEST_CASE_METHOD(SelectListFixture, "frontend_selectlist_row_to_item_index combines the row's content.lval with the scroll offset", "[kfx_frontend][frontmenu_selectlist]") {
    list.scroll_offset = 3;
    gbtn.content.lval = FE_SELECTLIST_ROW_BASE + 2; // 3rd visible row
    CHECK(frontend_selectlist_row_to_item_index(&list, &gbtn) == 5); // row 2 + offset 3
}

TEST_CASE_METHOD(SelectListFixture, "frontend_selectlist_row_maintain enables a row that maps to a real item", "[kfx_frontend][frontmenu_selectlist]") {
    fake_item_count_value = 10;
    list.scroll_offset = 0;
    gbtn.content.lval = FE_SELECTLIST_ROW_BASE; // row 0 -> item index 0, within range
    frontend_selectlist_row_maintain(&list, &gbtn);
    CHECK((gbtn.flags & LbBtnF_Enabled) != 0);
}

TEST_CASE_METHOD(SelectListFixture, "frontend_selectlist_row_maintain disables a row past the end of the list", "[kfx_frontend][frontmenu_selectlist]") {
    fake_item_count_value = 2;
    list.scroll_offset = 0;
    gbtn.content.lval = FE_SELECTLIST_ROW_BASE + 5; // item index 5, out of range
    gbtn.flags |= LbBtnF_Enabled;
    frontend_selectlist_row_maintain(&list, &gbtn);
    CHECK((gbtn.flags & LbBtnF_Enabled) == 0);
}

TEST_CASE_METHOD(SelectListFixture, "frontend_selectlist_update resets the offset to zero once the list becomes empty", "[kfx_frontend][frontmenu_selectlist]") {
    fake_item_count_value = 0;
    list.scroll_offset = 4;
    frontend_selectlist_update(&list);
    CHECK(list.scroll_offset == 0);
}

TEST_CASE_METHOD(SelectListFixture, "frontend_selectlist_update clamps a scroll offset left over from a longer list", "[kfx_frontend][frontmenu_selectlist]") {
    fake_item_count_value = 5;
    list.items_visible = 6; // as frontend_selectlist_set_visible would compute: min(5+1, items_visible_max)
    list.scroll_offset = 4;
    frontend_selectlist_update(&list);
    CHECK(list.scroll_offset == 0); // count(5) - items_visible(6) + 1 == 0
}
