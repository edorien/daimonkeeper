#include "pre_inc.h"
#include "frontgui_screens.h"
#include "frontgui_ingame.h" // Phase 0: the in-game HUD/menu ImGui arm
#include "frontgui_widgets.h"
#include "frontgui_skirmish_setup.h" // Skirmish Setup tab (docs/refactor/skirmish/)
#include "skirmish_setup.h"
#include "main_game.h" // default_loc_player -- the Skirmish human slot
#include "frontgui_deferred.h" // FeDeferredQueue
#include "frontgui_offscreen.h" // FeOffscreenTarget -- land-preview raster capture
#include "frontgui_style.h"
#include "frontgui_stylesheet_test.h"
#include "renderer/RendererManager.h"
#include "frontend.h"
#include "front_credits.h"
#include "front_easter.h"
#include "frontmenu_options.h"
#include "front_highscore.h"
#include "kjm_input.h"
#include "config_settings.h"
#include "game_saves.h"
#include "player_data.h"
#include "packets.h"
#include "config_strings.h"
#include "config_settingschema.h" // Phase G §6.3 -- the generic settings-tab renderer, frontgui_options_frame()
#include "config_campaigns.h"
#include "config.h" // get_level_info/get_first_level_info et al
#include "frontmenu_select.h"
#include "frontmenu_landpreview.h"
#include "game_campaign_progress.h" // Phase C: the Campaign Select land-view-graphics slider
#include "bflib_guibtns.h" // struct GuiButton, for the two synthetic gbtns land preview rendering needs
#include "bflib_video.h" // TbGraphicsWindow, LbScreen{Store,Load,Set}GraphicsWindow, TbPixel
#include "gui_draw.h" // get_frontmenu_background_area_rect -- Phase A/B menu backdrop
#include "bflib_planar.h" // struct TbRect
#include "front_lvlstats.h"
#include "frontmenu_net.h"
#include "front_network.h"
#include "net_main.h" // FrontendNetService, MAX_NET_USERS, net_player[]/net_session[]/etc.
#include "net_game.h" // setup_old_network_service
#include "bflib_enet.h" // GetPing
#include "net_exchange_common.h" // send_network_chat_message
#include "bflib_datetm.h" // LbTimerClock
#include "frontmenu_ingame_evnt.h" // timer_enabled
#include "kfx_sim_state.h" // kfx_sim_state.Timer/TimerGame
#include "kfx_net_state.h" // autopilot comp_player_* flags (in-game options fold)
#include <cstdio>
#include <vector>
#include "post_inc.h"

namespace {
    // frontend_set_state() (frontend.cpp) has arbitrarily heavy side effects
    // -- turn_on_menu/turn_off_menu, palette fades, campaign/level setup for
    // some target states -- found live (real crash, real stack trace) to
    // corrupt ImGui's window stack when called synchronously from inside a
    // screen function's own Begin()/End() scope: the very next ImGui:: call
    // afterwards (a plain SameLine()) segfaulted on a null-ish
    // g.CurrentWindow. Every screen below requests a transition instead of
    // calling frontend_set_state() directly; FrontendImGuiFrame() applies
    // the request at the very start of the *next* frame, before any ImGui
    // window from this module is open.
    int64_t s_pending_state = -1; // a FrontendMenuState, or -1 for "none pending"
    int64_t s_pending_load_slot = -1; // a save_game_catalogue[] index, or -1 for "none pending"

    void request_frontend_state(FrontendMenuState state)
    {
        s_pending_state = (int64_t)state;
    }

    // Generic one-shot deferred action, invoked at the very start of the
    // next frame's FrontendImGuiFrame(), before any ImGui window from this
    // module is open -- same "never call frontend_set_state()-adjacent
    // heavy code from inside an active window" reasoning as s_pending_state
    // above, generalized: Phase F's network flow has several click
    // handlers that reach frontend_set_state() indirectly, sometimes
    // several layers down and across the kfx_net/kfx_frontend boundary
    // (setup_network_service() -> net_callbacks->enter_net_session_screen()
    // -> frontend_set_state()) or with a conditional fallback baked in
    // (init_menu_state_on_net_stats_exit(), frontnet_return_to_session_menu())
    // -- too deep or too branchy to give each one its own
    // _resolve()-returns-target-state wrapper the way Phase E's simpler,
    // single-layer cases could. A raw function pointer with no captured
    // arguments is enough: a call site that needs to pass a parameter
    // (e.g. "which network service index was clicked") stores it in its
    // own small file-scope holder immediately before requesting the
    // action, and the trampoline function reads it back when it runs.
    // (FeDeferredQueue, docs/refactor/ingame-gui/10-maintainability-refactors.md §3.)
    FeDeferredQueue s_deferred_action;

    void request_pending_action(void (*fn)(void))
    {
        s_deferred_action.push(fn);
    }

    // Holder for frontnet_service_select_by_index()'s parameter -- see
    // s_pending_action's own comment on why a parameterized deferred
    // action needs one of these per call site.
    int64_t s_pending_net_service_index = -1;

    void run_pending_net_service_select(void)
    {
        int64_t i = s_pending_net_service_index;
        s_pending_net_service_index = -1;
        frontnet_service_select_by_index(i);
    }

    void run_pending_net_return_to_session_menu(void)
    {
        frontnet_return_to_session_menu(nullptr);
    }

    bool state_is_migrated(int64_t state)
    {
        switch (state)
        {
            case FeSt_STORY_POEM:
            case FeSt_STORY_BIRTHDAY:
            case FeSt_CREDITS:
            case FeSt_FEOPTIONS:
            case FeSt_FEDEFINE_KEYS:
            case FeSt_HIGH_SCORES:
            case FeSt_FELOAD_GAME:
            case FeSt_CAMPAIGN_SELECT:
            case FeSt_MAPPACK_SELECT:
            case FeSt_MP_MAPPACK_SELECT:
            case FeSt_MAIN_MENU:
            case FeSt_LEVEL_STATS:
            case FeSt_NET_SERVICE:
            case FeSt_NET_SESSION:
            case FeSt_NET_START:
                return true;
            default:
                return false;
        }
    }

    // Height to subtract from GetContentRegionAvail().y so a fixed-size
    // window's trailing FeSeparator() + `rows` stacked rows of FeButton()s
    // aren't clipped by the window's bottom edge. Measured from live body-
    // font metrics rather than a flat pixel guess (was `- 60.0f`, which
    // clipped the row once FeButton became text-only and its height started
    // tracking the body font at higher UI_FONT_SCALE). A few px of slack so
    // it errs toward a small gap above the buttons, never a crop.
    double fe_bottom_row_reserve(int64_t rows = 1)
    {
        const ImGuiStyle &style = ImGui::GetStyle();
        FeStylePushFont(FeFont_Body);
        double row_h = ImGui::GetTextLineHeight() + style.FramePadding.y * 2.0;
        FeStylePopFont();
        // per row: the row height + one ItemSpacing.y gap above it; plus the
        // separator (a line + an ItemSpacing.y on each side); plus slack.
        return rows * (row_h + style.ItemSpacing.y)
             + style.ItemSpacing.y * 2.0 + 1.0
             + 6.0;
    }

