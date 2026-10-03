#ifndef RENDERER_RENDERERMANAGER_H
#define RENDERER_RENDERERMANAGER_H

#include "bflib_basics.h"  // TbResult
#include "bflib_video.h"   // TbScreenMode, TbScreenCoord
#include "port_check.h"    // KFX_ASSERT_PORT_TABLE

struct SDL_Window;
struct SDL_Renderer;
union SDL_Event;

// RendererType is a C++ enum; C translation units see it as an opaque int.
#ifdef __cplusplus
#  include "renderer/IRenderer.h"
#else
typedef int64_t RendererType;
#  define RENDERER_INVALID  (-1)
#  define RENDERER_AUTO     0
#  define RENDERER_SOFTWARE 1
#  define RENDERER_GPU3D    2
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Lifecycle: initialise the requested backend (nonzero on success) / shut it down.
// If the requested backend's Init() fails and it isn't already
// RENDERER_SOFTWARE, falls back to RENDERER_SOFTWARE rather than leaving
// the game unable to start -- GPU capability is a real, machine-dependent
// boundary condition (unlike every other renderer type here), see
// RendererGpu3D.h.
int64_t          RendererInit(RendererType type);
void         RendererShutdown(void);
RendererType RendererGetActiveType(void);

// gpu-v2 Phase C.1: the user's configured renderer choice (RENDERER,
// keeperfx.cfg / options->Graphics), read by main.cpp at startup and by
// config_settingschema.c's schema row to reflect the pending (not yet
// applied -- SApply_NeedsRestart) choice back in the options UI. Defaults
// to RENDERER_SOFTWARE, matching this project's behaviour before C.1.
RendererType RendererGetDesiredType(void);

// gpu-v2 Phase C.1/C.2: world-frame recording for a GPU backend
// (RendererGpu3D). RendererWorldFrameActive() is true when the active
// backend consumes a recorded WorldFrame instead of the CPU rasterizer
// drawing the world view. Between RendererWorldFrameBegin() and
// RendererWorldFrameEnd() (engine_render.c's display_drawlist()), textured
// world triangles and the software sprite dispatchers record ops in draw
// order instead of rasterizing; End() submits them once. Begin() returns
// false (and nothing is captured) unless the active backend wants world
// frames and the rasterizer is targeting a window of the real framebuffer;
// when it returns true it has cleared that window to transparent so the GPU
// output shows under whatever the CPU still draws there.
struct WorldFrame;
struct PolyPoint;
TbBool RendererWorldFrameActive(void);
void   RendererSubmitWorldFrame(const struct WorldFrame *frame);
TbBool RendererWorldFrameBegin(void);
void   RendererWorldFrameEnd(void);
TbBool RendererWorldFrameCapturing(void);
// The pixel address the recorded coordinates are relative to (world view window, or the
// overlay's graphics window) while capturing, NULL otherwise. Draw primitives only record
// when the rasterizer's target window is this one.
const TbPixel *RendererWorldFrameWindow(void);
// Record translucent sprite draws made after the world frame was submitted onto the GPU
// image instead of blending them against the transparent CPU layer. No-op (returns 0, CPU
// path unchanged) unless a GPU world frame was submitted this frame.
TbBool RendererOverlayBegin(void);
void   RendererOverlayEnd(void);
// gpu-v2 Phase C.5: stamp subsequently recorded ops with the draw-list depth
// bucket they belong to (bucket 0 = nearest, bucket_count-1 = farthest).
void   RendererWorldFrameSetDepth(int64_t bucket, int64_t bucket_count);
// GPU_TRUE_DEPTH (experimental, keeperfx.cfg): when on, terrain triangles depth-test with
// their per-vertex depth (PolyPoint::Z) and the recorder no longer forces submission order
// to agree with depth, so the GPU depth buffer -- not the painter's algorithm -- decides
// visibility. Off (default): output identical to the CPU path.
void   RendererSetTrueDepth(TbBool enabled);
// GPU_DEBUG (keeperfx.cfg / options, restart needed): create the Vulkan device in debug
// mode, i.e. with the validation layers (slow; developers only). Read when the GPU device
// is created. Used to be tied to the heavy-log build (docs/refactor-pass2/stage-02-logging-option.md).
void   RendererSetGpuDebug(TbBool enabled);
TbBool RendererGetGpuDebug(void);

// gpu-v2 Phase C.5 lighting pass. RENDERER_LIGHTING_CLASSIC: the engine bakes every light into
// its per-subtile lightness grid (vertex shade, gouraud). RENDERER_LIGHTING_PERPIXEL: with the
// Vulkan renderer active, dynamic lights are evaluated per terrain pixel on the GPU (needs, and
// forces, per-vertex depth). PerPixelLightingActive() is what the engine asks each frame.
#define RENDERER_LIGHTING_CLASSIC  0
#define RENDERER_LIGHTING_PERPIXEL 1
// Lit sprites (per-pixel lighting): the engine brackets the draw of a shaded thing sprite with
// RendererSpriteLightSet(base shade x256) .. RendererSpriteLightClear(); while set, and while the
// frame carries lighting inputs, the sprite capture records it as WFS_LIT. HasLighting: this frame
// being recorded carries per-pixel lighting inputs.
void    RendererSpriteLightSet(int64_t shade_x256);
void    RendererSpriteLightClear(void);
int64_t RendererSpriteLightGet(void);
TbBool  RendererWorldFrameHasLighting(void);
void   RendererSetLightingMode(int mode);
int    RendererGetLightingMode(void);
TbBool RendererPerPixelLightingActive(void);
// Feed this frame's per-pixel lighting inputs (between Begin and End): the view->map x/y/z
// affine rows (4 doubles each), the perspective (lens, centre), the vertex-shade distance fade
// {min, max, scaler, range}, `light_count` lights as x,y,z,radius,intensity,r,g,b floats (8 per light, colour 0..1), and the
// solid-column height grid in subtiles (grid_w x grid_h bytes) the shadow rays march through.
// See WorldFrameLighting.
void   RendererWorldFrameSetLighting(const double map_x[4], const double map_y[4], const double map_z[4], double lens, double centre_x, double centre_y,
                                     const double fade[4], const float *lights, int64_t light_count,
                                     const unsigned char *grid, int64_t grid_w, int64_t grid_h);
