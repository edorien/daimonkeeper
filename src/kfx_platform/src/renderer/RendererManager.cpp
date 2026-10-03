#include "pre_inc.h"
#include "renderer/RendererManager.h"
#include "port_check.h"
#include "renderer/RendererSoftware.h"
#include "renderer/RendererGpu3D.h"
#include "renderer/WorldFrameRecorder.h"
#include "renderer/RendererProfile.h"
#include "renderer/software/SwDrawTarget.h" // SwTargetVec* -- world-frame window
#include "bflib_basics.h"
#include "bflib_video.h"
#include "bflib_sprfnt.h"   // LbTextDrawResizedImmediate
#include "renderer/ITextRenderer.h"
#include "renderer/IUIRenderer.h"
#include "bflib_vidraw.h"   // LbSpriteDraw*Immediate
#include "ports/display_host_port.h"
#include "post_inc.h"

static IRenderer*   s_active_renderer = nullptr;
static RendererType s_active_type     = RENDERER_INVALID;
static RendererType s_desired_type    = RENDERER_SOFTWARE;
static bool s_framebuffer_redirected = false; // RendererSwapFramebufferTarget() active
static unsigned char s_draw_colour = 0;
static int64_t s_draw_flags = 0;


// Allocate a backend for the requested type, or nullptr if unknown.
static IRenderer* create_renderer(RendererType type)
{
    switch (type)
    {
        case RENDERER_SOFTWARE: return new RendererSoftware();
        case RENDERER_GPU3D:    return new RendererGpu3D();
        default:                return nullptr;
    }
}

int64_t RendererInit(RendererType type)
{
    if (s_active_renderer != nullptr)
        RendererShutdown();

    RendererType resolved = (type == RENDERER_AUTO) ? RENDERER_SOFTWARE : type;
    IRenderer* rend = create_renderer(resolved);
    if (rend == nullptr)
    {
        ERRORLOG("Unknown renderer type %" PRId64, (int64_t)type);
        return 0;
    }
    if (!rend->Init())
    {
        ERRORLOG("Renderer '%s' failed to initialise", rend->GetName());
        delete rend;
        // GPU capability is a genuine machine-dependent boundary condition
        // (unlike an unknown/misconfigured type above) -- fall back to the
        // software renderer rather than leave the game unable to start.
        // Avoid recursing when RENDERER_SOFTWARE itself is what failed.
        if (resolved != RENDERER_SOFTWARE)
        {
            WARNLOG("Falling back to the software renderer");
            return RendererInit(RENDERER_SOFTWARE);
        }
        return 0;
    }
    s_active_renderer = rend;
    s_active_type     = resolved;
    SYNCDBG(0, "Renderer backend '%s' active", rend->GetName());
    return 1;
}

void RendererShutdown(void)
{
    if (s_active_renderer == nullptr)
        return;
    s_active_renderer->Shutdown();
    delete s_active_renderer;
    s_active_renderer = nullptr;
    s_active_type     = RENDERER_INVALID;
}

RendererType RendererGetActiveType(void)
{
    return s_active_type;
}

RendererType RendererGetDesiredType(void)
{
    return s_desired_type;
}

void RendererSetDesiredType(RendererType type)
{
    s_desired_type = type;
}

TbBool RendererWorldFrameActive(void)
{
    // Off-screen redirected targets (the eye-lens effect) are always CPU-drawn.
    return (s_active_renderer != nullptr && !s_framebuffer_redirected && s_active_renderer->WantsWorldFrame()) ? 1 : 0;
}

void RendererSubmitWorldFrame(const struct WorldFrame *frame)
{
    if (s_active_renderer != nullptr && frame != nullptr)
        s_active_renderer->SubmitWorldFrame(*frame);
}