    // Full-viewport, chrome-less window for the backdrop-plus-text screens
    // (§2.2) -- frontend_copy_background() (still called from frontend.cpp,
    // §3.4) is the visible layer beneath this text.
    void begin_text_overlay()
    {
        ImGuiIO &io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("##FeTextScreen", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    }

    void draw_centered_block(const char *text, FeFontRole role)
    {
        ImGuiIO &io = ImGui::GetIO();
        double margin = io.DisplaySize.x * 0.15;
        double wrap_w = io.DisplaySize.x - margin * 2.0;

        FeStylePushFont(role);
        ImGui::PushTextWrapPos(margin + wrap_w);
        ImVec2 sz = ImGui::CalcTextSize(text, nullptr, false, wrap_w);
        ImGui::SetCursorPos(ImVec2(margin, (io.DisplaySize.y - sz.y) * 0.5));
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        FeStylePopFont();
    }

    void frontgui_story_frame()
    {
        begin_text_overlay();
        draw_centered_block(get_string(frontstory_get_text_no()), FeFont_Subheading);
        ImGui::End();
    }

    void frontgui_birthday_frame()
    {
        const char *name = get_team_birthday();
        if (name == nullptr)
        {
            // Mirrors frontbirthday_draw()'s own fallback (front_easter.c):
            // no birthday today, bounce back to the intro.
            request_frontend_state(FeSt_INTRO);
            return;
        }
        char buf[256];
        std::snprintf(buf, sizeof(buf), "%s\n\n%s", get_string(GUIStr_HappyBirthday), name);

        begin_text_overlay();
        draw_centered_block(buf, FeFont_Heading);
        ImGui::End();
    }

    // Real-time (delta-time based) auto-scroll rather than the legacy
    // fixed-per-tick units_per_pixel advance (§4.3 -- ImGui text uses its
    // own metrics, not bflib_sprfnt's scale model) -- an approximation of
    // "the same rate", not a byte-exact port: framerate-independent pacing
    // is a real improvement over the tick-based original, but the exact
    // px/sec constants below are hand-tuned, not derived from it.
    void frontgui_credits_frame()
    {
        ImGuiIO &io = ImGui::GetIO();
        double speed = 40.0; // px/sec baseline
        if (ImGui::IsKeyDown(ImGuiKey_DownArrow))
            speed *= 5.0; // legacy: holding Down jumps a full line per tick
        else if (ImGui::IsKeyDown(ImGuiKey_UpArrow) && credits_offset <= 0)
            speed = -200.0; // legacy: Up only scrolls back before the end
        credits_offset -= (int64_t)(speed * io.DeltaTime);

        begin_text_overlay();
        ImDrawList *dl = ImGui::GetWindowDrawList();

        double h = (double)credits_offset;
        bool did_draw = h > 0.0;
        for (int64_t i = 0; campaign.credits[i].kind != CIK_None; i++)
        {
            if (h >= io.DisplaySize.y)
                break;
            struct CreditsItem *credit = &campaign.credits[i];
            // Loose best-effort mapping of the legacy 4-slot frontend_font[]
            // roster onto our 4-role type scale -- the two font systems
            // don't correspond exactly, this just keeps the "mixed faces"
            // character of the credits screen (§2.2).
            FeFontRole role = (credit->font < FeFont_COUNT) ? (FeFontRole)credit->font : FeFont_Body;
            FeStylePushFont(role);
            double line_h = ImGui::GetFontSize() + 4.0;
            if (h > -line_h)
            {
                const char *text = (credit->kind == CIK_StringId) ? get_string(credit->num) : credit->str;
                if (text == nullptr)
                    text = "";
                ImVec2 sz = ImGui::CalcTextSize(text);
                dl->AddText(ImVec2((io.DisplaySize.x - sz.x) * 0.5, h), ImGui::GetColorU32(ImGuiCol_Text), text);
                did_draw = true;
            }
            FeStylePopFont();
            h += line_h;
        }
        ImGui::End();

        if (!did_draw)
        {
            // Mirrors frontcredits_draw()'s own end-of-list handling
            // (front_credits.c): credits_end feeds frontend.cpp's
            // front_continue_pressed(credits_end) so the screen still
            // auto-advances once every line has scrolled past.
            credits_end = 1;
            credits_offset = (int64_t)io.DisplaySize.y;
        }
    }

    // Phase G §6.3's generic schema renderer: one FeCheckbox/FeSlider per
    // matching row, gated by is_enabled() (§6.2 finding 4 -- e.g. ALT_INPUT
    // flipping which of Unlock/Lock Cursor applies) and applied immediately
    // via setting_option_apply_bool/_int (live engine effect + persisted to
    // keeperfx.cfg through Phase G step 1's writer). A trailing " *" marks
    // needs-restart rows; draw_setting_options_restart_note() below prints
    // the one-line legend once, only if the visible tab actually has one.
    // Phase E (docs/refactor/gui/05-campaign-progress-and-landview.md §3.5):
    // shared confirmation gate for every SOptT_Action row -- a settings-
    // screen fire-and-forget action is inherently destructive-shaped, so
    // every row of this type confirms before firing rather than each
    // needing its own bespoke modal. Only one row can plausibly have a
    // pending confirmation at a time (a single settings screen, one modal
    // visible at once), hence a single static pointer rather than a set.
    const struct SettingOption *s_pending_action_option = nullptr;

    void draw_pending_action_confirm_modal()
    {
        if (s_pending_action_option == nullptr)
            return;
        FeOpenModal("FeSettingActionConfirm");
        bool open = FeBeginModal("FeSettingActionConfirm");
        if (open)
        {
            FeBodyText(get_string(GUIStr_ConfirmYouSure));
            if (s_pending_action_option->help_stridx != 0)
                FeBodyText(get_string(s_pending_action_option->help_stridx));
            FeSeparator();
            if (FeButton(get_string(GUIStr_ConfirmYes)))
            {
                s_pending_action_option->on_action();
                s_pending_action_option = nullptr;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (FeButton(get_string(GUIStr_ConfirmNo)))
            {
                s_pending_action_option = nullptr;
                ImGui::CloseCurrentPopup();
            }
        }
        FeEndModal(open);
    }

    // docs/refactor/ingame-gui/02-pause-menu-and-options.md §3/§7: the same
    // frontgui_options_frame() is reused for the in-game pause menu. When
    // in-game, a needs-restart option can't take effect until the engine
    // restarts, so it is shown disabled ("change from the main menu")
    // rather than editable -- a blanket rule keyed on apply_class, no new
    // schema field. Set at the top of frontgui_options_frame().
    bool s_options_in_game = false;

    static void draw_setting_options_for_category(enum SettingCategory category)
    {
        for (int64_t i = 0; i < setting_options_count; i++)
        {
            const struct SettingOption *opt = &setting_options[i];
            if (opt->category != category)
                continue;
            const bool restart_blocked_in_game =
                s_options_in_game && (opt->apply_class == SApply_NeedsRestart || opt->frontend_only);
            bool enabled = ((opt->is_enabled == nullptr) || opt->is_enabled()) && !restart_blocked_in_game;
            ImGui::BeginDisabled(!enabled);
            char label[128];
            std::snprintf(label, sizeof(label), "%s%s",
                opt->label_literal ? opt->label_literal : get_string(opt->label_stridx),
                (opt->apply_class == SApply_NeedsRestart) ? " *" : "");
            if (opt->type == SOptT_Bool)
            {
                bool val = opt->get_bool();
                if (FeCheckbox(label, &val))
                    setting_option_apply_bool(opt, val);
            }
            else if (opt->type == SOptT_Enum)
            {
                // FeCombo wants a 0-based index into a plain string array;
                // enum_table entries trade in their own .num values instead
                // (what keeperfx.cfg and get_enum/set_enum use) -- the
                // setting_option_enum_*() helpers (config_settingschema.h)
                // do that translation both ways. A std::vector rather than a
                // small fixed-size array: LANGUAGE's lang_type[] alone has
                // 24 entries, well past the earlier Sound/Input rows'
                // 3-4-entry tables this was first written against.
                int64_t count = setting_option_enum_count(opt);
                int64_t current = setting_option_enum_current_index(opt);
                std::vector<const char *> items(count);
                for (int64_t k = 0; k < count; k++)
                    items[k] = setting_option_enum_item_name(opt, k);
                // Bounded rather than ImGui's own default (a large
                // fraction of the available width) -- found live that
                // left too little room for the label text drawn right
                // after the control, clipping it against the window's
                // own edge.
                ImGui::SetNextItemWidth(220.0);
                if (FeCombo(label, &current, items.data(), count))
                    setting_option_apply_enum_index(opt, current);
            }
            else if (opt->type == SOptT_Int)
            {
                double v = (double)opt->get_int();
                ImGui::SetNextItemWidth(220.0); // see the SOptT_Enum case's own comment
                if (FeSlider(label, &v, (double)opt->int_min, (double)opt->int_max, "%.0f"))
                    setting_option_apply_int(opt, (int64_t)v);
            }
            else // SOptT_Action -- see draw_pending_action_confirm_modal()
            {
                if (FeButton(label))
                    s_pending_action_option = opt;
            }
            if (restart_blocked_in_game)
                FeHelpTooltip(opt->frontend_only
                    ? "Change this from the main menu."
                    : "Change this from the main menu -- it only takes effect after a restart.");
            else if (opt->help_literal)
                FeHelpTooltip(opt->help_literal);
            else if (opt->help_stridx != 0) // 0 isn't "no help text" in get_string()'s own id space -- guard it
                FeHelpTooltip(get_string(opt->help_stridx));
            ImGui::EndDisabled();
        }
    }

    static bool category_has_needs_restart_option(enum SettingCategory category)
    {
        for (int64_t i = 0; i < setting_options_count; i++)
        {
            if ((setting_options[i].category == category) && (setting_options[i].apply_class == SApply_NeedsRestart))
                return true;
        }
        return false;
    }

    static void draw_setting_options_restart_note(enum SettingCategory category)
    {
        if (!category_has_needs_restart_option(category))
            return;
        FeCaption(s_options_in_game
            ? "* only available from the main menu (needs a restart)"
            : "* takes effect after restarting");
    }

    // docs/refactor/ingame-gui/02-pause-menu-and-options.md §3: the legacy
    // in-game video_menu / autopilot_menu sprite sub-menus fold into this
    // window as hand-written rows rather than becoming keeperfx.cfg schema
    // rows -- they send packets (MP-deterministic, kfx_net_state /
    // player-state), which the generic get_int/set_int schema plumbing has
    // no player context for. Only drawn when in_game (they need a live
    // player); shadows + view distance are already real schema rows and
    // show in both contexts. Each reuses the exact legacy click handler
    // (which ignores its GuiButton* arg) so the packet + save_settings()
    // path is identical to the classic menu's.
    static void draw_ingame_video_controls()
    {
        FeSeparator();
        FeSubheading("View");

        const char *view_items[] = { "Isometric", "Isometric (level)", "Front view" };
        int64_t view = settings.video_rotate_mode;
        ImGui::SetNextItemWidth(220.0);
        if (FeCombo("View mode", &view, view_items, 3))
        {
            settings.video_rotate_mode = (unsigned char)view;
            gui_video_rotate_mode(nullptr); // PckA_SwitchView + save_settings()
        }
        FeHelpTooltip(get_string(GUIStr_OptionViewTypeDesc));

        bool full_walls = settings.video_cluedo_mode == 0; // cluedo mode 1 == see-over ("short") walls
        if (FeCheckbox("Full-height walls", &full_walls))
        {
            video_cluedo_mode = full_walls ? 0 : 1;
            gui_video_cluedo_mode(nullptr); // PckA_SetCluedo
        }
        FeHelpTooltip(get_string(GUIStr_OptionWallHeightDesc));

        double gamma = (double)settings.gamma_correction;
        ImGui::SetNextItemWidth(220.0);
        if (FeSlider("Gamma correction", &gamma, 0.0, (double)(GAMMA_LEVELS_COUNT - 1), "%.0f"))
        {
            video_gamma_correction = (unsigned char)gamma;
            set_players_packet_action(get_my_player(), PckA_SetGammaLevel, video_gamma_correction, 0, 0, 0);
        }
        FeHelpTooltip(get_string(GUIStr_OptionGammaCorrectionDesc));
    }

    static void draw_ingame_autopilot_controls()
    {
        // §2.3 decision: collapsed by default, room to grow as the AI does.
        if (!ImGui::CollapsingHeader(get_string(GUIStr_MnuComputerAssist)))
            return;

        struct AssistOpt { const char *label; int64_t kind; TextStringId help; };
        static const AssistOpt opts[] = {
            { "Aggressive",   1, GUIStr_AggressiveAssistDesc },
            { "Defensive",    2, GUIStr_DefensiveAssistDesc },
            { "Construction", 3, GUIStr_ConstructionAssistDesc },
            { "Move only",    4, GUIStr_MoveOnlyAssistDesc },
        };
        int64_t current = kfx_net_state.comp_player_aggressive   ? 1
                    : kfx_net_state.comp_player_defensive    ? 2
                    : kfx_net_state.comp_player_construct    ? 3
                    : kfx_net_state.comp_player_creatrsonly  ? 4 : 0;
        for (const AssistOpt &o : opts)
        {
            // No FeRadio wrapper exists; ImGui::RadioButton direct, same
            // narrow exception the land-preview / SetNextItemWidth calls in
            // this file already take (frontgui_widgets.h header note).
            if (ImGui::RadioButton(o.label, current == o.kind) && current != o.kind)
            {
                // Mirror the classic radio group: exactly one comp_player_*
                // flag set locally, then gui_set_autopilot() reads it and
                // sends PckA_SetComputerKind (the flags are GUI-display
                // state; the packet does the real setup on every client).
                kfx_net_state.comp_player_aggressive  = (o.kind == 1);
                kfx_net_state.comp_player_defensive   = (o.kind == 2);
                kfx_net_state.comp_player_construct   = (o.kind == 3);
                kfx_net_state.comp_player_creatrsonly = (o.kind == 4);
                gui_set_autopilot(nullptr);
            }
            FeHelpTooltip(get_string(o.help));
        }
    }

    void frontgui_options_frame(bool in_game)
    {
        s_options_in_game = in_game;
        ImGuiIO &io = ImGui::GetIO();
        // Fixed size regardless of which tab is active -- found live that
        // ImGuiWindowFlags_AlwaysAutoResize (removed below) made the whole
        // window resize/jump every time the player switched tabs, since
        // Game/Graphics/Sound/Input each have a different number of rows.
        // ImGuiCond_Always re-applies both every frame, so this overrides
        // that per-frame fit-to-content sizing unconditionally rather than
        // just setting an initial size. Width bumped from an initial 0.5
        // to 0.7 -- found live too narrow to fit both a row's own control
        // and its label (labels were getting clipped, e.g. "Display Num"),
        // and too narrow for all four tab headers to fit without ImGui's
        // own tab-bar scroll-arrows/truncation kicking in.
        ImVec2 win_size(io.DisplaySize.x * 0.7, io.DisplaySize.y * 0.8);
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5, io.DisplaySize.y * 0.5), ImGuiCond_Always, ImVec2(0.5, 0.5));
        ImGui::SetNextWindowSize(win_size, ImGuiCond_Always);
        ImGui::Begin("##FeOptions", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);

        FeHeading(get_string(frontend_button_info[FEBtn_MnuOptions].capstr_idx));
        FeSeparator();

        // §6.3's tabs mirror the launcher's own Game/Graphics/Sound/Input
        // grouping (§6.2's decision), so the schema's category maps
        // directly to a tab -- Sound and Input additionally carry the
        // pre-existing binary-GameSettings controls (volumes, mouse
        // sensitivity/invert) that predate this schema and aren't
        // keeperfx.cfg-backed, so they stay hand-written rather than
        // becoming schema rows.
        //
        // Each tab's own row list is wrapped in a fixed-height, scrolling
        // FeBeginScrollArea instead of just letting it flow into the
        // window -- with the window itself now fixed-size (above),
        // whichever tab has the most rows (Game, currently) needs
        // somewhere for the overflow to go rather than being clipped or
        // spilling past the window's own bottom edge. A fixed budget
        // (not GetContentRegionAvail() directly) so the scroll area is
        // the same size on every tab, whether or not *this* tab happens
        // to have a needs-restart row of its own: leaves room below for
        // the restart-note caption, the separator, and the button row.
        double scroll_h = ImGui::GetContentRegionAvail().y - 130.0;
        if (scroll_h < 80.0) scroll_h = 80.0; // floor for a very short display
        bool tabbar_open = FeBeginTabBar("##options_tabs");
        if (tabbar_open)
        {
            // "Game"/"Graphics" are plain literals, not routed through
            // get_string() -- same as the Phase B style-sheet proving
            // ground's own tab labels (frontgui_stylesheet_test.cpp); no
            // existing GUIStr_* fits a short tab caption, and English-first
            // is an accepted interim per §6.3's own decision. Sound/Mouse
            // Options below reuse their pre-existing real captions.
            if (FeTab("Game"))
            {
                FeBeginScrollArea("##game_scroll", ImVec2(0, scroll_h));
                draw_setting_options_for_category(SCat_Game);
                if (in_game)
                    draw_ingame_autopilot_controls();
                FeEndScrollArea();
                draw_setting_options_restart_note(SCat_Game);
                FeEndTab();
            }
            if (FeTab("Graphics"))
            {
                FeBeginScrollArea("##graphics_scroll", ImVec2(0, scroll_h));
                draw_setting_options_for_category(SCat_Graphics);
                if (in_game)
                    draw_ingame_video_controls();
                FeEndScrollArea();
                draw_setting_options_restart_note(SCat_Graphics);
                FeEndTab();
            }
            if (FeTab("GUI"))
            {
                FeBeginScrollArea("##gui_scroll", ImVec2(0, scroll_h));
                draw_setting_options_for_category(SCat_GUI);
                FeEndScrollArea();
                draw_setting_options_restart_note(SCat_GUI);
                FeEndTab();
            }
            if (FeTab(get_string(frontend_button_info[FEBtn_MnuSoundOptions].capstr_idx)))
            {
                FeBeginScrollArea("##sound_scroll", ImVec2(0, scroll_h));
                double v;
                v = (double)sound_volume_ctrl.get_value();
                if (FeSlider("Sound volume", &v, 0.0, 255.0, "%.0f"))
                    sound_volume_ctrl.set_value((int64_t)v);
                v = (double)music_volume_ctrl.get_value();
                if (FeSlider("Music volume", &v, 0.0, 255.0, "%.0f"))
                    music_volume_ctrl.set_value((int64_t)v);
                v = (double)mentor_volume_ctrl.get_value();
                if (FeSlider("Mentor volume", &v, 0.0, 255.0, "%.0f"))
                    mentor_volume_ctrl.set_value((int64_t)v);
                FeSeparator();
                draw_setting_options_for_category(SCat_Sound);
                FeEndScrollArea();
                draw_setting_options_restart_note(SCat_Sound);
                FeEndTab();
            }
            if (FeTab(get_string(frontend_button_info[FEBtn_MouseOptions].capstr_idx)))
            {
                FeBeginScrollArea("##input_scroll", ImVec2(0, scroll_h));
                double v = (double)mouse_sensitivity_ctrl.get_value();
                if (FeSlider(get_string(frontend_button_info[FEBtn_Sensitivity].capstr_idx), &v, 0.0, 7.0, "%.0f"))
                    mouse_sensitivity_ctrl.set_value((int64_t)v);

                bool inverted = mouse_invert_ctrl.get_value() != 0;
                if (FeCheckbox(get_string(frontend_button_info[FEBtn_MnuInvertMouse].capstr_idx), &inverted))
                    mouse_invert_ctrl.toggle_value();
                FeSeparator();
                draw_setting_options_for_category(SCat_Input);
                FeEndScrollArea();
                draw_setting_options_restart_note(SCat_Input);
                FeEndTab();
            }
        }
        FeEndTabBar(tabbar_open);

        FeSeparator();
        // Define Keys: frontend-only for now -- there is no in-game key-rebind
        // path (docs/refactor/ingame-gui/02-pause-menu-and-options.md §7).
        ImGui::BeginDisabled(in_game);
        if (FeButton(get_string(frontend_button_info[FEBtn_DefineKeys_95].capstr_idx)))
            request_frontend_state(FeSt_FEDEFINE_KEYS);
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (in_game)
        {
            // English literal, same as the "Game"/"Graphics" tab captions
            // above -- no GUIStr_* fits "back to the launcher".
            if (FeButton("Back"))
                ingame_options_back_to_launcher(); // collapse to the 4-button launcher
        }
        else
        {
            if (FeButton(get_string(frontend_button_info[FEBtn_MnuReturnToMain].capstr_idx)))
                request_frontend_state(FeSt_MAIN_MENU);
        }

        draw_pending_action_confirm_modal();

        ImGui::End();
        s_options_in_game = false;
    }

