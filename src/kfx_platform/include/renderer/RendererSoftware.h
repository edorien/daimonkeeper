#ifndef RENDERER_RENDERERSOFTWARE_H
#define RENDERER_RENDERERSOFTWARE_H

#include "renderer/IRenderer.h"
#include "renderer/backends/SoftwareUIRenderer.h"
#include "renderer/backends/SoftwareTextRenderer.h"

struct SDL_Renderer;
struct SDL_Texture;
struct SDL_Surface;
struct SDL_GPUDevice;
struct SDL_GPUTexture;

// Software backend. it's small for now, it's gonna grow the more I bring things into it.
class RendererSoftware : public IRenderer {
public:
    bool Init() override;
    void Shutdown() override;
    const char* GetName() const override { return "software"; }

    // gpu-v2 Phase C.1: opt-in device sharing for RendererGpu3D, which owns
    // this instance for its own compositing/present machinery (see that
    // class's own header comment). When set (non-null), ensure_present_target()
    // creates m_renderer via SDL_CreateRendererWithProperties on the "gpu"
    // driver, bound to this exact device (SDL_PROP_RENDERER_CREATE_GPU_DEVICE_POINTER)
    // -- so a texture RendererGpu3D's world-view pass renders is natively
    // drawable by this SDL_Renderer, no CPU round-trip (02-graphics-api-choice.md's
    // "preferred" interop answer). Must be called before the first
    // PresentFrame()/ensure_present_target(); default (never called, every
    // existing caller and test) is unchanged plain SDL_CreateRenderer()
    // behaviour.
    void UseGpuDevice(SDL_GPUDevice* device) { m_gpu_device = device; }

    // gpu-v2 Phase C.1: a GPU-rendered world view (RendererGpu3D's target,
    // same device) to draw *under* the CPU framebuffer on the next
    // PresentFrame(), which then composites the CPU surface over it with
    // alpha blending (its world-view window was cleared transparent, so only
    // sprites/overlays drawn there show). Stays in effect across presents
    // (a palette-fade step re-presents the same frame) until the next
    // ClearScreen() or SetWorldUnderlay(). Requires UseGpuDevice().
    void SetWorldUnderlay(SDL_GPUTexture* gpu_texture, int64_t width, int64_t height)
    {
        m_underlay_gpu = gpu_texture; m_underlay_w = width; m_underlay_h = height; m_underlay_pending = true;
    }
    void SetDisplayPalette(const unsigned char* pal6) override;
    void ClearScreen(unsigned char colour) override;
    void PresentFrame() override;
    unsigned char* LockFramebuffer(TbBytePitch* out_pitch) override;
    void UnlockFramebuffer() override;
    bool ScheduleScreenshot(const char* path, int64_t fmt) override;

    void* CreateDynamicTexture(int64_t width, int64_t height) override;
    void  UpdateDynamicTexture(void *texture, const void *rgba_data, int64_t width, int64_t height) override;
    void  DestroyDynamicTexture(void *texture) override;

    void SubmitWorldFrame(const WorldFrame& frame) override;

    IUIRenderer*   GetUIRenderer()   override { return &m_ui_renderer; }
    ITextRenderer* GetTextRenderer() override { return &m_text_renderer; }

private:
    bool ensure_present_target();
    void destroy_present_target();
    void perform_pending_screenshot();

    SDL_Renderer* m_renderer = nullptr;
    SDL_Texture*  m_texture  = nullptr;
    int64_t           m_tex_w    = 0;
    int64_t           m_tex_h    = 0;
    int64_t           m_vsync    = -1; // SDL_SetRenderVSync value; -1 = unset
    SDL_GPUDevice*    m_gpu_device = nullptr; // see UseGpuDevice() above
    SDL_GPUTexture*   m_underlay_gpu = nullptr; // see SetWorldUnderlay() above
    int64_t           m_underlay_w = 0;
    int64_t           m_underlay_h = 0;
    bool              m_underlay_pending = false;
    SDL_Texture*      m_underlay_tex = nullptr;      // wraps m_underlay_gpu for the SDL_Renderer
    SDL_GPUTexture*   m_underlay_tex_src = nullptr;  // which GPU texture m_underlay_tex currently wraps
    int64_t           m_underlay_tex_w = 0, m_underlay_tex_h = 0; // size it was wrapped at (a resized target may reuse the same pointer)

    // docs/refactor/renderer/gpu-v2/01-phase-b-2d-compositing.md B2: a
    // screenshot request queued by ScheduleScreenshot(), captured from
    // the actual composited renderer output (post-ImGui-render,
    // pre-present) inside PresentFrame() rather than synchronously --
    // see that function's own comment for why a synchronous CPU-buffer
    // capture can no longer see the ImGui overlay.
    bool    m_screenshot_pending = false;
    char    m_screenshot_path[512] = {};
    int64_t m_screenshot_fmt = 0;

    SoftwareUIRenderer   m_ui_renderer;
    SoftwareTextRenderer m_text_renderer;
};

#endif // RENDERER_RENDERERSOFTWARE_H