static int s_lighting_mode = 0; // RENDERER_LIGHTING_CLASSIC / PERPIXEL
static bool s_frame_true_depth = false; // resolved per world frame (GPU_TRUE_DEPTH or per-pixel lighting)
static bool s_true_depth = false; // GPU_TRUE_DEPTH: per-vertex depth instead of painter-parity bucket depth
static WorldFrameRecorder s_world_recorder;
static bool s_world_capturing = false;
static bool s_overlay_capturing = false;      // RendererOverlayBegin()..End()
static const TbPixel *s_capture_window = nullptr; // origin of the window the recorded coordinates are relative to
static int64_t s_world_view_x = 0, s_world_view_y = 0, s_world_view_w = 0, s_world_view_h = 0;
static TbPixel s_world_clear;

TbBool RendererWorldFrameBegin(void)
{
    s_world_capturing = false;
    s_world_recorder.Reset();
    if (!RendererWorldFrameActive() || lbDisplay.WScreen == NULL)
        return 0;
    const TbPixel *view = SwTargetVecScreen();
    const uint64_t pitch = SwTargetVecScreenWidth();
    if (view == NULL || pitch == 0 || pitch != (uint64_t)lbDisplay.GraphicsScreenWidth || view < lbDisplay.WScreen)
        return 0;
    const int64_t offset = (int64_t)(view - lbDisplay.WScreen);
    const int64_t x = offset % (int64_t)pitch;
    const int64_t y = offset / (int64_t)pitch;
    const int64_t w = SwTargetVecWindowWidth();
    const int64_t h = SwTargetVecWindowHeight();
    if (w <= 0 || h <= 0 || x + w > (int64_t)pitch || y + h > (int64_t)lbDisplay.GraphicsScreenHeight)
        return 0;
    s_world_clear = view[0]; // the frame clear colour, until terrain covers it
    for (int64_t row = 0; row < h; row++)
        memset((void *)(view + row * (int64_t)pitch), 0, (size_t)w * sizeof(TbPixel));
    s_world_view_x = x; s_world_view_y = y; s_world_view_w = w; s_world_view_h = h;
    s_capture_window = view;
    // Visibility depth: per-vertex only when GPU_TRUE_DEPTH asks for it. Per-pixel lighting no longer
    // forces it -- it needs positions, not a different visibility rule (ops carry view_depth for that),
    // and per-vertex visibility can differ from the painter's algorithm at wall edges.
    s_frame_true_depth = s_true_depth;
    s_world_recorder.SetMonotoneDepth(!s_frame_true_depth);
    s_world_capturing = true;
    return 1;
}

void RendererWorldFrameEnd(void)
{
    if (!s_world_capturing)
        return;
    s_world_capturing = false;
    WorldFrame frame = s_world_recorder.Build();
    frame.view_x = s_world_view_x;
    frame.view_y = s_world_view_y;
    frame.view_w = s_world_view_w;
    frame.view_h = s_world_view_h;
    frame.clear_r = s_world_clear.r;
    frame.clear_g = s_world_clear.g;
    frame.clear_b = s_world_clear.b;
    frame.true_depth = s_frame_true_depth ? 1 : 0;
    RendererSubmitWorldFrame(&frame);
    s_world_recorder.Reset();
}

void RendererWorldFrameSetDepth(int64_t bucket, int64_t bucket_count)
{
    if (!s_world_capturing || bucket_count <= 0)
        return;
    const double frac = ((double)bucket + 0.5) / (double)bucket_count; // 0 near .. 1 far, linear in view z
    // Painter-parity mode: linear bucket depth (only ordering matters). True-depth
    // mode: same convention as the per-vertex PolyPoint::Z, so sprites/shadows and
    // terrain compare consistently.
    const float hyperbolic = (float)worldframe_depth_from_view_z_f(frac * WORLDFRAME_DEPTH_FAR_Z);
    s_world_recorder.SetDepth(s_frame_true_depth ? hyperbolic : (float)frac, hyperbolic);
}