    // §6.1: "In ImGui the remap screen is a for loop over
    // num_definable_keys() inside an FeBeginListBox, and the twelve row
    // buttons plus their _maintain/_up/_down/_scroll callbacks all
    // disappear." FeBeginListBox's native scrolling replaces
    // kfx_frontend_state.define_key_scroll_offset entirely -- no manual
    // paging needed.
    void frontgui_definekeys_frame()
    {
        ImGuiIO &io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5, io.DisplaySize.y * 0.5), ImGuiCond_Always, ImVec2(0.5, 0.5));
        ImGui::Begin("##FeDefineKeys", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);

        FeHeading(get_string(frontend_button_info[FEBtn_DefineKeys].capstr_idx));
        FeSeparator();

        // docs/refactor/editor/10-definable-keybindings.md -- editor
        // keybindings get their own tab/table (editor_key_settings[]/
        // settings.editor_kbkeys[]) rather than sharing the ~90-entry
        // gameplay list -- same FeBeginTabBar/FeTab wrapper the settings
        // screen's own Game/Graphics/Sound/Mouse tabs and the editor
        // toolbox's own tool-strip tabs already use.
        bool tabbar_open = FeBeginTabBar("##DefineKeysTabs");
        if (tabbar_open)
        {
            if (FeTab("Game"))
            {
                bool open = FeBeginListBox("##definekeys_list", ImVec2(520, 320));
                if (open)
                {
                    // docs/refactor/editor/10-definable-keybindings.md §2.2
                    // -- loops every key_id and filters by
                    // binding_menu_visibility directly (same shape the
                    // "Editor" tab below already uses), rather than relying
                    // on num_definable_keys()'s own count-of-visible-entries
                    // plus an implicit "they're all a contiguous prefix"
                    // assumption. That assumption held by convention, not
                    // enforcement, and broke it for Gkey_EditorEraseTool
                    // when this doc's first draft tried inserting a new
                    // visible key mid-array -- filtering per-entry removes
                    // the assumption instead of just avoiding tripping it.
                    for (int64_t key_id = 0; key_id < GAME_KEYS_COUNT; key_id++)
                    {
                        if (game_key_settings[key_id].binding_menu_visibility != BMV_Visible)
                            continue;
                        char keytext[96];
                        frontend_format_key_binding(key_id, keytext, sizeof(keytext));
                        char label[192];
                        // docs/refactor/editor/10-definable-keybindings.md
                        // (D6) -- same label_literal-first precedent as
                        // this file's own setting-schema rendering
                        // (frontgui_screens.cpp's options list,
                        // "opt->label_literal ? opt->label_literal :
                        // get_string(opt->label_stridx)") for a binding
                        // that has no slot in the classic localized
                        // GUIStr_* table yet.
                        const struct GamekeySettings *gks = &game_key_settings[key_id];
                        std::snprintf(label, sizeof(label), "%-32s %s", gks->label_literal ? gks->label_literal : get_string(gks->string_id), keytext);
                        if (FeListRow(label, defining_a_key && !defining_editor_key && defining_a_key_id == key_id))
                        {
                            // Mirrors frontend_define_key()
                            // (frontmenu_options.c): define_key_input()
                            // (frontend.cpp's input dispatch) does the
                            // actual capture, unchanged by which draw path
                            // is active.
                            defining_a_key = 1;
                            defining_editor_key = false;
                            defining_a_key_id = key_id;
                            lbInkey = KC_UNASSIGNED;
                        }
                    }
                }
                FeEndListBox(open);
                FeEndTab();
            }
            if (FeTab("Editor"))
            {
                bool open = FeBeginListBox("##definekeys_editor_list", ImVec2(520, 320));
                if (open)
                {
                    for (int64_t key_id = 0; key_id < EDITOR_GAME_KEYS_COUNT; key_id++)
                    {
                        if (editor_key_settings[key_id].binding_menu_visibility != BMV_Visible)
                            continue;
                        char keytext[96];
                        frontend_format_editor_key_binding(key_id, keytext, sizeof(keytext));
                        char label[192];
                        const struct GamekeySettings *gks = &editor_key_settings[key_id];
                        std::snprintf(label, sizeof(label), "%-32s %s", gks->label_literal ? gks->label_literal : get_string(gks->string_id), keytext);
                        if (FeListRow(label, defining_a_key && defining_editor_key && defining_a_key_id == key_id))
                        {
                            defining_a_key = 1;
                            defining_editor_key = true;
                            defining_a_key_id = key_id;
                            lbInkey = KC_UNASSIGNED;
                        }
                    }
                }
                FeEndListBox(open);
                FeEndTab();
            }
        }
        FeEndTabBar(tabbar_open);

        FeSeparator();
        if (FeButton(get_string(frontend_button_info[FEBtn_MnuRetToOptions].capstr_idx)))
            request_frontend_state(FeSt_FEOPTIONS);

        ImGui::End();

        if (defining_a_key)
        {
            FeOpenModal("FeDefineKeyModal");
            bool modal_open = FeBeginModal("FeDefineKeyModal");
            if (modal_open)
                FeBodyText(get_string(GUIStr_PressAKey));
            FeEndModal(modal_open);
        }
    }

    void frontgui_highscores_frame()
    {
        ImGuiIO &io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5, io.DisplaySize.y * 0.5), ImGuiCond_Always, ImVec2(0.5, 0.5));
        ImGui::Begin("##FeHighScores", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);

        FeHeading(get_string(GUIStr_MnuHighScoreTable));
        FeSeparator();

        // Same defensive check frontend_draw_high_score_table() (the legacy
        // draw_call) already makes: hiscore_count and the hiscore_table
        // allocation can transiently disagree (e.g. between a campaign's
        // hiscore_count being set and load_high_score_table()/
        // create_empty_high_score_table() actually populating the
        // pointer) -- count_high_scores() itself doesn't guard against
        // this, so this screen has to.
        uint64_t count = (campaign.hiscore_table != NULL) ? count_high_scores() : 0;

        // SetKeyboardFocusHere() only on the frame editing actually starts
        // -- calling it every frame the row happens to match would keep
        // stealing focus back from the InputText the user is already
        // typing into.
        static int64_t s_last_editing_index = -1;
        bool start_editing = (high_score_entry_input_active >= 0) && (high_score_entry_input_active != s_last_editing_index);
        s_last_editing_index = high_score_entry_input_active;

        bool open = FeBeginListBox("##highscores_list", ImVec2(520, 320));
        if (open)
        {
            for (uint64_t i = 0; i < count && i < campaign.hiscore_count; i++)
            {
                struct HighScore *hs = &campaign.hiscore_table[i];
                if ((int64_t)i == high_score_entry_input_active)
                {
                    // §7 Phase D: "Text entry ... is Latin-only -- ImGui's
                    // SDL3 backend text input is enough" -- ImGui's own
                    // InputText owns high_score_entry directly while this
                    // row is being edited, replacing the legacy manual
                    // UTF-8 cursor/splice handling
                    // (frontend_high_score_table_input(), gated off in
                    // frontend.cpp's input dispatch while this screen is
                    // ImGui-active).
                    ImGui::PushID((int64_t)i);
                    char rank[16];
                    std::snprintf(rank, sizeof(rank), "%2" PRIu64 ".", (uint64_t)(i + 1));
                    ImGui::TextUnformatted(rank);
                    ImGui::SameLine();
                    if (start_editing)
                        ImGui::SetKeyboardFocusHere();
                    FeTextInput("##name_entry", high_score_entry, sizeof(high_score_entry));
                    if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))
                        finalize_high_score_entry(false);
                    else if (ImGui::IsKeyPressed(ImGuiKey_Escape))
                        finalize_high_score_entry(true);
                    ImGui::SameLine();
                    char scoretext[64];
                    std::snprintf(scoretext, sizeof(scoretext), "%" PRId64, (int64_t)hs->score);
                    ImGui::TextUnformatted(scoretext);
                    ImGui::PopID();
                }
                else
                {
                    char label[160];
                    std::snprintf(label, sizeof(label), "%2" PRIu64 ". %-24s %8" PRId64, (uint64_t)(i + 1), hs->name, (int64_t)hs->score);
                    FeListRow(label, false);
                }
            }
        }
        FeEndListBox(open);

        FeSeparator();
        if (FeButton(get_string(frontend_button_info[FEBtn_MnuReturnToMain].capstr_idx)))
        {
            // frontend_quit_high_score_table()'s own body (front_highscore.c),
            // split so only its actual state transition gets deferred --
            // finalize_high_score_entry() has no ImGui-unsafe side effects.
            finalize_high_score_entry(false);
            request_frontend_state(get_menu_state_when_back_from_substate(FeSt_HIGH_SCORES));
        }

        ImGui::End();
    }

    void frontgui_loadgame_frame()
    {
        ImGuiIO &io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5, io.DisplaySize.y * 0.5), ImGuiCond_Always, ImVec2(0.5, 0.5));
        ImGui::Begin("##FeLoadGame", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);

        FeHeading(get_string(frontend_button_info[FEBtn_MnuLoadGame_7].capstr_idx));
        FeSeparator();

        bool open = FeBeginListBox("##loadgame_list", ImVec2(520, 320));
        if (open)
        {
            int64_t catalogue_count = (save_game_catalogue != NULL) ? save_game_catalogue_count : 0;
            for (int64_t i = 0; i < catalogue_count; i++)
            {
                struct CatalogueEntry *centry = &save_game_catalogue[i];
                if ((centry->flags & CEF_InUse) == 0)
                    continue;
                if (FeListRow(centry->textname, false))
                    s_pending_load_slot = i; // load_game() is at least as heavy as frontend_set_state() -- same deferral
            }
        }
        FeEndListBox(open);

        FeSeparator();
        if (FeButton(get_string(frontend_button_info[FEBtn_MnuReturnToMain].capstr_idx)))
            request_frontend_state(FeSt_MAIN_MENU);

        ImGui::End();
    }

    // --- Phase E: master-detail select screens (Campaign select, the
    // merged Free play screen, MP mappack select) ------------------------

    void *s_land_preview_texture = nullptr;
    int64_t s_land_preview_tex_w = 0;
    int64_t s_land_preview_tex_h = 0;
    std::vector<TbPixel> s_land_preview_pixels;

    // Cached from the last draw_land_preview_panel() call, for
    // FrontendImGuiLandPreviewInput() (below) to hit-test against --
    // frontend_input() runs before this frame's own ImGui layout pass
    // computes a fresh rect, so land-preview *input* handling uses last
    // frame's rect instead of this frame's. The layout is static/
    // percentage-of-DisplaySize (see frontgui_campaignselect_frame/
    // frontgui_freeplayselect_frame), so it only actually changes on a
    // window resize -- a one-frame-stale rect is never visibly wrong.
    int64_t s_land_preview_screen_x = 0, s_land_preview_screen_y = 0;
    int64_t s_land_preview_screen_w = 0, s_land_preview_screen_h = 0;

    // Renders the shared land_preview panel (frontmenu_landpreview.h) into
    // an off-screen buffer sized to `size` and composites it via
    // ImGui::Image() -- the same off-screen-render-then-composite
    // technique the ImGui cursor uses (RendererSwapFramebufferTarget,
    // frontgui_style.cpp), scaled up from a single small static sprite to
    // a full interactive panel that's rebuilt every frame (panning, hover
    // animation), not just built once. land_preview_draw() only cares
    // about whatever the "current" render target is -- it already goes
    // through RendererGetFramebuffer()/LbGraphicsScreenWidth()/Height()
    // rather than lbDisplay.WScreen directly (see its own doc comment,
    // written with exactly this seam in mind) -- so it's driven with a
    // 0,0-origin rect matching the off-screen buffer. Input
    // (land_preview_maintain) is deliberately NOT called from here --
    // see FrontendImGuiLandPreviewInput's comment for why it has to run
    // earlier in the frame instead. Calling ImGui:: directly here rather
    // than through a frontgui_widgets.h wrapper is a deliberate, narrow
    // exception: this is domain-specific (it knows about struct
    // LandPreviewPanel), not a generic style-layer primitive -- see that
    // header's own comment on what it does and doesn't cover.
    // Shared by draw_land_preview_panel() and FrontendImGuiLandPreviewInput()
    // below -- found live, "cursor on landview / campaign preview pane is
    // still misaligned" (constant offset, present whenever the pointer is
    // over the panel, not just while dragging, not scaling with zoom): only
    // draw_land_preview_panel() used to override land_preview_frame_extra_scale_den
    // around its land_preview_draw() call, so the frame_inset actually baked
    // into the rendered content (halved) never matched the frame_inset
    // FrontendImGuiLandPreviewInput()/land_preview_maintain() used for its
    // rect and ensign-hit-test math (the global's resting value, 1) --
    // land_preview_maintain() runs earlier in the same frame (frontend_input(),
    // before FrontendImGuiFrame() gets to draw_land_preview_panel()), so it
    // always saw the un-halved inset. A shared constant, used identically at
    // both call sites, keeps that in sync instead of relying on the same
    // magic number being copied correctly to two files.
    static const int64_t kLandPreviewImGuiFrameScaleDen = 2;

    void draw_land_preview_panel(ImVec2 size)
    {
        ImVec2 screen_pos = ImGui::GetCursorScreenPos();
        ImGui::Dummy(size); // reserve layout space only

        int64_t w = (int64_t)size.x;
        int64_t h = (int64_t)size.y;
        s_land_preview_screen_x = (int64_t)screen_pos.x;
        s_land_preview_screen_y = (int64_t)screen_pos.y;
        s_land_preview_screen_w = w;
        s_land_preview_screen_h = h;
        if (w <= 0 || h <= 0 || !land_preview.loaded)
            return;

        if (w != s_land_preview_tex_w || h != s_land_preview_tex_h)
        {
            if (s_land_preview_texture != nullptr)
                RendererDestroyDynamicTexture(s_land_preview_texture);
            s_land_preview_texture = RendererCreateDynamicTexture(w, h);
            s_land_preview_tex_w = w;
            s_land_preview_tex_h = h;
        }
        if (s_land_preview_texture == nullptr)
            return;

        s_land_preview_pixels.assign((size_t)w * (size_t)h, TbPixel{0, 0, 0, 0});
        struct GuiButton draw_gbtn = {};
        draw_gbtn.width = (int64_t)w;
        draw_gbtn.height = (int64_t)h;

        {
            FeOffscreenTarget cap(s_land_preview_pixels.data(), w, h);
            // Found live: even after giving this panel most of the right
            // column (the 0.80/0.16 split above), the ornate corner frame
            // still read as oversized -- see land_preview_set_frame_extra_scale_den's
            // own comment for why the frame doesn't respond to this panel's
            // size on its own. Halved here, reset right after so the legacy
            // (-classicmenu) screen's own call to land_preview_draw() is
            // unaffected.
            land_preview_set_frame_extra_scale_den(kLandPreviewImGuiFrameScaleDen);
            land_preview_draw(&draw_gbtn);
            land_preview_set_frame_extra_scale_den(1);
        }
        RendererUpdateDynamicTexture(s_land_preview_texture, s_land_preview_pixels.data(), w, h);
        ImGui::GetWindowDrawList()->AddImage((ImTextureID)(intptr_t)s_land_preview_texture,
            screen_pos, ImVec2(screen_pos.x + w, screen_pos.y + h));
    }

    // Phase C (docs/refactor/gui/05-campaign-progress-and-landview.md §3.3):
    // Campaign Select's land-view-graphics slider. Not required for
    // -classicmenu (§3.4) -- draw_landview_slider() below is simply not
    // called from the legacy draw path at all, so no runtime gate is
    // needed here beyond the ImGui screen this lives on already being
    // new-menu-only.
    //
    // Recomputed only when the highlighted campaign actually changes
    // (frontend_campaign_select_by_index sets land_selection_highlighted_campaign),
    // not every frame -- campaign.single_levels_count is small but this
    // avoids redoing the unlocked-level scan on every single draw call.
    std::vector<LevelNumber> s_landview_slider_levels;
    int64_t s_landview_slider_index = 0;
    struct GameCampaign *s_landview_slider_campaign = nullptr;

    void rebuild_landview_slider_levels(struct GameCampaign *campgn)
    {
        s_landview_slider_levels.clear();
        s_landview_slider_index = 0;
        s_landview_slider_campaign = campgn;
        if (campgn == nullptr)
            return;
        // Self-sufficient rather than relying on some other code path
        // (e.g. continue_game_available()) having already loaded
        // save/progress.cfg this session -- found live: entering Campaign
        // Select directly (not via Continue) could reach here before
        // anything else ever populated the in-memory table, silently
        // leaving the slider empty. Cheap enough to call unconditionally
        // on every highlight change (not every frame).
        load_campaign_progress_file();
        reconcile_fx1contn_into_progress();
        struct CampaignProgressEntry *progress = get_campaign_progress(campgn->fname, false);
        if (progress == nullptr)
            return; // no progress recorded for this campaign yet -- nothing unlocked to browse
        for (uint64_t i = 0; i < campgn->single_levels_count; i++)
        {
            LevelNumber lvnum = campgn->single_levels[i];
            // Unlocked (completed) levels, plus the one immediately next
            // -- found live: the player wants to preview what's coming up
            // too, not just what's already been cleared. Not a spoiler
            // the way a level further ahead would be: it's the level
            // they're about to play next, same as Land View's own
            // "next" ensign already shows.
            bool is_next = (lvnum == (LevelNumber)progress->intralvl.next_level);
            if (!campaign_progress_has_unlocked_level(progress, lvnum) && !is_next)
                continue;
            struct LevelInformation *lvinfo = get_level_info(lvnum);
            // §3.3: only levels that define their own LAND_VIEW art are
            // worth a slider stop -- one with none has nothing distinct
            // to show over whatever's already displayed.
            if ((lvinfo == nullptr) || (lvinfo->land_view[0] == '\0'))
                continue;
            s_landview_slider_levels.push_back(lvnum);
        }
        // Default to the most recently unlocked level with its own art
        // (last in campaign order) -- §3.3's own specified default --
        // and actually load it, not just record the index: found live,
        // the preview kept showing frontend_campaign_select_by_index()'s
        // own initial land_view_start load until the slider was dragged
        // at least once.
        if (!s_landview_slider_levels.empty())
        {
            s_landview_slider_index = (int64_t)s_landview_slider_levels.size() - 1;
            land_preview_load(&land_preview, s_landview_slider_levels[s_landview_slider_index], true);
        }
    }

    void draw_landview_slider(struct GameCampaign *campgn)
    {
        if (campgn != s_landview_slider_campaign)
            rebuild_landview_slider_levels(campgn);
        if (s_landview_slider_levels.size() < 2)
            return; // nothing to scroll through yet (0 or 1 stops)

        // No label here at all, on either side -- found live that a
        // separate name shown beside the slider read as a second,
        // seemingly-different answer to "what am I looking at" next to
        // draw_select_detail_panel()'s own title, even though they were
        // never actually the same value (the panel showed the campaign's
        // name/description, not the slider's level, unless an ensign was
        // separately hovered). Fixed at the source instead of by
        // relabeling: draw_select_detail_panel() now falls back to the
        // slider's own current level when nothing is ensign-hovered, so
        // there's exactly one place this name shows.
        int64_t count = (int64_t)s_landview_slider_levels.size();
        double index_f = (double)s_landview_slider_index;
        // Smaller than ImGui's own full-column default width -- found
        // live to look oversized otherwise.
        ImGui::SetNextItemWidth(160.0);
        if (FeSlider("##landview_slider", &index_f, 0.0, (double)(count - 1), "%.0f"))
        {
            int64_t new_index = (int64_t)(index_f + 0.5);
            if ((new_index >= 0) && (new_index < count) && (new_index != s_landview_slider_index))
            {
                s_landview_slider_index = new_index;
                // Retain the viewport (pan position + zoom) across a
                // slider move -- land_preview_load() itself always resets
                // both to defaults, which is right for its other callers
                // (a fresh campaign/level highlight) but not for a slider
                // drag: the player is browsing art at whatever pan/zoom
                // they already set, not starting a new view each step.
                // Re-clamped afterward since a different level's art can
                // be a different size, so the retained shift might now be
                // out of bounds for it.
                int64_t saved_shift_x = land_preview.screen_shift_x;
                int64_t saved_shift_y = land_preview.screen_shift_y;
                int64_t saved_units_per_px = land_preview.units_per_px;
                // Browse-only: does not change the active campaign or
                // select a level to play, per §3.3 -- committing still
                // goes through the existing highlight/Enter Land flow.
                // show_ensigns=true: found live that false hides every
                // ensign outright (LandPreviewPanel.show_ensigns gates
                // land_preview_draw()'s ensign pass entirely, not just
                // their position) -- these per-level images are all
                // variants of the same campaign map the ensign
                // coordinates were authored against, so keeping them
                // visible (and correctly placed) across every slider
                // position is the intended behaviour, not just the
                // shared overview image.
                land_preview_load(&land_preview, s_landview_slider_levels[s_landview_slider_index], true);
                land_preview.screen_shift_x = saved_shift_x;
                land_preview.screen_shift_y = saved_shift_y;
                land_preview.units_per_px = saved_units_per_px;
                land_preview_clamp_shift(&land_preview, s_land_preview_screen_w, s_land_preview_screen_h);
            }
        }
    }

    // Detail panel content: the highlighted level's name+description if an
    // ensign/level is highlighted, otherwise the highlighted campaign's
    // own -- same fallback frontend_draw_land_selection_detail/
    // frontend_draw_freeplay_detail already use (frontmenu_select.c),
    // reimplemented against FeSubheading/FeBodyText instead of their
    // LbTextDrawResized calls. campaign_fallback is NULL for Free play,
    // which has no campaign-level fallback (frontend_draw_freeplay_detail's
    // own comment: it always has a specific level highlighted, or none).
    void draw_select_detail_panel(struct GameCampaign *campaign_fallback, double height)
    {
        const char *name = nullptr;
        const char *description = nullptr;
        if (land_preview.highlighted_lvnum != SINGLEPLAYER_NOTSTARTED)
        {
            struct LevelInformation *lvinfo = get_level_info(land_preview.highlighted_lvnum);
            if (lvinfo != nullptr)
            {
                name = (lvinfo->name_stridx > 0) ? get_string(lvinfo->name_stridx) : lvinfo->name;
                description = lvinfo->description;
            }
        }
        // Falls back to whichever level the Campaign Select land-view
        // slider currently has selected (s_landview_slider_levels stays
        // empty everywhere else, so this is a no-op off that screen) --
        // the slider itself carries no label of its own (draw_landview_slider()'s
        // own comment), so this is the one place its current level's name
        // shows while nothing is separately ensign-hovered.
        if ((name == nullptr) && !s_landview_slider_levels.empty())
        {
            struct LevelInformation *lvinfo = get_level_info(s_landview_slider_levels[s_landview_slider_index]);
            if (lvinfo != nullptr)
            {
                name = (lvinfo->name_stridx > 0) ? get_string(lvinfo->name_stridx) : lvinfo->name;
                description = lvinfo->description;
            }
        }
        if ((name == nullptr) && (campaign_fallback != nullptr))
        {
            name = campaign_fallback->display_name;
            description = campaign_fallback->description;
        }

        // Gated on name, not description: found live that gating on
        // description alone hid the name too the moment a level had none
        // in its own .cfg -- the panel is now the *only* place that name
        // shows at all (the slider itself carries no label, per §14), so
        // losing it here left no indication of what was selected. A
        // missing description alone still just skips that one line
        // below, same as before -- only a genuinely empty panel (no name
        // either) hides outright.
        if (name == nullptr)
            return;

        // scrollable=true: found live ("using the mouse wheel on campaign/
        // scenario/skirmish menus causes screen to scroll") -- a level's
        // description can run longer than this fixed-height box, and
        // without its own scrollbar/wheel capture that overflow either
        // clipped invisibly or (worse) let the wheel event fall through to
        // the window behind it. This is the box's own "fixed line count,
        // scroll for the rest" -- the text equivalent of a listbox's fixed
        // item count with its own scrollbar.
        if (FeBeginPanel("", ImVec2(0, height), true))
        {
            FeSubheading(name);
            if ((description != nullptr) && (description[0] != '\0'))
                FeBodyText(description);
        }
        FeEndPanel();
    }

    void frontgui_campaignselect_frame()
    {
        ImGuiIO &io = ImGui::GetIO();
        ImVec2 win_size(io.DisplaySize.x * 0.82, io.DisplaySize.y * 0.82);
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5, io.DisplaySize.y * 0.5), ImGuiCond_Always, ImVec2(0.5, 0.5));
        ImGui::SetNextWindowSize(win_size, ImGuiCond_Always);
        // NoScrollbar|NoScrollWithMouse: a hard structural guarantee, not
        // just careful budget math -- found live, twice, that getting the
        // height budget below exactly right is fragile (two rounds of
        // "still scrolls" after two rounds of arithmetic fixes, most
        // recently an off-by-one-ItemSpacing this same column's own
        // comments below describe). Whatever residual pixel or two of
        // overflow the budget still leaves (font metrics, DPI, theme,
        // anything not accounted for), the window itself can now never be
        // the thing that scrolls -- wheel input only ever reaches an
        // actual scrollable child (the campaign list box, the detail
        // panel) via ImGui's normal per-window handling, exactly like
        // FeBeginPanel()'s own non-scrolling variant already relies on for
        // the same reason. The budget math stays, for a correctly laid out
        // screen with nothing invisibly clipped; this is the backstop for
        // whenever it's still off by a little.
        ImGui::Begin("##FeCampaignSelect", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings
            | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        FeHeading(get_string(frontend_button_info[FEBtn_MnuLandSelection].capstr_idx));
        FeSeparator();

        double list_w = win_size.x * 0.3;
        double content_h = ImGui::GetContentRegionAvail().y - fe_bottom_row_reserve(); // leave room for the bottom button row

        ImGui::BeginGroup();
        FeCaption(get_string(frontend_button_info[FEBtn_MnuCampaigns].capstr_idx));
        bool open = FeBeginListBox("##campaign_list", ImVec2(list_w, content_h));
        if (open)
        {
            for (int64_t i = 0; i < (int64_t)campaigns_list.items_num; i++)
            {
                struct GameCampaign *campgn = &campaigns_list.items[i];
                bool selected = (campgn == land_selection_highlighted_campaign);
                if (FeListRow(campgn->display_name, selected))
                    frontend_campaign_select_by_index(i);
            }
        }
        FeEndListBox(open);
        ImGui::EndGroup();

        ImGui::SameLine();
        ImGui::BeginGroup();
        // Preview gets the large majority of the right column -- found
        // live: at content_h*0.62 the fixed-size ornate frame decorations
        // (land_preview_draw_ornate_frame, scaled off the real display's
        // resolution via scale_ui_value_lofi, independent of this panel's
        // own size) ate a large fraction of a panel that modest, crowding
        // out the land art and its ensigns. Detail text is short (a
        // name + a few lines of description) and doesn't need much room.
        //
        // draw_landview_slider()'s own row wasn't budgeted for here at all
        // -- found live, "using the mouse wheel on campaign/scenario/
        // skirmish menus causes screen to scroll": its unaccounted height
        // pushed this column's real content past content_h, and since the
        // window itself carries no scroll flags (same fixed-window
        // approach frontgui_feoptions_frame() uses), ImGui didn't clip the
        // overflow -- it just grew a scrollbar for the *whole menu*, so any
        // wheel-scroll over the screen scrolled the entire window instead
        // of whichever list/panel the pointer was actually over. Reserved
        // unconditionally, whether or not the slider actually renders this
        // frame (fewer than 2 unlocked levels hides it, draw_landview_slider's
        // own early-out) -- a fixed layout that doesn't jump depending on
        // progress beats reclaiming that space when unused.
        //
        // First pass at this reservation used GetFrameHeightWithSpacing()
        // alone and still overflowed by one ItemSpacing.y -- found live,
        // the screen still scrolled after that fix landed. That call only
        // bundles the *trailing* gap after the slider (ImGui's own
        // convention: item height + one ItemSpacing.y); it doesn't cover
        // the *leading* gap ImGui inserts between the land preview panel
        // above and the slider below -- a genuinely separate spacing, since
        // the layout here is three stacked items (panel, slider, panel),
        // not two. Explicit about both gaps below instead of relying on a
        // "WithSpacing" helper's built-in assumption of how many there are.
        // Still measured against FeSlider's own font/metrics (FeFont_Body)
        // rather than a flat pixel guess, so it holds at any UI_FONT_SCALE.
        FeStylePushFont(FeFont_Body);
        double slider_h = ImGui::GetFrameHeight();
        FeStylePopFont();
        double spacing_y = ImGui::GetStyle().ItemSpacing.y;
        double split_h = content_h - slider_h - 2.0 * spacing_y;
        draw_land_preview_panel(ImVec2(ImGui::GetContentRegionAvail().x, split_h * 0.79));
        draw_landview_slider(land_selection_highlighted_campaign);
        draw_select_detail_panel(land_selection_highlighted_campaign, split_h * 0.21);
        ImGui::EndGroup();

        FeSeparator();
        // Return on the left, Enter/Play on the right -- matches the
        // legacy screen's own button order (frontend_land_selection_return_to_main_maintain/
        // frontend_land_selection_enter_maintain, frontmenu_select.c), found
        // live to be flipped here.
        if (FeButton(get_string(frontend_button_info[FEBtn_MnuReturnToMain].capstr_idx)))
            request_frontend_state(FeSt_MAIN_MENU);
        ImGui::SameLine();
        if (FeButton(get_string(frontend_button_info[FEBtn_MnuEnterLand].capstr_idx)))
        {
            int64_t next_state = frontend_land_selection_enter_resolve();
            if (next_state >= 0)
                request_frontend_state((FrontendMenuState)next_state);
        }
        // Explicit breathing room below the button row -- found live,
        // "there needs to be a slight gap between the return/enter buttons
        // and the bottom of the pane. thats been lost in this pass": the
        // content_h budget above was tuned to just fit above it, leaving
        // ~0 slack against the window's own WindowPadding before this
        // screen's wheel-scroll fixes landed (4fdce2e/32bff97/4d4bca1) --
        // harmless before then since the window could still scroll a
        // couple of pixels to compensate, but now that it deliberately
        // can't (NoScrollWithMouse), that tightness reads as no gap at
        // all. A trailing Dummy is simpler and safer than tightening the
        // budget further: it only ever adds blank space here, at the very
        // end of this window's content, so it can't affect anything drawn
        // above it even if the window's fixed height is razor-tight.
        ImGui::Dummy(ImVec2(0.0, ImGui::GetStyle().WindowPadding.y));

        ImGui::End();
    }

    // Merged Free play screen: mappack list + level list stacked in the
    // left column, land preview + detail + commit button on the right --
    // same structure as Campaign select, just two lists sharing the left
    // column's height budget instead of one (mirrors frontmenu_select_data.cpp's
    // legacy split, FE_FREEPLAY_MAPPACK_ROW_Y0/_LEVEL_ROW_Y0).
    void frontgui_freeplayselect_frame()
    {
        ImGuiIO &io = ImGui::GetIO();
        ImVec2 win_size(io.DisplaySize.x * 0.82, io.DisplaySize.y * 0.82);
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5, io.DisplaySize.y * 0.5), ImGuiCond_Always, ImVec2(0.5, 0.5));
        ImGui::SetNextWindowSize(win_size, ImGuiCond_Always);
        // NoScrollbar|NoScrollWithMouse: see frontgui_campaignselect_frame()'s
        // own comment on this same flag pair -- same screen family, same
        // hard structural guarantee instead of relying solely on the
        // height budget below being exactly right.
        ImGui::Begin("##FeFreePlaySelect", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings
            | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        // Shared with Skirmish (frontend_start_skirmish_resolve(),
        // frontend.cpp) -- same merged screen, sourced from the
        // multiplayer mappack list instead when entered that way. See
        // frontend_freeplay_is_skirmish()'s own comment (frontmenu_select.h).
        bool is_skirmish = frontend_freeplay_is_skirmish();
        FeHeading(is_skirmish ? get_string(GUIStr_NetServiceSkirmish)
            : get_string(frontend_button_info[FEBtn_MnuFreePlayLevels_107].capstr_idx));
        FeSeparator();

        double list_w = win_size.x * 0.3;
        double content_h = ImGui::GetContentRegionAvail().y - fe_bottom_row_reserve();
        // Both FeCaption lines above the two list boxes need reserving
        // here, not just the second one -- found live alongside the
        // campaign-select slider-row bug (same symptom, same root cause:
        // "using the mouse wheel on campaign/scenario/skirmish menus
        // causes screen to scroll"): the first caption's own height was
        // never subtracted from content_h at all, silently overflowing
        // this column by about one caption line every time, which (this
        // window carries no scroll flags either, matching
        // frontgui_feoptions_frame()'s approach) turned into a whole-window
        // scrollbar instead of a clip.
        //
        // First pass at this reservation used GetTextLineHeightWithSpacing()
        // for each caption and still overflowed by one ItemSpacing.y --
        // found live, the screen still scrolled after that fix landed.
        // The stack here is four items (caption, list, caption, list), so
        // there are three gaps between them, not two -- each "WithSpacing"
        // call only bundles the gap that follows *that one* item, so using
        // it twice (once per caption) only ever accounts for 2 of the 3.
        // Explicit about the line heights and every gap separately instead
        // of relying on how many a convenience helper happens to bundle.
        // Still measured against FeCaption's own font/metrics rather than
        // a flat guess, so it holds at any UI_FONT_SCALE.
        FeStylePushFont(FeFont_Caption);
        double caption_line_h = ImGui::GetTextLineHeight();
        FeStylePopFont();
        double spacing_y = ImGui::GetStyle().ItemSpacing.y;
        double lists_h = content_h - 2.0 * caption_line_h - 3.0 * spacing_y;
        double mappack_list_h = lists_h * 0.35;
        double level_list_h = lists_h - mappack_list_h;

        struct CampaignsList *active_mappacks_list = frontend_freeplay_active_mappacks_list();

        ImGui::BeginGroup();
        FeCaption(get_string(frontend_button_info[FEBtn_MnuMapPacks].capstr_idx));
        bool mappack_open = FeBeginListBox("##mappack_list", ImVec2(list_w, mappack_list_h));
        if (mappack_open)
        {
            for (int64_t i = 0; i < (int64_t)active_mappacks_list->items_num; i++)
            {
                struct GameCampaign *campgn = &active_mappacks_list->items[i];
                bool selected = (campgn == freeplay_highlighted_mappack);
                if (FeListRow(campgn->display_name, selected))
                    frontend_mappack_select_by_index(i);
            }
        }
        FeEndListBox(mappack_open);

        FeCaption(get_string(frontend_button_info[FEBtn_MnuLevels].capstr_idx));
        bool level_open = FeBeginListBox("##freeplay_level_list", ImVec2(list_w, level_list_h));
        if (level_open)
        {
            uint64_t levels_count;
            LevelNumber *levels = frontend_freeplay_active_levels(&levels_count);
            for (int64_t i = 0; i < (int64_t)levels_count; i++)
            {
                LevelNumber lvnum = levels[i];
                struct LevelInformation *lvinfo = get_level_info(lvnum);
                if (lvinfo == nullptr)
                    continue;
                const char *name = (lvinfo->name_stridx > 0) ? get_string(lvinfo->name_stridx) : lvinfo->name;
                bool selected = (lvnum == freeplay_highlighted_level);
                if (FeListRow(name, selected))
                    frontend_level_select_by_index(i);
            }
        }
        FeEndListBox(level_open);
        ImGui::EndGroup();

        ImGui::SameLine();
        ImGui::BeginGroup();
        if (is_skirmish)
        {
            // docs/refactor/skirmish/: Skirmish gets a tab bar -- the usual map
            // preview/description, and a Setup tab (General / Availability /
            // Win-Lose / Slots & AI) that customises the level's script setup.
            // Free play keeps the plain layout below.
            skirmish_setup_sync(freeplay_highlighted_level, default_loc_player);
            const bool tabs_open = FeBeginTabBar("##SkirmishTabs");
            const double tab_h = ImGui::GetFrameHeightWithSpacing();
            const double body_h = content_h - tab_h;
            if (tabs_open)
            {
                if (FeTab("Map"))
                {
                    draw_land_preview_panel(ImVec2(ImGui::GetContentRegionAvail().x, body_h * 0.75));
                    draw_select_detail_panel(nullptr, body_h * 0.20);
                    FeEndTab();
                }
                const std::string setup_label = std::string(skirmish_setup_is_changed() ? "Setup *" : "Setup") + "###SkirmishSetupTab";
                if (FeTab(setup_label.c_str()))
                {
                    frontgui_skirmish_setup_draw(body_h);
                    FeEndTab();
                }
            }
            FeEndTabBar(tabs_open);
        }
        else
        {
            // See frontgui_campaignselect_frame's own comment on the 0.75/0.20
            // split -- same fixed-size-frame-decoration issue, same fix.
            draw_land_preview_panel(ImVec2(ImGui::GetContentRegionAvail().x, content_h * 0.75));
            draw_select_detail_panel(nullptr, content_h * 0.20); // no campaign-level fallback -- see draw_select_detail_panel's comment
        }
        ImGui::EndGroup();

        FeSeparator();
        // Return on the left, Play on the right -- see
        // frontgui_campaignselect_frame's own comment on this order.
        if (FeButton(get_string(frontend_button_info[FEBtn_MnuReturnToMain].capstr_idx)))
        {
            if (is_skirmish)
                skirmish_setup_forget();
            request_frontend_state(FeSt_MAIN_MENU);
        }
        ImGui::SameLine();
        const bool play_blocked = is_skirmish && skirmish_setup_play_blocked(freeplay_highlighted_level);
        ImGui::BeginDisabled(play_blocked);
        if (FeButton(get_string(frontend_button_info[FEBtn_MnuPlayLevel].capstr_idx)))
        {
            int64_t next_state = frontend_freeplay_enter_resolve();
            if (next_state >= 0)
                request_frontend_state((FrontendMenuState)next_state);
        }
        ImGui::EndDisabled();
        if (play_blocked)
        {
            ImGui::SameLine();
            FeCaption(frontgui_skirmish_setup_status());
        }
        // Explicit breathing room below the button row -- see
        // frontgui_campaignselect_frame's own comment on this same fix.
        ImGui::Dummy(ImVec2(0.0, ImGui::GetStyle().WindowPadding.y));

        ImGui::End();
    }

    // MP mappack select: a plain list, immediate-commit on click -- no
    // highlight/preview split, matching the legacy screen exactly (it
    // never had one; see frontend_mp_mappack_select_resolve's comment).
    void frontgui_mpmappackselect_frame()
    {
        ImGuiIO &io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5, io.DisplaySize.y * 0.5), ImGuiCond_Always, ImVec2(0.5, 0.5));
        ImGui::Begin("##FeMpMappackSelect", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);

        FeHeading(get_string(frontend_button_info[FEBtn_MnuMpMapPacks].capstr_idx));
        FeSeparator();

        bool open = FeBeginListBox("##mp_mappack_list", ImVec2(520, 320));
        if (open)
        {
            for (int64_t i = 0; i < (int64_t)mp_mappacks_list.items_num; i++)
            {
                struct GameCampaign *campgn = &mp_mappacks_list.items[i];
                if (FeListRow(campgn->display_name, false))
                {
                    int64_t next_state = frontend_mp_mappack_select_resolve(i);
                    if (next_state >= 0)
                        request_frontend_state((FrontendMenuState)next_state);
                }
            }
        }
        FeEndListBox(open);

        FeSeparator();
        if (FeButton(get_string(frontend_button_info[FEBtn_MnuReturnToLobby].capstr_idx)))
            request_frontend_state((FrontendMenuState)frontend_back_from_mp_mappack_list_target());

        ImGui::End();
    }

    // --- Phase F: Main Menu, Level Stats, network flow, error overlay ---

    // docs/refactor/editor/01-entry-and-editor-session.md §1 -- "Tools"
    // opens a small modal with one entry (Editor) plus Back, rather than
    // jumping straight there, so future tools (map-pack manager, packet-
    // demo browser, ...) have a home in the same modal later. Same
    // FeOpenModal/FeBeginModal/FeEndModal pattern as
    // draw_pending_action_confirm_modal() above -- a static "should this be
    // open" flag, (re)opened every frame it's true, closed via
    // ImGui::CloseCurrentPopup() on either button.
    bool s_tools_modal_open = false;

    void draw_tools_modal()
    {
        if (!s_tools_modal_open)
            return;
        FeOpenModal("FeToolsModal");
        bool open = FeBeginModal("FeToolsModal");
        if (open)
        {
            FeHeading("Tools");
            FeSeparator();
            const ImVec2 btn_size(220, 0);
            FeCenterNextItem(btn_size.x);
            // docs/refactor/editor/phase3/02-slice3-dialogs-menubar.md --
            // used to route through FeSt_EDITOR's New/Open browser; that
            // screen is retired now that New/Open/Save live inside the
            // editor's own File menu (editor_dialogs.cpp), so this jumps
            // straight into a blank New Map with the same defaults the old
            // browser's own "New Map" button used. File > Open (in-session)
            // covers picking an existing level instead.
            if (FeButton("Editor", btn_size))
            {
                s_tools_modal_open = false;
                ImGui::CloseCurrentPopup();
                editor_pending_lvnum = EDITOR_SCRATCH_LEVEL_NUMBER;
                editor_pending_is_new = true;
                editor_pending_new_map_w = 85;
                editor_pending_new_map_h = 85;
                editor_pending_new_map_texture = 0;
                request_frontend_state(FeSt_START_EDITOR);
            }
            FeCenterNextItem(btn_size.x);
            if (FeButton("Back", btn_size))
            {
                s_tools_modal_open = false;
                ImGui::CloseCurrentPopup();
            }
        }
        FeEndModal(open);
    }

    void frontgui_mainmenu_frame()
    {
        ImGuiIO &io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5, io.DisplaySize.y * 0.5), ImGuiCond_Always, ImVec2(0.5, 0.5));
        ImGui::Begin("##FeMainMenu", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);

        // The window is AlwaysAutoResize, so its width tracks whichever row
        // is currently widest -- and at larger UI_FONT_SCALE that's the
        // bottom row of auto-sized buttons (their text grows with the font;
        // the heading and the ImVec2(260,0) buttons above don't scale their
        // own width at all). Left as plain sequential draws, every one of
        // those fixed/independently-sized items just stacks flush against
        // the window's left edge once the window grows wider than they are
        // ("menu items don't stay centered" when changing UI_FONT_SCALE).
        // FeCenterNextItem() re-centers each item, every frame, against
        // whatever the window's current width actually is -- so the whole
        // stack tracks the auto-resize instead of drifting from it.
        const char *heading_text = get_string(frontend_button_info[FEBtn_MnuMainMenu].capstr_idx);
        FeStylePushFont(FeFont_Heading);
        double heading_w = ImGui::CalcTextSize(heading_text).x;
        FeStylePopFont();
        FeCenterNextItem(heading_w);
        FeHeading(heading_text);
        FeSeparator();

        const ImVec2 btn_size(260, 0);
        FeCenterNextItem(btn_size.x);
        if (FeButton(get_string(frontend_button_info[FEBtn_MnuStartNewGame].capstr_idx), btn_size))
        {
            int64_t next_state = frontend_start_new_game_resolve();
            if (next_state >= 0)
                request_frontend_state((FrontendMenuState)next_state);
        }

        ImGui::BeginDisabled(mappacks_list.items_num <= 0);
        FeCenterNextItem(btn_size.x);
        if (FeButton(get_string(frontend_button_info[FEBtn_MnuFreePlayLevels].capstr_idx), btn_size))
        {
            // frontend_load_mappacks's own body, minus its unsafe-here
            // frontend_set_state() call -- the reset matters: without it,
            // a prior Skirmish visit this session would leak into this
            // normal Free play entry (see its own comment, frontend.cpp).
            net_service_index_selected = FrontendNetSvc_Online;
            request_frontend_state(FeSt_MAPPACK_SELECT);
        }
        ImGui::EndDisabled();

        ImGui::BeginDisabled(mp_mappacks_list.items_num <= 0);
        FeCenterNextItem(btn_size.x);
        if (FeButton(get_string(frontend_button_info[FEBtn_MnuSkirmish].capstr_idx), btn_size))
        {
            int64_t next_state = frontend_start_skirmish_resolve();
            if (next_state >= 0)
                request_frontend_state((FrontendMenuState)next_state);
        }
        ImGui::EndDisabled();

        // Continue Game is removed entirely from the new menu (not just
        // repointed) -- docs/refactor/gui/05-campaign-progress-and-landview.md
        // §3.4/§10: under the new menu it's a plain duplicate of the
        // "Campaign" button above (both resolve to FeSt_CAMPAIGN_SELECT
        // with no pre-arranged state), so keeping a second entry for the
        // same destination is redundant. `-classicmenu`'s own main menu
        // (frontend_main_menu_buttons[], frontend.cpp) keeps its Continue
        // Game button completely unchanged -- this only touches the ImGui
        // draw path.

        ImGui::BeginDisabled(number_of_saved_games <= 0);
        FeCenterNextItem(btn_size.x);
        if (FeButton(get_string(frontend_button_info[FEBtn_MnuLoadGame].capstr_idx), btn_size))
            request_frontend_state(FeSt_FELOAD_GAME);
        ImGui::EndDisabled();

        FeCenterNextItem(btn_size.x);
        if (FeButton(get_string(frontend_button_info[FEBtn_MnuMultiplayer].capstr_idx), btn_size))
        {
            int64_t next_state = frontend_netservice_change_state_resolve();
            if (next_state >= 0)
                request_frontend_state((FrontendMenuState)next_state);
        }

        // docs/refactor/editor/01-entry-and-editor-session.md §1 -- opens
        // draw_tools_modal() (Editor / Back) rather than jumping straight
        // to the editor, so future tools have a home in the same modal.
        // Not routed through frontend_button_info[]/GUIStr_* like the
        // buttons above -- these are net-new captions with no translation
        // entry yet (they'd render as "untranslated <N>" until a lang
        // pipeline regen), and this is a dev-tool entry point, not
        // player-facing campaign content. Follow-up if/when the editor
        // ships to players.
        FeCenterNextItem(btn_size.x);
        if (FeButton("Tools", btn_size))
            s_tools_modal_open = true;

        FeSeparator();

        // Bottom row's three buttons are auto-sized (their width tracks the
        // font), so unlike the fixed-width buttons above, the row's own
        // total width has to be measured before it's drawn to center it
        // as a block -- FeCenterNextItem only nudges the first item's
        // cursor, SameLine() keeps the rest flush after it.
        const char *opt_label = get_string(frontend_button_info[FEBtn_MnuOptions_97].capstr_idx);
        const char *scores_label = get_string(frontend_button_info[FEBtn_MnuHighScoreTable_104].capstr_idx);
        const char *quit_label = get_string(frontend_button_info[FEBtn_MnuQuit].capstr_idx);
        FeStylePushFont(FeFont_Body);
        const ImGuiStyle &style = ImGui::GetStyle();
        double row_w = ImGui::CalcTextSize(opt_label).x + style.FramePadding.x * 2.0
            + ImGui::CalcTextSize(scores_label).x + style.FramePadding.x * 2.0
            + ImGui::CalcTextSize(quit_label).x + style.FramePadding.x * 2.0
            + style.ItemSpacing.x * 2.0;
        FeStylePopFont();
        FeCenterNextItem(row_w);
        if (FeButton(opt_label))
            request_frontend_state(FeSt_FEOPTIONS);
        ImGui::SameLine();
        if (FeButton(scores_label))
        {
            int64_t next_state = frontend_ldcampaign_change_state_resolve();
            if (next_state >= 0)
                request_frontend_state((FrontendMenuState)next_state);
        }
        ImGui::SameLine();
        if (FeButton(quit_label))
            request_frontend_state(FeSt_QUIT_GAME);

        draw_tools_modal();

        ImGui::End();
    }

    // docs/refactor/editor/phase3/02-slice3-dialogs-menubar.md -- the
    // pre-session New/Open browser this comment used to describe
    // (frontgui_editorbrowser_frame, FeSt_EDITOR) is retired: New/Open/Save
    // now live inside the editor's own File menu (kfx_editor/editor_dialogs.cpp),
    // reached once a session is already running. Tools -> Editor (above)
    // jumps straight into a blank New Map instead of showing a picker first.

    // Renders one "name ......... value" row -- shared by both stat blocks
    // (the always-visible main_stats_data and the scrollable
    // scrolling_stats_data). Mirrors frontstats_draw_main_stats/
    // _draw_scrolling_stats' own GUIStr_Time special case (front_lvlstats.c):
    // when the level's own turn-based timer is off and the wall-clock timer
    // is on, show the wall-clock HH:MM:SS:MS breakdown instead of the raw
    // stat value.
    void draw_stat_row(const struct StatsData *stat)
    {
        ImGui::TextUnformatted(get_string(stat->name_stridx));
        ImGui::SameLine(240.0);
        char valbuf[64];
        if (timer_enabled() && (stat->name_stridx == GUIStr_Time) && !kfx_sim_state.TimerGame)
        {
            std::snprintf(valbuf, sizeof(valbuf), "%02" PRId64 ":%02" PRId64 ":%02" PRId64 ":%03" PRId64,
                (int64_t)(kfx_sim_state.Timer.Hours), (int64_t)(kfx_sim_state.Timer.Minutes),
                (int64_t)(kfx_sim_state.Timer.Seconds), (int64_t)(kfx_sim_state.Timer.MSeconds));
        }
        else
        {
            int64_t val = (stat->get_value != nullptr) ? stat->get_value(stat->get_arg) : -1;
            std::snprintf(valbuf, sizeof(valbuf), "%" PRId64, (int64_t)(val));
        }
        ImGui::TextUnformatted(valbuf);
    }

    void run_pending_stats_leave(void)
    {
        // init_menu_state_on_net_stats_exit() (front_lvlstats.c) has a
        // conditional fallback (try to stay in net service, else fall back
        // further) baked around its own frontend_set_state() call -- too
        // branchy for a _resolve()-returns-target-state split, deferred
        // wholesale instead. Already a no-arg void(void) function, so no
        // trampoline plumbing needed beyond this one-line wrapper.
        init_menu_state_on_net_stats_exit();
    }

    void frontgui_levelstats_frame()
    {
        ImGuiIO &io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5, io.DisplaySize.y * 0.5), ImGuiCond_Always, ImVec2(0.5, 0.5));
        ImGui::Begin("##FeLevelStats", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);

        FeHeading(get_string(frontend_button_info[FEBtn_MnuStatistics].capstr_idx));
        FeSeparator();

        FeStylePushFont(FeFont_Body);
        for (const struct StatsData *stat = main_stats_data; stat->name_stridx > 0; stat++)
            draw_stat_row(stat);
        FeStylePopFont();

        FeSeparator();

        bool open = FeBeginListBox("##levelstats_scroll", ImVec2(420, 260));
        if (open)
        {
            for (const struct StatsData *stat = scrolling_stats_data; stat->name_stridx > 0; stat++)
                draw_stat_row(stat);
        }
        FeEndListBox(open);

        FeSeparator();
        if (FeButton(get_string(frontend_button_info[FEBtn_MnuOk].capstr_idx)))
            request_pending_action(&run_pending_stats_leave);

        ImGui::End();
    }

    void frontgui_netservice_frame()
    {
        ImGuiIO &io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5, io.DisplaySize.y * 0.5), ImGuiCond_Always, ImVec2(0.5, 0.5));
        ImGui::Begin("##FeNetService", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);

        FeHeading(get_string(frontend_button_info[FEBtn_NetServiceMenu].capstr_idx));
        FeSeparator();
        FeCaption(get_string(frontend_button_info[FEBtn_NetServices].capstr_idx));

        bool open = FeBeginListBox("##net_service_list", ImVec2(420, 200));
        if (open)
        {
            for (int64_t i = 0; i < net_number_of_services; i++)
            {
                if (FeListRow(net_service[i], false))
                {
                    // frontnet_service_select_by_index() is unsafe to call
                    // directly here -- see its own comment
                    // (frontmenu_net.c) -- deferred wholesale.
                    s_pending_net_service_index = i;
                    request_pending_action(&run_pending_net_service_select);
                }
            }
        }
        FeEndListBox(open);

        FeSeparator();
        if (FeButton(get_string(frontend_button_info[FEBtn_MnuReturnToMain].capstr_idx)))
            request_frontend_state(FeSt_MAIN_MENU);

        ImGui::End();
    }

    void frontgui_netsession_frame()
    {
        ImGuiIO &io = ImGui::GetIO();
        ImVec2 win_size(io.DisplaySize.x * 0.65, io.DisplaySize.y * 0.75);
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5, io.DisplaySize.y * 0.5), ImGuiCond_Always, ImVec2(0.5, 0.5));
        ImGui::SetNextWindowSize(win_size, ImGuiCond_Always);
        ImGui::Begin("##FeNetSession", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);

        FeHeading(get_string(frontend_button_info[FEBtn_MnuOnlineLobbies].capstr_idx));
        FeSeparator();

        FeCaption(get_string(frontend_button_info[FEBtn_NetName].capstr_idx));
        FeTextInput("##net_player_name", tmp_net_player_name, sizeof(tmp_net_player_name));
        if (ImGui::IsItemDeactivatedAfterEdit())
            frontnet_session_set_player_name(nullptr); // no gbtn use in its body -- safe, same idiom frontend.cpp's own frontnet_session_create(NULL) call already uses
        FeSeparator();

        double content_h = ImGui::GetContentRegionAvail().y - fe_bottom_row_reserve();
        FeCaption(get_string(frontend_button_info[FEBtn_NetSessions].capstr_idx));
        bool sess_open = FeBeginListBox("##net_session_list", ImVec2(0, content_h * 0.55));
        if (sess_open)
        {
            for (int64_t i = 0; i < net_number_of_sessions; i++)
            {
                if (net_session[i] == nullptr)
                    continue;
                bool selected = (i == net_session_index_active);
                if (FeListRow(net_session[i]->text, selected))
                    frontnet_session_select_by_index(i); // no frontend_set_state() involved -- safe to call directly, see its own comment
            }
        }
        FeEndListBox(sess_open);

        FeCaption(get_string(frontend_button_info[FEBtn_MnuPlayers].capstr_idx));
        bool ply_open = FeBeginListBox("##net_session_players", ImVec2(0, content_h * 0.3));
        if (ply_open)
        {
            for (int64_t i = 0; i < net_number_of_enum_players; i++)
                FeListRow(net_player[i].name, false);
        }
        FeEndListBox(ply_open);

        FeSeparator();
        bool can_join = (net_session_index_active >= 0) && (net_session_index_active < net_number_of_sessions)
            && (net_session[net_session_index_active] != nullptr);
        ImGui::BeginDisabled(!can_join);
        if (FeButton(get_string(frontend_button_info[FEBtn_NetJoinGame].capstr_idx)))
        {
            int64_t next_state = frontnet_session_join_resolve();
            if (next_state >= 0)
                request_frontend_state((FrontendMenuState)next_state);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (FeButton(get_string(frontend_button_info[FEBtn_NetCreateGame].capstr_idx)))
        {
            int64_t next_state = frontnet_session_create_resolve();
            if (next_state >= 0)
                request_frontend_state((FrontendMenuState)next_state);
        }
        ImGui::SameLine();
        if (FeButton(get_string(frontend_button_info[FEBtn_MnuReturnToMain].capstr_idx)))
        {
            int64_t next_state = frontnet_return_to_main_menu_resolve();
            if (next_state >= 0)
                request_frontend_state((FrontendMenuState)next_state);
        }

        ImGui::End();
    }

    // Largest and least-testable-by-hand screen in this phase (needs an
    // actual live multiplayer session, not just a local menu click, to
    // exercise for real) -- see the plan doc's own note on this. Players +
    // alliance grid on one row, computer-players toggle + mappack picker,
    // then the chat log + input, then Start/Cancel.
    void frontgui_netstart_frame()
    {
        ImGuiIO &io = ImGui::GetIO();
        ImVec2 win_size(io.DisplaySize.x * 0.8, io.DisplaySize.y * 0.85);
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5, io.DisplaySize.y * 0.5), ImGuiCond_Always, ImVec2(0.5, 0.5));
        ImGui::SetNextWindowSize(win_size, ImGuiCond_Always);
        ImGui::Begin("##FeNetStart", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);

        FeHeading(get_string(frontend_button_info[FEBtn_NetSessionMenu].capstr_idx));
        FeSeparator();

        double content_h = ImGui::GetContentRegionAvail().y - fe_bottom_row_reserve();
        double top_h = content_h * 0.35;

        ImGui::BeginGroup();
        FeCaption(get_string(frontend_button_info[FEBtn_MnuPlayers].capstr_idx));
        bool ply_open = FeBeginListBox("##net_start_players", ImVec2(win_size.x * 0.4, top_h));
        if (ply_open)
        {
            for (int64_t i = 0; i < net_number_of_enum_players; i++)
            {
                char label[160];
                uint64_t ping = (i != my_player_number) ? GetPing((int64_t)i, my_player_number) : 0;
                if (ping > 0)
                    std::snprintf(label, sizeof(label), "%s - %" PRIu64 "ms", net_player[i].name, (uint64_t)(ping));
                else
                    std::snprintf(label, sizeof(label), "%s", net_player[i].name);
                FeListRow(label, false);
            }
        }
        FeEndListBox(ply_open);
        ImGui::EndGroup();

        ImGui::SameLine();

        // No text label here either -- the legacy screen's own alliance
        // box tab (frontnet_draw_alliance_box_tab) draws player colour
        // icons, not a caption, so there's no GUIStr_ to reuse for one.
        ImGui::BeginGroup();
        if (ImGui::BeginTable("##alliance_grid", (int64_t)net_number_of_enum_players + 1, ImGuiTableFlags_Borders, ImVec2(0, top_h)))
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            for (int64_t c = 0; c < net_number_of_enum_players; c++)
            {
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(net_player[c].name);
            }
            for (int64_t r = 0; r < net_number_of_enum_players; r++)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(net_player[r].name);
                for (int64_t c = 0; c < net_number_of_enum_players; c++)
                {
                    ImGui::TableNextColumn();
                    if (r == c)
                    {
                        ImGui::TextUnformatted("-");
                        continue;
                    }
                    bool allied = (frontend_alliances & alliance_grid[r][c]) != 0;
                    ImGui::PushID((int64_t)(r * MAX_NET_USERS + c));
                    if (ImGui::Checkbox("##ally", &allied))
                        frontnet_select_alliance_by_index((int64_t)r, (int64_t)c); // queued via the packet system, not immediate -- see its own comment
                    ImGui::PopID();
                }
            }
            ImGui::EndTable();
        }
        ImGui::EndGroup();

        bool computer_on = fe_computer_players != 0;
        if (FeCheckbox(get_string(frontend_button_info[FEBtn_MnuComputer].capstr_idx), &computer_on))
            frontend_toggle_computer_players(nullptr); // no gbtn use in its body

        ImGui::SameLine();
        if (FeButton(campaign.display_name))
            request_frontend_state(FeSt_MP_MAPPACK_SELECT); // frontend_load_mp_mappacks's own body -- no other side effects

        FeSeparator();

        double chat_h = content_h - top_h - 90.0;
        bool msg_open = FeBeginListBox("##net_messages", ImVec2(0, chat_h));
        if (msg_open)
        {
            for (int64_t i = 0; i < net_number_of_messages; i++)
            {
                struct NetMessage *nmsg = &net_message[i];
                char label[NET_MESSAGE_LEN + 32];
                std::snprintf(label, sizeof(label), "%s: %s", net_player[nmsg->plyr_idx].name, nmsg->text);
                FeListRow(label, false);
            }
        }
        FeEndListBox(msg_open);

        struct PlayerInfo *my_net_player = get_my_player();
        FeTextInput("##net_chat_input", my_net_player->mp_message_text, sizeof(my_net_player->mp_message_text));
        if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))
        {
            // Mirrors frontnet_start_input()'s own Enter-to-send body
            // (frontmenu_net.c) -- ImGui's InputText already handles
            // backspace/UTF-8 editing natively, so only the send-on-Enter
            // half needs reimplementing here.
            if (my_net_player->mp_message_text[0] != '\0')
                send_network_chat_message(my_player_number, my_net_player->mp_message_text);
            process_frontend_chat_message(my_player_number, my_net_player->mp_message_text);
        }

        FeSeparator();
        ImGui::BeginDisabled(net_number_of_enum_players <= 1);
        if (FeButton(get_string(frontend_button_info[FEBtn_NetStartGame].capstr_idx)))
            set_packet_start(nullptr); // no gbtn use in its body
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (FeButton(get_string(frontend_button_info[FEBtn_MnuCancel].capstr_idx)))
            request_pending_action(&run_pending_net_return_to_session_menu);

        ImGui::End();
    }

    // Not folded into any one screen's case below: the error box
    // (GMnu_FEERROR_BOX) can appear over any migrated screen -- network
    // errors, map-desync/fxdata-mismatch messages -- triggered from deep
    // inside kfx_net/kfx_game via net_callbacks->create_frontend_error_box,
    // not just at startup. Polled here, independent of frontend_menu_state,
    // mirroring frontend_maintain_error_text_box's own dismiss logic
    // (ESC or timeout, frontend.cpp) since that legacy maintain_call never
    // runs while the underlying screen is ImGui-active (draw_gui(), and
    // with it draw_active_menus_buttons(), is skipped entirely then).
    void draw_error_box_overlay()
    {
        if (!menu_is_active(GMnu_FEERROR_BOX))
            return;

        if (ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            gui_message_timeout = 0;
            turn_off_menu(GMnu_FEERROR_BOX);
            return;
        }
        if ((gui_message_timeout > 0) && (LbTimerClock() > gui_message_timeout))
        {
            turn_off_menu(GMnu_FEERROR_BOX);
            return;
        }

        FeOpenModal("FeErrorBox");
        bool open = FeBeginModal("FeErrorBox");
        if (open)
        {
            FeBodyText(gui_message_text);
            FeSeparator();
            if (FeButton(get_string(frontend_button_info[FEBtn_MnuOk].capstr_idx)))
            {
                gui_message_timeout = 0;
                turn_off_menu(GMnu_FEERROR_BOX);
                ImGui::CloseCurrentPopup();
            }
        }
        FeEndModal(open);
    }
}

