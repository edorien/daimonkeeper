#include "ftest_gui_seam_ingame.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include "../ftest.h"
#include "../ftest_util.h"

#include "globals.h"
#include "game_legacy.h"
#include "config_keeperfx.h"
#include "player_data.h"
#include "packet_data.h"
#include "gui_frontmenu.h"      // turn_on_menu
#include "frontend.h"           // menu_is_active
#include "frontgui_ingame.h"    // ingame_imgui_menu_active / _modal_active / _quitmenu_confirm

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

static FTestActionResult action001__seam_predicates(struct FTestActionArgs *const args);
static FTestActionResult action002__options_launcher(struct FTestActionArgs *const args);
static FTestActionResult action003__quit_packet(struct FTestActionArgs *const args);

TbBool ftest_gui_seam_ingame_init()
{
    ftest_append_action(action001__seam_predicates,  30, NULL);
    ftest_append_action(action002__options_launcher,  3, NULL);
    ftest_append_action(action003__quit_packet,       3, NULL);
    return true;
}

/* GMnu_QUIT is the Phase 0 proof menu. With ImGui enabled and a level
 * running, the seam predicates must report it migrated, and once it is
 * opened, report it modal -- while the legacy menu-stack machinery
 * (turn_on_menu / menu_is_active) is untouched: the ImGui path only swaps
 * *draw* and *input*, never menu registration. */
static FTestActionResult action001__seam_predicates(struct FTestActionArgs *const args)
{
    if (ingame_gui_use_classic_hud())
    {
        FTEST_FAIL_TEST("ImGui in-game HUD is not active -- run without GUI_ICON_PACK=CLASSIC");
        return FTRs_Go_To_Next_Action;
    }

    /* ingame_imgui_menu_active() is "is this GMnu_* one the ImGui path owns"
     * (mirrors frontend_imgui_screen_active) -- it does not depend on the
     * menu being open. */
    if (!ingame_imgui_menu_active(GMnu_QUIT))
    {
        FTEST_FAIL_TEST("ingame_imgui_menu_active(GMnu_QUIT) false -- GMnu_QUIT is the Phase 0 migrated menu");
        return FTRs_Go_To_Next_Action;
    }
    if (ingame_imgui_menu_active(GMnu_EVENT))
    {
        FTEST_FAIL_TEST("ingame_imgui_menu_active(GMnu_EVENT) true -- GMnu_EVENT is not migrated yet");
        return FTRs_Go_To_Next_Action;
    }

    /* ingame_imgui_modal_active() *does* depend on a migrated monopoly menu
     * being up -- nothing is open yet. */
    if (ingame_imgui_modal_active())
    {
        FTEST_FAIL_TEST("ingame_imgui_modal_active() true with no menu open");
        return FTRs_Go_To_Next_Action;
    }

    turn_on_menu(GMnu_QUIT);

    if (!menu_is_active(GMnu_QUIT))
    {
        FTEST_FAIL_TEST("turn_on_menu(GMnu_QUIT) did not register the menu -- stack machinery broken");
        return FTRs_Go_To_Next_Action;
    }
    if (!ingame_imgui_modal_active())
    {
        FTEST_FAIL_TEST("ingame_imgui_modal_active() false while a migrated monopoly menu is up");
        turn_off_menu(GMnu_QUIT);
        return FTRs_Go_To_Next_Action;
    }

    turn_off_menu(GMnu_QUIT);
    FTESTLOG("seam predicates OK: GMnu_QUIT migrated + modal once opened, menu-stack machinery intact");
    return FTRs_Go_To_Next_Action;
}

/* Phase 1: GMnu_OPTIONS (the pause-menu launcher) is migrated + modal.
 * (The "a legacy child menu stacked above the launcher wins input" case is
 * exercised interactively -- opening it here and driving its fade to
 * completion is not reliable headless.) */
static FTestActionResult action002__options_launcher(struct FTestActionArgs *const args)
{
    if (!ingame_imgui_menu_active(GMnu_OPTIONS))
    {
        FTEST_FAIL_TEST("ingame_imgui_menu_active(GMnu_OPTIONS) false -- Phase 1 migrated the launcher");
        return FTRs_Go_To_Next_Action;
    }
    if (ingame_imgui_menu_active(GMnu_VIDEO))
    {
        FTEST_FAIL_TEST("ingame_imgui_menu_active(GMnu_VIDEO) true -- GMnu_VIDEO is not migrated");
        return FTRs_Go_To_Next_Action;
    }

    turn_on_menu(GMnu_OPTIONS);
    if (!menu_is_active(GMnu_OPTIONS) || !ingame_imgui_modal_active())
    {
        FTEST_FAIL_TEST("GMnu_OPTIONS not registered / not modal after opening");
        turn_off_menu(GMnu_OPTIONS);
        return FTRs_Go_To_Next_Action;
    }
    turn_off_menu(GMnu_OPTIONS);

    /* Phase 1b: GMnu_LOAD / GMnu_SAVE slot lists are migrated too. Only the
     * predicate is asserted here -- actually opening them runs the classic
     * init_{load,save}_menu create_cb (catalogue file I/O + a pause packet
     * + the menu fade), which stalls headless with frame_skip; that path is
     * exercised interactively. */
    if (!ingame_imgui_menu_active(GMnu_LOAD) || !ingame_imgui_menu_active(GMnu_SAVE))
    {
        FTEST_FAIL_TEST("ingame_imgui_menu_active(GMnu_LOAD/SAVE) false -- Phase 1b migrated the slot lists");
        return FTRs_Go_To_Next_Action;
    }

    FTESTLOG("pause-menu launcher OK: GMnu_OPTIONS modal, GMnu_LOAD/SAVE migrated, menu-stack machinery intact");
    return FTRs_Go_To_Next_Action;
}

/* The migrated quit-confirm "Yes" action must send exactly the packet the
 * legacy gui_quit_game() does. Checked directly on the local packet before
 * the turn's process_packets() acts on it (which quits the level). */
static FTestActionResult action003__quit_packet(struct FTestActionArgs *const args)
{
    struct Packet *pckt = get_packet(my_player_number);
    if (pckt->action != PckA_None)
    {
        FTEST_FAIL_TEST("local packet already carries action %" PRIu64 " before the quit test", (uint64_t)pckt->action);
        return FTRs_Go_To_Next_Action;
    }

    ingame_quitmenu_confirm();

    pckt = get_packet(my_player_number);
    if (pckt->action != PckA_QuitToMainMenu)
    {
        FTEST_FAIL_TEST("migrated quit 'Yes' produced action %" PRIu64 ", expected PckA_QuitToMainMenu (%" PRIu64 ")",
                        (uint64_t)pckt->action, (uint64_t)PckA_QuitToMainMenu);
        return FTRs_Go_To_Next_Action;
    }
    if (menu_is_active(GMnu_QUIT))
    {
        FTEST_FAIL_TEST("GMnu_QUIT still active after the migrated confirm -- should have been turned off");
        return FTRs_Go_To_Next_Action;
    }

    FTESTLOG("migrated quit 'Yes' -> PckA_QuitToMainMenu, menu closed (matches legacy gui_quit_game)");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif /* FUNCTESTING */