void RendererSetLightingMode(int mode)
{
    s_lighting_mode = (mode == RENDERER_LIGHTING_PERPIXEL) ? RENDERER_LIGHTING_PERPIXEL : RENDERER_LIGHTING_CLASSIC;
}

int RendererGetLightingMode(void)
{
    return s_lighting_mode;
}

TbBool RendererPerPixelLightingActive(void)
{
    return (s_lighting_mode == RENDERER_LIGHTING_PERPIXEL && RendererWorldFrameActive()) ? 1 : 0;
}

void RendererWorldFrameSetLighting(const double map_x[4], const double map_y[4], const double map_z[4], double lens, double centre_x, double centre_y,
                                   const double fade[4], const float *lights, int64_t light_count,
                                   const unsigned char *grid, int64_t grid_w, int64_t grid_h)
{
    if (!s_world_capturing || s_lighting_mode != RENDERER_LIGHTING_PERPIXEL)
        return;
    WorldFrameLighting l = {};
    for (int i = 0; i < 4; i++) { l.map_x[i] = map_x[i]; l.map_y[i] = map_y[i]; l.map_z[i] = map_z[i]; }
    l.lens = lens; l.centre_x = centre_x; l.centre_y = centre_y;
    l.fade_min = fade[0]; l.fade_max = fade[1]; l.fade_scaler = fade[2]; l.fade_range = fade[3];
    if (light_count > WORLDFRAME_MAX_LIGHTS) light_count = WORLDFRAME_MAX_LIGHTS;
    for (int64_t i = 0; i < light_count; i++)
    {
        l.lights[i].x = lights[8 * i]; l.lights[i].y = lights[8 * i + 1]; l.lights[i].z = lights[8 * i + 2];
        l.lights[i].radius = lights[8 * i + 3]; l.lights[i].intensity = lights[8 * i + 4];
        l.lights[i].r = lights[8 * i + 5]; l.lights[i].g = lights[8 * i + 6]; l.lights[i].b = lights[8 * i + 7];
    }
    l.light_count = (int32_t)(light_count > 0 ? light_count : 0);
    s_world_recorder.SetLighting(l, grid, grid_w, grid_h);
}

// Per-pixel-lit sprites: the engine sets the base shade (8.8 fixed, 0..63 shade units) of the thing sprite
// it is about to draw; the sprite capture reads it. -1 = not a lit sprite.
static int64_t s_sprite_light = -1;
void    RendererSpriteLightSet(int64_t shade_x256) { s_sprite_light = shade_x256; }
void    RendererSpriteLightClear(void)            { s_sprite_light = -1; }
int64_t RendererSpriteLightGet(void)              { return s_sprite_light; }
TbBool  RendererWorldFrameHasLighting(void)       { return ((s_world_capturing || s_overlay_capturing) && s_world_recorder.HasLighting()) ? 1 : 0; }

static bool s_gpu_debug = false; // GPU_DEBUG: Vulkan validation layers

void RendererSetGpuDebug(TbBool enabled)
{
    s_gpu_debug = (enabled != 0);
}

TbBool RendererGetGpuDebug(void)
{
    return s_gpu_debug ? 1 : 0;
}

void RendererSetTrueDepth(TbBool enabled)
{
    s_true_depth = (enabled != 0);
}

TbBool RendererGetTrueDepth(void)
{
    return s_true_depth ? 1 : 0;
}

const TbPixel *RendererWorldFrameWindow(void)
{
    return (s_world_capturing || s_overlay_capturing) ? s_capture_window : nullptr;
}