TbBool frontend_imgui_screen_active(int64_t state)
{
    return state_is_migrated(state);
}

// Runs land_preview_maintain() (Phase E's master-detail screens' preview
// panel: pan/drag, ensign click-to-highlight, right-click-to-clear) from
// frontend_input(), before frontscreen_end_input()'s own right-click
// "go back" check -- not from FrontendImGuiFrame()/draw_land_preview_panel,
// which would be too late. Found live analysing the design (not a real
// crash report -- caught before shipping): FrontendImGuiFrame() runs from
// RendererSoftware::PresentFrame(), which executes *after*
// frontend_input() within the same frame, so land_preview_maintain()
// consuming a right-click there would always run after
// frontscreen_end_input() already saw the same right_button_clicked flag
// and unconditionally treated it as "go back" -- a right-click meant only
// to clear the preview's ensign highlight would instead always bounce the
// whole screen back to Main Menu. Calling this first restores the same
// ordering the legacy path already has for free (get_gui_inputs()'s
// per-button maintain_calls, including land_preview_maintain, run before
// frontscreen_end_input in the same frontend_input() call).
void FrontendImGuiLandPreviewInput(int64_t state)
{
    if (!frontend_imgui_screen_active(state))
        return;
    if ((state != FeSt_CAMPAIGN_SELECT) && (state != FeSt_MAPPACK_SELECT))
        return; // MP mappack select has no preview panel -- see frontend_mp_mappack_select_resolve's comment
    struct GuiButton gbtn = {};
    gbtn.scr_pos_x = (int64_t)s_land_preview_screen_x;
    gbtn.scr_pos_y = (int64_t)s_land_preview_screen_y;
    gbtn.width = (int64_t)s_land_preview_screen_w;
    gbtn.height = (int64_t)s_land_preview_screen_h;
    // Must match draw_land_preview_panel()'s override around its own
    // land_preview_draw() call (see kLandPreviewImGuiFrameScaleDen's comment)
    // -- otherwise this frame_inset (used for the mouse-in-rect bound and
    // the ensign hit-test's relative coordinates) doesn't match what was
    // actually baked into the rendered content, producing a constant
    // visual offset between the cursor/highlight and the panel's content.
    land_preview_set_frame_extra_scale_den(kLandPreviewImGuiFrameScaleDen);
    land_preview_maintain(&gbtn);
    land_preview_set_frame_extra_scale_den(1);
}

