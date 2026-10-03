#ifndef KFX_FRONTEND_GUI_FRONTENDIMGUI_H
#define KFX_FRONTEND_GUI_FRONTENDIMGUI_H

// ImGui lifecycle service, moved here from kfx_platform's gui/ImGuiContext.h
// (docs/refactor/renderer/05-imgui-linkage-consolidation.md): kfx_frontend
// is the one library that submits real ImGui widgets (every frontend screen
// and the in-game HUD), so context/backend ownership lives alongside them
// rather than being split across a kfx_platform/kfx_frontend callback
// boundary. kfx_platform still owns the SDL_Window/SDL_Renderer these
// backends attach to; it reaches this lifecycle through the
// DisplayHostPort's imgui_* entries (ports/display_host_port.h) instead of
// #including this header, so no ImGui knowledge leaks below kfx_frontend.

#include "bflib_basics.h" // TbBool

struct SDL_Window;
struct SDL_Renderer;
union SDL_Event;

#ifdef __cplusplus
extern "C" {
#endif

// (Re-)create the ImGui context and its SDL3/SDLRenderer3 backends against
// the given window/renderer if not already active, or if either handle
// changed since the last call (mirrors RendererSoftware::ensure_present_target's
// own window/renderer change detection -- the two lifecycles need to stay
// in step, since the ImGui backends hold references into the SDL_Renderer).
// Returns true if a context is active after the call -- RendererSoftware::
// PresentFrame() gates the rest of the overlay block on this, so a failed
// backend init (or a null window/renderer) safely skips begin_frame/submit/
// render for that frame rather than calling into an inactive context.
TbBool FrontendImGuiEnsure(struct SDL_Window *window, struct SDL_Renderer *renderer);

// Tear down the backends + context, if active. Must be called before the
// SDL_Renderer it was created against is destroyed.
void FrontendImGuiRendererDestroying(void);

TbBool FrontendImGuiIsActive(void);

// Event feed -- call once per polled SDL event, from LbPollInputs()'s poll
// loop (bflib_inputctrl.cpp), gated by FrontendImGuiIsActive() there. Mouse
// motion events are deliberately not forwarded here (LbPollInputs skips
// them) -- FrontendImGuiBeginFrame() feeds ImGui the game's own tracked
// cursor position instead, since the game's warp-based relative mouse
// handling makes a raw motion event's absolute position meaningless near a
// window edge.
void FrontendImGuiProcessEvent(const union SDL_Event *event);

// Draw the game's own cursor sprite (found live: without this, ImGui falls
// back to its own generic built-in arrow while the pointer is over ImGui
// content, visibly mismatched against the game's actual cursor everywhere
// else) instead of ImGui's software cursor. The pixels come from
// FeStyleGetCursorImage() (frontgui_style.cpp), polled every frame (the
// in-game path returns the game's *current* pointer sprite, which changes
// -- pickaxe, power hand, per-spell pointers, ...); the texture is
// re-uploaded when `serial` changes.
struct ImGuiCursorImage {
    const void *rgba; // width * height * 4 bytes, row-major
    int64_t width;
    int64_t height;
    int64_t hotspot_x;
    int64_t hotspot_y;
    // Bumped by the provider whenever the pixels change, so the context
    // knows to re-upload. 0 from a provider that never changes its image.
    uint64_t serial;
    // 1 = the pixels are already at the intended on-screen size (the
    // in-game pointer, pre-scaled to match the game's own cursor); the
    // context draws it 1:1. 0 = native sprite size, context rescales to
    // ImGui's UI scale (the frontend GFS_cursor_horny path).
    TbBool native_size;
};

// True while the *current* screen is entirely ImGui-owned (docs/refactor/
// renderer/05-imgui-owned-menu-backdrop.md's migrated frontend states -- no
// live legacy content composited underneath at all, just a static backdrop
// drawn by ImGui itself) or the in-game parchment map is up. Queried both
// internally (the cursor-draw logic below) and from kfx_platform
// (RendererSoftware::PresentFrame's legacy-blit skip, bflib_mspointer.cpp's
// legacy-cursor skip) via DisplayHostPort's imgui_screen_owned
// -- WantCaptureMouse alone is only true over the actual centred menu
// panel, not the surrounding backdrop area, which has no legacy cursor
// fallback left once the legacy blit is skipped for these screens.
TbBool FrontendImGuiScreenOwned(void);

// Per-frame pair: begin_frame before any ImGui:: submission for the frame,
// render after submission and after the frame's own SDL_Renderer content
// has been drawn but before SDL_RenderPresent (RendererSoftware::PresentFrame
// is the single call site for both).
void FrontendImGuiBeginFrame(void);
void FrontendImGuiRender(void);

// Phase A proof-of-concept only (docs/refactor/renderer/
// 04-imgui-gui-foundation.md §7, Phase A exit criteria): show imgui_demo.cpp's
// ShowDemoWindow() every frame while active, so the backend wiring can be
// exercised interactively before any real screen migrates. Superseded by
// per-screen submission in Phase C onward.
void FrontendImGuiSetDemoVisible(TbBool visible);

TbBool FrontendImGuiWantCaptureMouse(void);
TbBool FrontendImGuiWantCaptureKeyboard(void);

#ifdef __cplusplus
}
#endif

#endif // KFX_FRONTEND_GUI_FRONTENDIMGUI_H
