#include "pre_inc.h"
#include "frontgui_ingame_parchment.h"

#include "frontgui_style.h"
#include "frontgui_offscreen.h"       // FeOffscreenTarget -- parchment raster capture
#include "renderer/RendererManager.h" // dynamic textures

#include "globals.h"
#include "bflib_basics.h"
#include "bflib_planar.h"             // struct TbRect
#include "bflib_video.h"              // LbGraphicsScreen*, LbScreen*GraphicsWindow, TbPixel
#include "gui_parchment.h"            // draw_map_parchment, draw_2d_map, draw_zoom_box, load_parchment_file, get_map_level_name
#include "player_data.h"              // get_my_player, PVM_ParchmentView
#include "config_keeperfx.h"          // ingame_gui_use_classic_hud

#include "post_inc.h"

#include <cstdint>
#include <vector>
#include <imgui.h>

namespace {
void *s_tex = nullptr;
int64_t s_tex_w = 0;
int64_t s_tex_h = 0;
std::vector<TbPixel> s_pixels;
}

extern "C" TbBool ingame_parchment_active(void)
{
    if (ingame_gui_use_classic_hud())
        return false;
    const struct PlayerInfo *player = get_my_player();
    return (player != nullptr && player->view_mode == PVM_ParchmentView);
}

// The parchment view has no 3D scene behind it -- redraw_parchment_view()
// used to raster straight to the framebuffer. Under ImGui we redirect that
// raster into a dynamic texture and composite it (plus a crisp level name)
// as a full-screen ImGui image. Input (point_to_overhead_map via
// engine_redraw.c screen->map) is unchanged -- the image is 1:1 with the
// screen.
void ingame_parchment_frame(void)
{
    if (ingame_gui_use_classic_hud())
        return;
    const struct PlayerInfo *player = get_my_player();
    if (player == nullptr || player->view_mode != PVM_ParchmentView)
        return;

    const int64_t w = (int64_t)LbGraphicsScreenWidth();
    const int64_t h = (int64_t)LbGraphicsScreenHeight();
    if (w <= 0 || h <= 0)
        return;

    if (w != s_tex_w || h != s_tex_h || s_tex == nullptr)
    {
        if (s_tex != nullptr)
            RendererDestroyDynamicTexture(s_tex);
        s_tex = RendererCreateDynamicTexture(w, h);
        s_tex_w = w;
        s_tex_h = h;
    }
    if (s_tex == nullptr)
        return;

    s_pixels.assign((size_t)w * (size_t)h, TbPixel{ 0, 0, 0, 255 });

    {
        FeOffscreenTarget cap(s_pixels.data(), w, h);
        load_parchment_file();
        draw_map_parchment();
        draw_2d_map();
        draw_zoom_box();
    }
    RendererUpdateDynamicTexture(s_tex, s_pixels.data(), w, h);

    const ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0.0, 0.0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0);
    ImGui::Begin("##IngameParchment", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs
                 | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus
                 | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing);
    ImDrawList *dl = ImGui::GetWindowDrawList();

    dl->AddImage((ImTextureID)(intptr_t)s_tex, ImVec2(0.0, 0.0), io.DisplaySize);

    const char *lv = get_map_level_name();
    if (lv != nullptr && lv[0] != '\0')
    {
        FeStylePushFont(FeFont_Heading);
        const ImVec2 ts = ImGui::CalcTextSize(lv);
        const ImVec2 pos((io.DisplaySize.x - ts.x) * 0.5, io.DisplaySize.y * 0.055);
        dl->AddText(ImVec2(pos.x + 2.0, pos.y + 2.0), IM_COL32(20, 12, 4, 200), lv);
        dl->AddText(pos, IM_COL32(48, 28, 12, 255), lv);
        FeStylePopFont();
    }

    ImGui::End();
}