// gpu-v2 (possession-swipe fix): translucent draws made *after* the world frame was
// submitted (the possession swipe, drawn over the finished scene by
// draw_swipe_graphic()) would otherwise blend against the transparent CPU layer, not the
// GPU image. Between RendererOverlayBegin() and End(), sprite draws are recorded and drawn
// on top of the GPU world target with their real blend modes (ghost/alpha/...), no clear.
TbBool RendererOverlayBegin(void)
{
    if (s_world_capturing || s_overlay_capturing || s_active_renderer == nullptr || lbDisplay.WScreen == nullptr)
        return 0;
    if (!RendererWorldFrameActive() || !s_active_renderer->HasWorldLayer())
        return 0;
    const TbPixel *win = SwTargetGraphicsWindowPtr();
    const int64_t pitch = lbDisplay.GraphicsScreenWidth;
    if (win == nullptr || pitch <= 0 || win < lbDisplay.WScreen)
        return 0;
    const int64_t offset = (int64_t)(win - lbDisplay.WScreen);
    const int64_t x = offset % pitch, y = offset / pitch;
    const int64_t w = SwTargetWindowWidth(), h = SwTargetWindowHeight();
    if (w <= 0 || h <= 0 || x + w > pitch || y + h > (int64_t)lbDisplay.GraphicsScreenHeight)
        return 0;
    s_world_recorder.Reset();
    s_world_recorder.SetMonotoneDepth(true);
    s_world_recorder.SetDepth(0.0f); // nearest: an overlay is never depth-rejected
    s_world_view_x = x; s_world_view_y = y; s_world_view_w = w; s_world_view_h = h;
    s_capture_window = win;
    s_overlay_capturing = true;
    return 1;
}

void RendererOverlayEnd(void)
{
    if (!s_overlay_capturing)
        return;
    s_overlay_capturing = false;
    WorldFrame frame = s_world_recorder.Build();
    frame.view_x = s_world_view_x;
    frame.view_y = s_world_view_y;
    frame.view_w = s_world_view_w;
    frame.view_h = s_world_view_h;
    frame.overlay = 1;
    if (frame.op_count > 0)
        RendererSubmitWorldFrame(&frame);
    s_world_recorder.Reset();
}

TbBool RendererWorldFrameCapturing(void)
{
    return (s_world_capturing || s_overlay_capturing) ? 1 : 0;
}

void RendererWorldFrameAddPoly(const struct PolyPoint *a, const struct PolyPoint *b, const struct PolyPoint *c, unsigned char *texture)
{
    if (s_world_capturing || s_overlay_capturing)
        s_world_recorder.AddPoly(a, b, c, texture);
}

TbBool RendererWorldFrameAddShadowTri(const struct PolyPoint *a, const struct PolyPoint *b, const struct PolyPoint *c,
                                      const unsigned char *mask, int64_t shade)
{
    return ((s_world_capturing || s_overlay_capturing) && s_world_recorder.AddShadowTri(a, b, c, mask, shade)) ? 1 : 0;
}

void RendererWorldFrameAddSprite(const unsigned char *rle, int32_t src_w, int32_t src_h,
                                 int32_t dst_x, int32_t dst_y, int32_t dst_w, int32_t dst_h,
                                 const uint16_t *xmap, const uint16_t *ymap,
                                 const uint32_t *cmap, uint32_t mode, uint32_t rgba)
{
    if (s_world_capturing || s_overlay_capturing)
        s_world_recorder.AddSprite(rle, src_w, src_h, dst_x, dst_y, dst_w, dst_h, xmap, ymap, cmap, mode, rgba);
}

const unsigned char* RendererGetActivePalette(void)
{
    return LbPaletteGetReadonly();
}

// chan6_to_8() (VGA 6-bit -> 8-bit-per-channel) is declared in bflib_video.h --
// shared with the pixel-format blend-math functions, which need the exact
// same conversion.

TbResult RendererPaletteSet(unsigned char *palette)
{
    if (!lbScreenInitialised)
        return Lb_FAIL;
    TbResult ret = LbPaletteStore(palette);
    if (ret == Lb_SUCCESS)
    {
        const unsigned char* pal6 = LbPaletteGetReadonly();
        unsigned char rgb8[PALETTE_SIZE];
        for (int64_t i = 0; i < PALETTE_SIZE; i++)
            rgb8[i] = chan6_to_8(pal6[i]);
        RendererSetDisplayPalette(rgb8);
    }
    return ret;
}

