#ifndef RENDERER_IRENDERER_H
#define RENDERER_IRENDERER_H

#include "bflib_video.h" // TbBytePitch
#include "renderer/WorldFrame.h"

// Selectable renderer backends.
enum RendererType {
    RENDERER_INVALID  = -1,
    RENDERER_AUTO     = 0,  // pick the best available backend at startup
    RENDERER_SOFTWARE = 1,  // CPU software renderer, SDL display output
    // gpu-v2 Phase C.1/C.2: SDL_GPU-backed (Vulkan) world-view renderer,
    // RendererGpu3D -- terrain, sprites and shadows on the GPU; a few
    // overlays still CPU-drawn on top (see its header comment and
    // 07-phased-delivery.md).
    RENDERER_GPU3D    = 2,
};

// Backend-agnostic renderer interface. Grown as drawing migrates behind the seam.
class IRenderer {
public:
    virtual ~IRenderer() = default;

    virtual bool Init() = 0;
    virtual void Shutdown() = 0;
    virtual const char* GetName() const = 0;
    
    virtual void SetDisplayPalette(const unsigned char* rgb8) { (void)rgb8; }

    // Clear the whole display to a palette index. Default no-op.
    virtual void ClearScreen(unsigned char colour) { (void)colour; }

    // Present the drawn frame to the window (blit + flip). Default no-op.
    virtual void PresentFrame() {}

    // Lock the CPU framebuffer for drawing; return its pixels + pitch, or nullptr.
    // Pitch is reported in bytes (SDL's own convention) -- see TbBytePitch.
    virtual unsigned char* LockFramebuffer(TbBytePitch* out_pitch) { (void)out_pitch; return nullptr; }
    virtual void UnlockFramebuffer() {}

    // Queue a screenshot to be saved to a file (fmt: 1=PNG, 2=BMP) --
    // "schedule", not "save synchronously": a backend may defer the
    // actual capture (RendererSoftware does, to see its composited
    // overlay -- see RendererManager.h's own comment). Default:
    // unsupported.
    virtual bool ScheduleScreenshot(const char* path, int64_t fmt) { (void)path; (void)fmt; return false; }

    // Sub-renderers. Null when a backend has none, so callers fall back to
    // drawing directly.
    virtual class ITextRenderer* GetTextRenderer() { return nullptr; }
    virtual class IUIRenderer*   GetUIRenderer()   { return nullptr; }

    // Dynamic RGBA texture (STREAMING-backed), for callers that need a raw
    // texture handle against the backend's own render target -- e.g.
    // kfx_frontend embedding rendered content via ImGui::Image(), which
    // casts the opaque handle straight to ImTextureID. Backend-specific
    // (an SDL_Texture* for RendererSoftware); kept opaque here since
    // IRenderer is backend-agnostic. width/height are fixed at creation --
    // destroy and recreate to resize. Default: unsupported.
    virtual void* CreateDynamicTexture(int64_t width, int64_t height) { (void)width; (void)height; return nullptr; }
    virtual void  UpdateDynamicTexture(void *texture, const void *rgba_data, int64_t width, int64_t height) { (void)texture; (void)rgba_data; (void)width; (void)height; }
    virtual void  DestroyDynamicTexture(void *texture) { (void)texture; }

    // gpu-v2 Phase C seam (see renderer/WorldFrame.h): submit a flattened,
    // backend-agnostic frame of world-view geometry. Default: unsupported --
    // at C.0 only RendererSoftware implements this, translating the IR back
    // into the existing CPU rasterizer call, to prove the IR shape is
    // sufficient before any GPU-backed implementation exists.
    virtual void SubmitWorldFrame(const WorldFrame& frame) { (void)frame; }
    // True when this backend consumes SubmitWorldFrame() *instead of* the
    // CPU rasterizer drawing the world-view polygons (RendererGpu3D). The
    // facade RendererWorldFrameActive() forwards this to kfx_render.
    virtual bool WantsWorldFrame() const { return false; }
    // True when a world frame was submitted since the last ClearScreen() (a GPU layer exists to draw an overlay onto).
    virtual bool HasWorldLayer() const { return false; }
    // gpu-v2 Phase C.3: when a world frame was submitted since the last
    // ClearScreen(), copy the rect (x,y,w,h; framebuffer coordinates) of the
    // GPU-drawn world layer into dst -- laid out like the framebuffer, i.e.
    // dst[(y+j)*dst_pitch + x+i] -- fully opaque, and return true. Blocks
    // until the GPU has finished. Returns false (dst untouched) when there is
    // no GPU layer to read; the caller then has the CPU framebuffer only.
    // Used by RendererCopyFrameRect() to feed CPU post-process effects (the
    // eye-lens system) from a GPU-rendered scene.
    virtual bool ReadbackWorldLayer(TbPixel* dst, int64_t dst_pitch, int64_t x, int64_t y, int64_t w, int64_t h)
    { (void)dst; (void)dst_pitch; (void)x; (void)y; (void)w; (void)h; return false; }
};

#endif // RENDERER_IRENDERER_H
