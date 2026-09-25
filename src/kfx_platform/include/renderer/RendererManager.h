#ifndef RENDERER_RENDERERMANAGER_H
#define RENDERER_RENDERERMANAGER_H

#include "bflib_basics.h"  // TbResult
#include "bflib_video.h"   // TbScreenMode, TbScreenCoord
#include "gui/ImGuiContext.h" // ImGuiCursorImage/ImGuiCursorImageFn, reused directly below

// RendererType is a C++ enum; C translation units see it as an opaque int.
#ifdef __cplusplus
#  include "renderer/IRenderer.h"
#else
typedef int64_t RendererType;
#  define RENDERER_INVALID  (-1)
#  define RENDERER_AUTO     0
#  define RENDERER_SOFTWARE 1
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Lifecycle: initialise the requested backend (nonzero on success) / shut it down.
int64_t          RendererInit(RendererType type);
void         RendererShutdown(void);
RendererType RendererGetActiveType(void);

// The currently-active 6-bit VGA palette (768 bytes) that indexed drawing samples.
const unsigned char* RendererGetActivePalette(void);

// Set / read back the active game palette (the seam entry points engine code uses).
TbResult RendererPaletteSet(unsigned char *palette);
TbResult RendererPaletteGet(unsigned char *palette);

// Apply an 8-bit RGB palette (256*3 bytes) directly to the display
void RendererSetDisplayPalette(const unsigned char *rgb8);

// Clear the whole display to a palette index.
void RendererClearScreen(unsigned char colour);

// Present the drawn frame to the window (blit draw surface + flip).
void RendererPresentFrame(void);

// Lock / unlock the CPU framebuffer, pointing lbDisplay.WScreen at the backend pixels.
TbResult RendererLockFramebuffer(void);
TbResult RendererUnlockFramebuffer(void);

// The current framebuffer pointer (valid while locked) -- for callers outside
// kfx_platform that need the base pointer for a raw/bulk pixel operation
// (image blits, per-pixel overlays) rather than a single IUIRenderer
// submission. A pure read, no lock side effects; mirrors
// renderer/software/SwDrawTarget.h's SwTargetWScreen() but is the public
// (RendererManager) entry point non-kfx_platform callers should use instead
// of reaching into the software backend's internal headers.
TbPixel* RendererGetFramebuffer(void);

// Redirect lbDisplay.WScreen/GraphicsScreenWidth/GraphicsScreenHeight at an
// off-screen buffer for off-screen rendering (e.g. the eye-lens effect's
// render target) -- returns the previous WScreen pointer, which must be
// passed to RendererRestoreFramebufferTarget() to point drawing back at the
// real framebuffer. The previous GraphicsScreenWidth/Height are saved
// internally (single-level -- callers always restore before swapping again)
// and reapplied by RendererRestoreFramebufferTarget(), so a target smaller
// than the real screen (e.g. a small off-screen cursor/icon render) doesn't
// leave the real framebuffer's stride wrong for every draw after it.
// Callers still save/restore the graphics *window* (the clip rect within
// the target) separately via LbScreenStoreGraphicsWindow()/
// LbScreenLoadGraphicsWindow() -- this pair only owns the target identity.
TbPixel* RendererSwapFramebufferTarget(TbPixel *target, uint64_t width, uint64_t height);
void RendererRestoreFramebufferTarget(TbPixel *previous_target);

// Save the current frame to a file via the active backend (fmt: 1=PNG, 2=BMP).
TbBool RendererScheduleScreenshot(const char* path, int64_t fmt);

// Phase A proof-of-concept only: show imgui_demo.cpp's demo window so the
// backend wiring can be exercised interactively (§7 Phase A exit criteria).
void RendererSetImGuiDemoVisible(TbBool visible);

// Per-frame ImGui content submission, for callers above kfx_platform.
// kfx_frontend owns the §5 wrapper layer and Phase B's style-sheet test
// screen, both of which submit real ImGui widgets -- but kfx_platform can't
// call up into kfx_frontend directly (layering), so RendererSoftware::
// PresentFrame calls this registered callback (between ImGui's NewFrame and
// Render) instead, the same *Callbacks-in-kfx_platform,
// registered-in-main.cpp::setup_game() pattern RendererDrawCallbacks above
// already uses. A null callback (the default) means nothing extra is drawn.
typedef void (*RendererImGuiFrameFn)(void);
void RendererSetImGuiFrameCallback(RendererImGuiFrameFn fn);
void RendererRunImGuiFrameCallback(void);

// Feeds ImGui the game's own tracked cursor position each frame, in place
// of raw SDL motion events -- see gui/ImGuiContext.h's ImGuiMousePositionFn
// for why (the game's grab-warp mouse handling makes a motion event's
// absolute position meaningless near a window edge). fn's out_x/out_y are
// real window pixels, matching where the game's own cursor sprite draws
// (e.g. kfx_frontend's GetMouseX()/GetMouseY()).
typedef void (*RendererMousePositionFn)(int64_t *out_x, int64_t *out_y);
void RendererSetMousePositionCallback(RendererMousePositionFn fn);