void RendererSetDisplayPalette(const unsigned char *rgb8)
{
    if (s_active_renderer != nullptr)
        s_active_renderer->SetDisplayPalette(rgb8);
}

void RendererClearScreen(unsigned char colour)
{
    if (s_active_renderer != nullptr)
        s_active_renderer->ClearScreen(colour);
}

// docs/refactor/renderer/gpu-v2/06-call-site-consolidation.md: the actual
// present, kept static -- callers reach it only through the two named
// entry points below now, not directly (no external declaration left in
// RendererManager.h).
static void RendererPresentFrame(void)
{
    if (s_active_renderer != nullptr)
        s_active_renderer->PresentFrame();
    // Debug log levels buffer their writes; get each frame's lines to disk.
    LbLogFlush();
}

void RendererPresentGameFrame(void)
{
    RPROF_BEGIN(RPS_PRESENT);
    RendererPresentFrame();
    RPROF_END(RPS_PRESENT);
    RPROF_FRAME(s_active_renderer != nullptr ? s_active_renderer->GetName() : "none");
}

void RendererPresentStepFrame(void)
{
    RendererPresentFrame();
}

TbResult RendererLockFramebuffer(void)
{
    if (!lbScreenInitialised || s_active_renderer == nullptr)
        return Lb_FAIL;
    TbBytePitch pitch = {0};
    TbPixel* px = (TbPixel*)s_active_renderer->LockFramebuffer(&pitch);
    if (px == nullptr)
    {
        lbDisplay.GraphicsWindowPtr = NULL;
        lbDisplay.WScreen = NULL;
        return Lb_FAIL;
    }
    lbDisplay.WScreen = px;
    lbDisplay.GraphicsScreenWidth = TbBytePitch_ToPixels(pitch);
    lbDisplay.GraphicsWindowPtr = &lbDisplay.WScreen[lbDisplay.GraphicsWindowX +
        lbDisplay.GraphicsScreenWidth * lbDisplay.GraphicsWindowY];
    return Lb_SUCCESS;
}

TbResult RendererUnlockFramebuffer(void)
{
    lbDisplay.WScreen = NULL;
    lbDisplay.GraphicsWindowPtr = NULL;
    if (s_active_renderer != nullptr)
        s_active_renderer->UnlockFramebuffer();
    return Lb_SUCCESS;
}

TbPixel* RendererGetFramebuffer(void)
{
    return lbDisplay.WScreen;
}

void RendererCopyFrameRect(TbPixel *dst, uint64_t dst_pitch, int64_t x, int64_t y, int64_t w, int64_t h)
{
    const TbPixel *fb = lbDisplay.WScreen;
    if (dst == nullptr || fb == nullptr)
        return;
    const int64_t fb_pitch = lbDisplay.GraphicsScreenWidth;
    const int64_t fb_height = lbDisplay.GraphicsScreenHeight;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > fb_pitch) w = fb_pitch - x;
    if (y + h > fb_height) h = fb_height - y;
    if (w <= 0 || h <= 0)
        return;
    RPROF_BEGIN(RPS_READBACK);
    const bool gpu = (s_active_renderer != nullptr) &&
        s_active_renderer->ReadbackWorldLayer(dst, (int64_t)dst_pitch, x, y, w, h);
    RPROF_END(RPS_READBACK);
    for (int64_t row = y; row < y + h; row++)
    {
        const TbPixel *src = fb + row * fb_pitch + x;
        TbPixel *out = dst + row * (int64_t)dst_pitch + x;
        if (!gpu)
        {
            memcpy(out, src, (size_t)w * sizeof(TbPixel));
            continue;
        }
        // out already holds the opaque GPU pixel: CPU layer over it.
        for (int64_t i = 0; i < w; i++)
        {
            const unsigned a = src[i].a;
            if (a == 255)
                out[i] = src[i];
            else if (a != 0)
            {
                out[i].r = (unsigned char)((src[i].r * a + out[i].r * (255 - a)) / 255);
                out[i].g = (unsigned char)((src[i].g * a + out[i].g * (255 - a)) / 255);
                out[i].b = (unsigned char)((src[i].b * a + out[i].b * (255 - a)) / 255);
                out[i].a = 255;
            }
        }
    }
}