// docs/refactor/renderer/05-imgui-owned-menu-backdrop.md Phase B. Draws
// FeStyleGetMenuBackdropTexture()'s cached texture full-screen via the
// background draw list (renders before every window, so per-screen
// content submitted after this call still layers correctly on top --
// the mirror image of ImGuiContext.cpp's own use of the *foreground* draw
// list for the cursor). Reuses get_frontmenu_background_area_rect()'s own
// aspect-fit math (gui_draw.c) rather than re-deriving it, so the image is
// centred/letterboxed at non-4:3 resolutions exactly like the legacy blit
// already was -- not stretched.
static void draw_menu_backdrop(void)
{
    int64_t tex_w = 0, tex_h = 0;
    void *tex = FeStyleGetMenuBackdropTexture(&tex_w, &tex_h);
    if (tex == nullptr)
        return;
    ImGuiIO &io = ImGui::GetIO();
    struct TbRect area;
    get_frontmenu_background_area_rect(0, 0, (int64_t)io.DisplaySize.x, (int64_t)io.DisplaySize.y, &area);
    ImGui::GetBackgroundDrawList()->AddImage((ImTextureID)(intptr_t)tex,
        ImVec2((double)area.left, (double)area.top), ImVec2((double)area.right, (double)area.bottom));
}

