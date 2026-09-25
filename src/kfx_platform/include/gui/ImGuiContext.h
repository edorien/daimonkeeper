#ifndef KFX_GUI_IMGUICONTEXT_H
#define KFX_GUI_IMGUICONTEXT_H

// Narrow, extern-"C"-friendly ImGui lifecycle service. Lives alongside
// RendererManager (docs/refactor/renderer/04-imgui-gui-foundation.md §3.1):
// kfx_platform already owns the SDL_Window (WindowSystemSDL) and the
// SDL_Renderer (RendererSoftware::m_renderer), which are exactly what
// ImGui's SDL3 + SDLRenderer3 backends need, so context creation/teardown
// and per-frame plumbing belong here rather than leaking imgui.h into any
// higher layer's lifecycle code. kfx_frontend (ranked above kfx_platform)
// is the one place allowed to #include imgui.h directly and submit
// widgets -- see the plan doc §3.1/§5.2.

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
// Returns true if a context is active after the call.
TbBool ImGuiContextEnsure(struct SDL_Window *window, struct SDL_Renderer *renderer);

// Tear down the backends + context, if active. Must be called before the
// SDL_Renderer it was created against is destroyed.
void ImGuiContextShutdown(void);

TbBool ImGuiContextIsActive(void);

// Event feed -- call once per polled SDL event, from LbPollInputs()'s poll
// loop (bflib_inputctrl.cpp), gated by ImGuiContextIsActive() there. Mouse
// motion events are deliberately not forwarded here (LbPollInputs skips
// them) -- see ImGuiMousePositionFn below for why.
void ImGuiContextProcessEvent(const union SDL_Event *event);

// The game's own mouse handling (bflib_inputctrl.cpp) grab-warps the OS
// cursor back toward the window centre whenever it nears an edge ("warp-
// based relative motion" -- keeps precise camera control without the
// cursor ever hitting a screen edge), tracking its real logical position
// via accumulated deltas instead of the OS cursor's absolute position.
// Raw SDL motion events' absolute x/y reflect that warping, not the
// game's actual tracked position, so ImGui's own SDL3-backend position
// tracking (which reads those events) would snap to the window centre
// right when the pointer nears any edge -- and since the game's own
// software cursor sprite is drawn at the correct tracked position while
// ImGui's cursor (io.MouseDrawCursor, RendererSoftware.cpp) draws at
// whatever position this feeds it, letting them diverge shows two
// visibly different cursors.
//
// Register a callback returning the game's own tracked position (real
// window pixels, matching where its cursor sprite draws) instead --
// called once per frame, before ImGui::NewFrame(), overriding whatever
// (wrong) position raw motion events would have produced.
typedef void (*ImGuiMousePositionFn)(int64_t *out_x, int64_t *out_y);
void ImGuiContextSetMousePositionCallback(ImGuiMousePositionFn fn);

// Draw the game's own cursor sprite (found live: without this, ImGui falls
// back to its own generic built-in arrow while the pointer is over ImGui
// content, visibly mismatched against the game's actual cursor everywhere
// else) instead of ImGui's software cursor. kfx_platform has no access to
// the game's sprite/asset system (get_frontend_sprite() et al rank above
// it), so the pixels are supplied via callback, as RGBA8888; the callback
// is polled every frame (the in-game path returns the game's *current*
// pointer sprite, which changes -- pickaxe, power hand, per-spell
// pointers, ...) and the texture re-uploaded when `serial` changes (rgba
// only needs to be valid for the duration of the call).
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
typedef TbBool (*ImGuiCursorImageFn)(struct ImGuiCursorImage *out);
void ImGuiContextSetCursorImageCallback(ImGuiCursorImageFn fn);

// True while the *current* screen is entirely ImGui-owned (docs/refactor/
// renderer/05-imgui-owned-menu-backdrop.md's 15 migrated frontend states --
// no live legacy content composited underneath at all, just a static
// backdrop drawn by ImGui itself). RendererSoftware::PresentFrame() uses
// this to skip its own legacy framebuffer blit for those screens; the
// cursor code below uses the same query to draw the ImGui cursor
// unconditionally there too -- WantCaptureMouse alone (true only over the
// actual centred menu panel, not the surrounding backdrop area) left the
// cursor invisible the instant it strayed outside that panel, since there's
// no legacy cursor path left to fall back to once the legacy blit is
// skipped for these screens. Defaults to a null callback (never owned), so
// any screen not yet migrated behaves exactly as before.
typedef TbBool (*ImGuiScreenOwnedFn)(void);
void ImGuiContextSetScreenOwnedCallback(ImGuiScreenOwnedFn fn);
TbBool ImGuiContextScreenOwned(void);

// Per-frame pair: NewFrame before any ImGui:: submission for the frame,
// Render after submission and after the frame's own SDL_Renderer content
// has been drawn but before SDL_RenderPresent (RendererSoftware::PresentFrame
// is the single call site for both, per the plan doc §3.3).
void ImGuiContextNewFrame(void);
void ImGuiContextRender(void);

// Phase A proof-of-concept only (docs/refactor/renderer/
// 04-imgui-gui-foundation.md §7, Phase A exit criteria): show imgui_demo.cpp's
// ShowDemoWindow() every frame while active, so the backend wiring can be
// exercised interactively before any real screen migrates. Superseded by
// per-screen submission in Phase C onward.
void ImGuiContextSetDemoVisible(TbBool visible);

TbBool ImGuiContextWantCaptureMouse(void);
TbBool ImGuiContextWantCaptureKeyboard(void);

// Dynamic RGBA texture, for embedding rendered content (e.g. an off-screen
// software render, the same off-screen-then-composite technique
// ImGuiCursorImageFn above uses for the cursor sprite, scaled up to a full
// interactive panel -- Phase E's land-preview panel) into ImGui via
// ImGui::Image()/AddImage(). Returns an opaque handle (really an
// SDL_Texture*, kept opaque here so kfx_frontend doesn't need to know
// that) already safe to cast directly to ImTextureID at the call site --
// same (ImTextureID)(intptr_t)handle idiom the cursor code uses. Backed by
// SDL_TEXTUREACCESS_STREAMING (unlike the cursor's STATIC texture): the
// caller is expected to call ImGuiContextUpdateTexture() every frame it's
// shown, not just once. width/height are fixed at creation -- destroy and
// recreate to resize. Returns NULL if no ImGui context is active or the
// size is invalid.
void* ImGuiContextCreateTexture(int64_t width, int64_t height);
void ImGuiContextUpdateTexture(void *texture, const void *rgba_data, int64_t width, int64_t height);
void ImGuiContextDestroyTexture(void *texture);

#ifdef __cplusplus
}
#endif

#endif // KFX_GUI_IMGUICONTEXT_H