static int64_t s_saved_screen_width = 0;
static int64_t s_saved_screen_height = 0;

TbPixel* RendererSwapFramebufferTarget(TbPixel *target, uint64_t width, uint64_t height)
{
    TbPixel *previous = lbDisplay.WScreen;
    s_framebuffer_redirected = true;
    s_saved_screen_width = lbDisplay.GraphicsScreenWidth;
    s_saved_screen_height = lbDisplay.GraphicsScreenHeight;
    lbDisplay.WScreen = target;
    lbDisplay.GraphicsScreenWidth = width;
    lbDisplay.GraphicsScreenHeight = height;
    return previous;
}

void RendererRestoreFramebufferTarget(TbPixel *previous_target)
{
    s_framebuffer_redirected = false;
    lbDisplay.WScreen = previous_target;
    lbDisplay.GraphicsScreenWidth = s_saved_screen_width;
    lbDisplay.GraphicsScreenHeight = s_saved_screen_height;
}

TbBool RendererScheduleScreenshot(const char* path, int64_t fmt)
{
    return (s_active_renderer != nullptr) ? s_active_renderer->ScheduleScreenshot(path, fmt) : 0;
}

void RendererSetImGuiDemoVisible(TbBool visible)
{
    display_imgui_set_demo_visible(visible);
}

TbBool RendererScreenOwned(void)
{
    return display_imgui_screen_owned();
}

void* RendererCreateDynamicTexture(int64_t width, int64_t height)
{
    return (s_active_renderer != nullptr) ? s_active_renderer->CreateDynamicTexture(width, height) : nullptr;
}

void RendererUpdateDynamicTexture(void *texture, const void *rgba_data, int64_t width, int64_t height)
{
    if (s_active_renderer != nullptr)
        s_active_renderer->UpdateDynamicTexture(texture, rgba_data, width, height);
}

void RendererDestroyDynamicTexture(void *texture)
{
    if (s_active_renderer != nullptr)
        s_active_renderer->DestroyDynamicTexture(texture);
}

TbResult RendererSetupScreen(TbScreenMode mode, TbScreenCoord width, TbScreenCoord height,
    unsigned char *palette, int64_t buffers_count, TbBool wscreen_vid)
{
    return LbScreenSetup(mode, width, height, palette, buffers_count, wscreen_vid);
}

TbResult RendererResetScreen(TbBool exiting_application)
{
    return LbScreenReset(exiting_application);
}

TbResult RendererScreenInitialize(void)
{
    return LbScreenInitialize();
}

TbResult RendererSetDoubleBuffering(TbBool state)
{
    return LbScreenSetDoubleBuffering(state);
}

TbBool RendererTextDrawResized(int64_t posx, int64_t posy, int64_t units_per_px, const char *text)
{
    ITextRenderer* tr = (s_active_renderer != nullptr) ? s_active_renderer->GetTextRenderer() : nullptr;
    if (tr == nullptr)
        return LbTextDrawResizedImmediate(posx, posy, units_per_px, text);
    return tr->DrawTextResized(posx, posy, units_per_px, text);
}

static IUIRenderer* active_ui_renderer(void)
{
    return (s_active_renderer != nullptr) ? s_active_renderer->GetUIRenderer() : nullptr;
}

