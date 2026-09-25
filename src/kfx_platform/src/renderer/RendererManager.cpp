#include "pre_inc.h"
#include "renderer/RendererManager.h"
#include "renderer/RendererSoftware.h"
#include "bflib_basics.h"
#include "bflib_video.h"
#include "bflib_sprfnt.h"   // LbTextDrawResizedImmediate
#include "renderer/ITextRenderer.h"
#include "renderer/IUIRenderer.h"
#include "bflib_vidraw.h"   // LbSpriteDraw*Immediate
#include "gui/ImGuiContext.h"
#include "post_inc.h"

static IRenderer*   s_active_renderer = nullptr;
static RendererType s_active_type     = RENDERER_INVALID;
static unsigned char s_draw_colour = 0;
static int64_t s_draw_flags = 0;
static RendererImGuiFrameFn s_imgui_frame_fn = nullptr;

static void noop_draw_slab_background_immediate(int64_t pos_x, int64_t pos_y, int64_t width, int64_t height) {}
static const struct RendererDrawCallbacks default_renderer_draw_callbacks = {
    &noop_draw_slab_background_immediate,
};
const struct RendererDrawCallbacks *renderer_draw_callbacks = &default_renderer_draw_callbacks;

void set_renderer_draw_callbacks(const struct RendererDrawCallbacks *callbacks)
{
    renderer_draw_callbacks = callbacks ? callbacks : &default_renderer_draw_callbacks;
}

// Allocate a backend for the requested type, or nullptr if unknown.
static IRenderer* create_renderer(RendererType type)
{
    switch (type)
    {
        case RENDERER_SOFTWARE: return new RendererSoftware();
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

void RendererPresentFrame(void)
{
    if (s_active_renderer != nullptr)
        s_active_renderer->PresentFrame();
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

static int64_t s_saved_screen_width = 0;
static int64_t s_saved_screen_height = 0;

TbPixel* RendererSwapFramebufferTarget(TbPixel *target, uint64_t width, uint64_t height)
{
    TbPixel *previous = lbDisplay.WScreen;
    s_saved_screen_width = lbDisplay.GraphicsScreenWidth;
    s_saved_screen_height = lbDisplay.GraphicsScreenHeight;
    lbDisplay.WScreen = target;
    lbDisplay.GraphicsScreenWidth = width;
    lbDisplay.GraphicsScreenHeight = height;
    return previous;
}

void RendererRestoreFramebufferTarget(TbPixel *previous_target)
{
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
    ImGuiContextSetDemoVisible(visible);
}

void RendererSetImGuiFrameCallback(RendererImGuiFrameFn fn)
{
    s_imgui_frame_fn = fn;
}

void RendererRunImGuiFrameCallback(void)
{
    if (s_imgui_frame_fn != nullptr)
        s_imgui_frame_fn();
}

void RendererSetMousePositionCallback(RendererMousePositionFn fn)
{
    ImGuiContextSetMousePositionCallback(fn);
}

void RendererSetCursorImageCallback(ImGuiCursorImageFn fn)
{
    ImGuiContextSetCursorImageCallback(fn);
}

void RendererSetScreenOwnedCallback(RendererScreenOwnedFn fn)
{
    // ImGuiScreenOwnedFn and RendererScreenOwnedFn are the same shape
    // (TbBool(void)) by design -- this facade just forwards the pointer,
    // same as every other Renderer*Callback in this file.
    ImGuiContextSetScreenOwnedCallback(fn);
}

TbBool RendererScreenOwned(void)
{
    return ImGuiContextScreenOwned();
}

void* RendererCreateDynamicTexture(int64_t width, int64_t height)
{
    return ImGuiContextCreateTexture(width, height);
}

void RendererUpdateDynamicTexture(void *texture, const void *rgba_data, int64_t width, int64_t height)
{
    ImGuiContextUpdateTexture(texture, rgba_data, width, height);
}

void RendererDestroyDynamicTexture(void *texture)
{
    ImGuiContextDestroyTexture(texture);
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
    if (ui == nullptr) { renderer_draw_callbacks->draw_slab_background_immediate(x, y, width, height); return; }
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