TbBool RendererGetTrueDepth(void);
void   RendererWorldFrameAddPoly(const struct PolyPoint *a, const struct PolyPoint *b, const struct PolyPoint *c, unsigned char *texture);
// Sprite ops -- see WorldFrameSpriteOp (renderer/WorldFrame.h). xmap/ymap:
// per destination column/row, the source column/row; cmap: 256 RGBA8
// entries (r | g<<8 | b<<16 | a<<24) or NULL for the active palette.
// One triangle of a creature shadow -- see WorldFrameShadowOp. `mask` is the
// engine's 256x256 shadow buffer, copied at call time. Returns whether it was recorded.
TbBool RendererWorldFrameAddShadowTri(const struct PolyPoint *a, const struct PolyPoint *b, const struct PolyPoint *c,
                                      const unsigned char *mask, int64_t shade);
void   RendererWorldFrameAddSprite(const unsigned char *rle, int32_t src_w, int32_t src_h,
                                   int32_t dst_x, int32_t dst_y, int32_t dst_w, int32_t dst_h,
                                   const uint16_t *xmap, const uint16_t *ymap,
                                   const uint32_t *cmap, uint32_t mode, uint32_t rgba);
void         RendererSetDesiredType(RendererType type);

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
// docs/refactor/renderer/gpu-v2/06-call-site-consolidation.md: this used
// to be one raw entry point called from ~21 sites across kfx_apploop/
// kfx_net/kfx_frontend/kfx_platform/kfx_render; it's now two named ones,
// so a future present-path obligation (GPU-submission reentrancy, a
// render-thread contract, ...) gets audited once per entry point instead
// of once per call site. Both currently do exactly the same thing --
// there is no behavioural difference yet -- the split exists so Phase C
// has somewhere to hang per-category logic without another ~21-site
// sweep.
//
// The real per-tick game-loop-body / frontend-menu-loop-body present.
void RendererPresentGameFrame(void);
// One increment of visible progress with no full loop tick behind it --
// Smacker/cutscene frame stepping, net-resync/loading-screen progress,
// landview transitions, palette-fade/mode-change refresh steps.
void RendererPresentStepFrame(void);

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

// gpu-v2 Phase C.3: copy the *composited* frame rect (x,y,w,h in framebuffer
// coordinates) into dst, laid out like the framebuffer (dst[(y+j)*dst_pitch +
// x+i]). With a GPU world layer this frame (RendererGpu3D) that is the GPU
// image read back with the CPU framebuffer alpha-blended over it; otherwise
// it is a plain copy of the CPU framebuffer. The input for CPU post-process
// effects (the eye-lens system) that must see the whole rendered scene. The
// rect is clipped to the framebuffer. A GPU read-back blocks until the GPU
// is done with the frame.
void RendererCopyFrameRect(TbPixel *dst, uint64_t dst_pitch, int64_t x, int64_t y, int64_t w, int64_t h);

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

// Queue a screenshot for the active backend to save (fmt: 1=PNG, 2=BMP).
// docs/refactor/renderer/gpu-v2/01-phase-b-2d-compositing.md B2: the
// software backend captures from the actual composited output (including
// the ImGui overlay) at its own next present, not synchronously from
// this call -- the return value means "request accepted" (a valid path/
// format), not "file written".
TbBool RendererScheduleScreenshot(const char* path, int64_t fmt);

// docs/refactor/renderer/05-imgui-linkage-consolidation.md: ImGui context
// and backend ownership lives in kfx_frontend (gui/FrontendImGui.{h,cpp}),
// the one library that submits real ImGui widgets. kfx_platform can't
// #include that header (layering), so RendererSoftware::PresentFrame(),
// bflib_inputctrl.cpp's poll loop and bflib_mspointer.cpp reach it through
// ports/display_host_port.h's imgui_* entries.

// Thin facade over DisplayHostPort's imgui_screen_owned, for the kfx_platform for the kfx_platform
// call sites (RendererSoftware::PresentFrame) that only need this one
// query rather than the whole struct. The want_capture_mouse/_keyboard
// entries and their facade (RendererWantCaptureMouse) were retired once
// their one caller, bflib_mspointer.cpp's legacy cursor draw, was gone
// (docs/refactor/renderer/gpu-v2/01-phase-b-2d-compositing.md's cursor
// unification).
TbBool RendererScreenOwned(void);
void RendererSetImGuiDemoVisible(TbBool visible);

// Dynamic RGBA texture for embedding rendered content into ImGui via
// ImGui::Image() -- opaque handle, safe to cast straight to ImTextureID,
// STREAMING-backed so callers update it every frame. Forwards to the
// active backend (IRenderer::CreateDynamicTexture et al -- pure SDL, no
// ImGui symbol, so this stays in kfx_platform even though the ImGui
// context itself does not). Thin facade so kfx_frontend (which owns
// imgui.h usage) never needs to reach into the backend directly, matching
// every other ImGui-adjacent entry point on this file.
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

// RendererDrawSlabBackground below falls back to kfx_frontend's own tile
// drawing (DisplayHostPort's draw_slab_background_immediate) when no
// UI-renderer sub-backend is active yet.

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