// Draws the game's own cursor sprite over ImGui content instead of ImGui's
// generic built-in arrow -- see gui/ImGuiContext.h's ImGuiCursorImage for
// the full story. fn supplies the sprite as RGBA8888, e.g. by rendering it
// through the software backend's off-screen framebuffer-target seam
// (RendererSwapFramebufferTarget below) the same way the eye-lens effect
// does, since kfx_platform has no access to the sprite/asset system that
// owns the pixels.
void RendererSetCursorImageCallback(ImGuiCursorImageFn fn);

// True while the current frontend screen is entirely ImGui-owned (docs/
// refactor/renderer/05-imgui-owned-menu-backdrop.md's 15 migrated states) --
// see gui/ImGuiContext.h's ImGuiScreenOwnedFn for the full contract this is
// a thin pass-through to. RendererSoftware::PresentFrame() checks this to
// skip its own legacy framebuffer blit for those screens; the cursor code
// (ImGuiContext.cpp) checks the same thing to draw the ImGui cursor
// unconditionally there, not just when WantCaptureMouse happens to be true.
typedef TbBool (*RendererScreenOwnedFn)(void);
void RendererSetScreenOwnedCallback(RendererScreenOwnedFn fn);
TbBool RendererScreenOwned(void);

// Dynamic RGBA texture for embedding rendered content into ImGui via
// ImGui::Image() -- see gui/ImGuiContext.h's ImGuiContextCreateTexture for
// the full contract (opaque handle, safe to cast straight to ImTextureID,
// STREAMING-backed so callers update it every frame). Thin facade so
// kfx_frontend (which owns imgui.h usage per §3.1/§5.2) never needs to
// reach into gui/ImGuiContext.h directly, matching every other ImGui-
// adjacent entry point on this file.
void* RendererCreateDynamicTexture(int64_t width, int64_t height);
void RendererUpdateDynamicTexture(void *texture, const void *rgba_data, int64_t width, int64_t height);
void RendererDestroyDynamicTexture(void *texture);

// Screen lifecycle (window + draw surface).
TbResult RendererSetupScreen(TbScreenMode mode, TbScreenCoord width, TbScreenCoord height,
    unsigned char *palette, int64_t buffers_count, TbBool wscreen_vid);
TbResult RendererResetScreen(TbBool exiting_application);
TbResult RendererScreenInitialize(void);
TbResult RendererSetDoubleBuffering(TbBool state);

// Current draw colour — ambient draw-call state, held off lbDisplay.  will be removing in the future, just for now it keeps the pr small
// Text. LbTextDrawResized routes here so the active backend can record the
// draw for this frame or draw it now.
TbBool RendererTextDrawResized(int64_t posx, int64_t posy, int64_t units_per_px, const char *text);

// gui_draw.h (kfx_frontend) -- draw_slab64k_background_immediate is
// kfx_frontend's actual tile-drawing code, used as the immediate-mode
// fallback by RendererDrawSlabBackground below when no UI-renderer
// sub-backend is active yet. kfx_platform is the lowest-ranked library
// and can't include gui_draw.h directly, so this is injected instead,
// mirroring bflib_inputctrl.h's InputFocusPredicates and
// bflib_sndlib.h's SoundStateCallbacks.
struct RendererDrawCallbacks {
    void (*draw_slab_background_immediate)(int64_t pos_x, int64_t pos_y, int64_t width, int64_t height);
};
void set_renderer_draw_callbacks(const struct RendererDrawCallbacks *callbacks);
extern const struct RendererDrawCallbacks *renderer_draw_callbacks;

// Sprites. The Lb* entry points route here so the active backend can record the
// draw for this frame or draw it now.
struct TbSprite;
TbResult RendererDrawBox(int64_t x, int64_t y, uint64_t width, uint64_t height, TbPixel colour);
void RendererDrawSlabBackground(int64_t x, int64_t y, int64_t width, int64_t height);
TbResult RendererSpriteDraw(int64_t x, int64_t y, const struct TbSprite *spr);
TbResult RendererSpriteDrawOneColour(int64_t x, int64_t y, const struct TbSprite *spr, TbPixel colour);
TbResult RendererSpriteDrawScaled(int64_t x, int64_t y, const struct TbSprite *spr, int64_t w, int64_t h);
TbResult RendererSpriteDrawScaledOneColour(int64_t x, int64_t y, const struct TbSprite *spr, int64_t w, int64_t h, TbPixel colour);
int64_t      RendererSpriteDrawScaledRemap(int64_t x, int64_t y, const struct TbSprite *spr, int64_t w, int64_t h, const TbPixel *cmap);

unsigned char RendererGetDrawColour(void);
void RendererSetDrawColour(unsigned char colour);

// Current draw flags (TbDrawFlags bitmask) — ambient draw-call state, held off lbDisplay.
int64_t RendererGetDrawFlags(void);
void RendererSetDrawFlags(int64_t flags);   // = flags
void RendererAddDrawFlags(int64_t flags);    // |= flags
void RendererClearDrawFlags(int64_t flags);  // &= ~flags
void RendererToggleDrawFlags(int64_t flags); // ^= flags

#ifdef __cplusplus
}
#endif

#endif // RENDERER_RENDERERMANAGER_H