/* The ambient draw state is what the caller set before the call, so it travels with
 * the submission and is reapplied if the draw is replayed later. */
static KfxDrawState ambient_draw_state(void)
{
    return draw_state_make(RendererGetDrawFlags(), RendererGetDrawColour());
}

void RendererDrawSlabBackground(int64_t x, int64_t y, int64_t width, int64_t height)
{
    IUIRenderer* ui = active_ui_renderer();
    if (ui == nullptr) { display_draw_slab_background_immediate(x, y, width, height); return; }
    ui->SubmitSlabBackground((int64_t)x, (int64_t)y, (int64_t)width, (int64_t)height);
}

TbResult RendererDrawBox(int64_t x, int64_t y, uint64_t width, uint64_t height, TbPixel colour)
{
    IUIRenderer* ui = active_ui_renderer();
    if (ui == nullptr) return LbDrawBoxImmediate(x, y, width, height, colour);
    ui->SubmitSolidBox(x, y, (int64_t)width, (int64_t)height, colour, ambient_draw_state());
    return Lb_SUCCESS;
}

TbResult RendererSpriteDraw(int64_t x, int64_t y, const struct TbSprite *spr)
{
    IUIRenderer* ui = active_ui_renderer();
    if (ui == nullptr) return LbSpriteDrawImmediate(x, y, spr);
    return ui->SubmitRawSprite(x, y, spr, ambient_draw_state());
}

TbResult RendererSpriteDrawOneColour(int64_t x, int64_t y, const struct TbSprite *spr, TbPixel colour)
{
    IUIRenderer* ui = active_ui_renderer();
    if (ui == nullptr) return LbSpriteDrawOneColourImmediate(x, y, spr, colour);
    return ui->SubmitRawSpriteOneColour(x, y, spr, colour, ambient_draw_state());
}

TbResult RendererSpriteDrawScaled(int64_t x, int64_t y, const struct TbSprite *spr, int64_t w, int64_t h)
{
    IUIRenderer* ui = active_ui_renderer();
    if (ui == nullptr) return LbSpriteDrawScaledImmediate(x, y, spr, w, h);
    return ui->SubmitRawSpriteScaled(x, y, spr, w, h, ambient_draw_state());
}

TbResult RendererSpriteDrawScaledOneColour(int64_t x, int64_t y, const struct TbSprite *spr, int64_t w, int64_t h, TbPixel colour)
{
    IUIRenderer* ui = active_ui_renderer();
    if (ui == nullptr) return LbSpriteDrawScaledOneColourImmediate(x, y, spr, w, h, colour);
    return ui->SubmitRawSpriteScaledOneColour(x, y, spr, w, h, colour, ambient_draw_state());
}

int64_t RendererSpriteDrawScaledRemap(int64_t x, int64_t y, const struct TbSprite *spr, int64_t w, int64_t h, const TbPixel *cmap)
{
    IUIRenderer* ui = active_ui_renderer();
    if (ui == nullptr) return LbSpriteDrawScaledRemapImmediate(x, y, spr, w, h, cmap);
    return ui->SubmitRawSpriteScaledRemap(x, y, spr, w, h, cmap, ambient_draw_state());
}

unsigned char RendererGetDrawColour(void) { return s_draw_colour; }
void RendererSetDrawColour(unsigned char colour) { s_draw_colour = colour; }

int64_t RendererGetDrawFlags(void) { return s_draw_flags; }
void RendererSetDrawFlags(int64_t flags) { s_draw_flags = flags; }
void RendererAddDrawFlags(int64_t flags) { s_draw_flags |= flags; }
void RendererClearDrawFlags(int64_t flags) { s_draw_flags &= ~flags; }
void RendererToggleDrawFlags(int64_t flags) { s_draw_flags ^= flags; }

TbResult RendererPaletteGet(unsigned char *palette)
{
    return LbPaletteGet(palette);
}