void FrontendImGuiFrame(void)
{
    // Apply anything requested last frame here, before any ImGui window
    // from this module is open -- see s_pending_state's own comment for why
    // frontend_set_state()/load_game() must never run while one of this
    // module's ImGui windows is still on the stack.
    if (s_pending_load_slot >= 0)
    {
        int64_t slot = s_pending_load_slot;
        s_pending_load_slot = -1;
        struct PlayerInfo *player = get_my_player();
        if (!load_game(slot))
        {
            ERRORLOG("Loading game %" PRId64 " failed; quitting.", (int64_t)(slot));
            set_players_packet_action(player, PckA_TogglePause, 0, 0, 0, 0);
            quit_game = 1;
        }
    }
    if (s_pending_state >= 0)
    {
        FrontendMenuState next = (FrontendMenuState)s_pending_state;
        s_pending_state = -1;
        frontend_set_state(next);
    }
    s_deferred_action.drain();

    FeStyleSheetFrame(); // Phase B debug overlay -- independent of migration state

    // docs/refactor/ingame-gui/ Phase 0: the in-game HUD/menu arm. No-op
    // unless a level is running with a migrated GMnu_* turned on -- mutually
    // exclusive with the frontend arm below via kfx_sim_state.game_kind.
    ingame_imgui_frame();

    if (!frontend_imgui_screen_active(frontend_menu_state))
        return;

    draw_menu_backdrop(); // Phase B: every migrated screen's own background, drawn once here

    switch (frontend_menu_state)
    {
        case FeSt_STORY_POEM:     frontgui_story_frame(); break;
        case FeSt_STORY_BIRTHDAY: frontgui_birthday_frame(); break;
        case FeSt_CREDITS:        frontgui_credits_frame(); break;
        case FeSt_FEOPTIONS:      frontgui_options_frame(false); break;
        case FeSt_FEDEFINE_KEYS:  frontgui_definekeys_frame(); break;
        case FeSt_HIGH_SCORES:    frontgui_highscores_frame(); break;
        case FeSt_FELOAD_GAME:    frontgui_loadgame_frame(); break;
        case FeSt_CAMPAIGN_SELECT:   frontgui_campaignselect_frame(); break;
        case FeSt_MAPPACK_SELECT:    frontgui_freeplayselect_frame(); break;
        case FeSt_MP_MAPPACK_SELECT: frontgui_mpmappackselect_frame(); break;
        case FeSt_MAIN_MENU:      frontgui_mainmenu_frame(); break;
        case FeSt_LEVEL_STATS:    frontgui_levelstats_frame(); break;
        case FeSt_NET_SERVICE:    frontgui_netservice_frame(); break;
        case FeSt_NET_SESSION:    frontgui_netsession_frame(); break;
        case FeSt_NET_START:      frontgui_netstart_frame(); break;
        default: break;
    }

    draw_error_box_overlay(); // independent of frontend_menu_state -- see its own comment
}

// docs/refactor/ingame-gui/02-pause-menu-and-options.md: the in-game pause
// menu's "Options" reuses the very same settings window, in its in-game
// form (restart-class rows disabled, Define Keys disabled, "Back" instead
// of "Return to Main"). Called from frontgui_ingame.cpp.
void frontgui_options_frame_ingame(void)
{
    frontgui_options_frame(true);
}
