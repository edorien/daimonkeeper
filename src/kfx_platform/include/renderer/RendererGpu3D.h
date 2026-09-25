#ifndef RENDERER_RENDERERGPU3D_H
#define RENDERER_RENDERERGPU3D_H

#include "renderer/RendererSoftware.h"
#include "renderer/GpuSpriteRenderer.h"

#include <unordered_map>
#include <vector>
#include <cstdint>

struct SDL_GPUDevice;
struct SDL_GPUShader;
struct SDL_GPUGraphicsPipeline;
struct SDL_GPUSampler;
struct SDL_GPUTexture;
struct SDL_GPUBuffer;
struct SDL_GPUTransferBuffer;

// gpu-v2 Phase C.1: SDL_GPU-backed world-view renderer (docs/refactor/
// renderer/gpu-v2/02-graphics-api-choice.md -- SDL_GPU, Vulkan backend
// forced). Owns the SDL_GPUDevice and the world-view graphics pipeline;
// everything else (compositing, ImGui, UI/text rendering, screenshots,
// dynamic textures, present) is delegated wholesale to an internally-owned
// RendererSoftware instance rather than reimplemented here -- 04's own
// design ("no new present architecture", a GPU 3D output texture "slots
// into PresentFrame exactly where the CPU framebuffer texture does today")
// means the compositing machinery Phase B already built and verified stays
// exactly as-is; this class's only real job is the world-view geometry
// pass. The two share one SDL_GPUDevice (RendererSoftware::UseGpuDevice(),
// SDL_PROP_RENDERER_CREATE_GPU_DEVICE_POINTER under the hood) so a texture
// this pass renders is natively drawable by the shared SDL_Renderer with no
// CPU round-trip -- 02's "preferred" interop answer, not the read-back
// fallback.
//
// C.1/C.2 scope: SubmitWorldFrame() renders the recorded world-view ops in
// draw order -- terrain polygons (QK_PolygonStandard + QK_PolygonNearFP, via
// engine_render.c's gpoly wrapper) with real textures from a block cache -- a 2D-array texture, one
// 32x32 layer per distinct texture block, resolved through the active
// palette once and re-uploaded only when the block's palette indices or the
// palette change (docs/refactor/renderer/gpu-v2/03-pixel-format-and-texture-cache.md;
// invalidation by content comparison rather than a generation counter, which
// also catches in-place animated-texture rewrites) -- interleaved with the
// scaled sprites the software sprite dispatchers record (GpuSpriteRenderer),
// so walls occlude creatures exactly as in the CPU painter's algorithm.
// What still draws on the CPU, over everything: see 07-phased-delivery.md's C.2 note.
class RendererGpu3D : public IRenderer {
public:
    bool Init() override;
    void Shutdown() override;
    const char* GetName() const override { return "gpu3d (vulkan)"; }
    bool WantsWorldFrame() const override { return m_device != nullptr && m_pipeline != nullptr; }
    bool HasWorldLayer() const override { return m_world_frame_valid; }

    // Delegated wholesale to the internal RendererSoftware -- see class
    // comment. Kept as thin one-line forwards, not reimplemented, so this
    // class carries zero independent risk for anything except the
    // world-view pass itself.
    void SetDisplayPalette(const unsigned char* rgb8) override { m_software.SetDisplayPalette(rgb8); }
    void ClearScreen(unsigned char colour) override { m_world_frame_valid = false; m_software.ClearScreen(colour); }
    void PresentFrame() override { m_software.PresentFrame(); }
    unsigned char* LockFramebuffer(TbBytePitch* out_pitch) override { return m_software.LockFramebuffer(out_pitch); }
    void UnlockFramebuffer() override { m_software.UnlockFramebuffer(); }
    bool ScheduleScreenshot(const char* path, int64_t fmt) override { return m_software.ScheduleScreenshot(path, fmt); }
    ITextRenderer* GetTextRenderer() override { return m_software.GetTextRenderer(); }
    IUIRenderer*   GetUIRenderer()   override { return m_software.GetUIRenderer(); }
    void* CreateDynamicTexture(int64_t width, int64_t height) override { return m_software.CreateDynamicTexture(width, height); }
    void  UpdateDynamicTexture(void *texture, const void *rgba_data, int64_t width, int64_t height) override { m_software.UpdateDynamicTexture(texture, rgba_data, width, height); }
    void  DestroyDynamicTexture(void *texture) override { m_software.DestroyDynamicTexture(texture); }

    // Renders frame's flattened triangles into m_world_target via a real
    // SDL_GPU graphics pipeline, then hands that texture to the internal
    // RendererSoftware as the underlay for the next PresentFrame().
    void SubmitWorldFrame(const WorldFrame& frame) override;

    // C.3: GPU -> CPU read-back of the world target (see IRenderer). Only
    // valid for a frame submitted since the last ClearScreen().
    bool ReadbackWorldLayer(TbPixel* dst, int64_t dst_pitch, int64_t x, int64_t y, int64_t w, int64_t h) override;

    // Test/diagnostic only: the render target SubmitWorldFrame() draws
    // into, so a verification program can read it back; null before the
    // first successful SubmitWorldFrame() call.
    SDL_GPUTexture* GetWorldTargetForTesting() const { return m_world_target; }
    SDL_GPUTexture* GetDepthTargetForTesting() const { return m_depth_target; }
    SDL_GPUDevice*  GetDeviceForTesting() const { return m_device; }

private:
    bool init_gpu_device();
    bool init_pipeline();
    bool init_block_array();
    bool init_light_grid();
    uint32_t resolve_block_layer(const unsigned char* texture, uint32_t& upload_count, unsigned char* upload_dst);
    bool ensure_world_target(int64_t width, int64_t height);
    bool ensure_vertex_buffer(uint32_t vertex_count);
    void destroy_gpu();

    RendererSoftware m_software;
    GpuSpriteRenderer m_sprites;

    SDL_GPUDevice* m_device = nullptr;
    SDL_GPUShader* m_vertex_shader = nullptr;
    SDL_GPUShader* m_fragment_shader = nullptr;
    SDL_GPUGraphicsPipeline* m_pipeline = nullptr;
    SDL_GPUSampler* m_sampler = nullptr;

    // Block texture cache -- see class comment. Layer 0 is solid white (used
    // for null texture pointers and once the array is full).
    SDL_GPUTexture* m_block_array = nullptr;
    SDL_GPUTransferBuffer* m_block_transfer = nullptr;
    uint32_t m_block_layers = 0;
    uint32_t m_next_layer = 1;
    uint64_t m_frame_id = 0;
    std::vector<uint32_t> m_free_layers; // layers reclaimed from blocks not seen this frame
    bool m_overflow_logged = false;
    struct BlockEntry {
        uint32_t layer = 0;
        uint64_t frame_seen = 0;
        unsigned char indices[WORLDFRAME_BLOCK_SIZE * WORLDFRAME_BLOCK_SIZE];
    };
    std::unordered_map<const unsigned char*, BlockEntry> m_block_cache;
    unsigned char m_palette_copy[768] = {};
    bool m_palette_valid = false;
    std::vector<uint32_t> m_upload_layers; // layers queued for upload this frame, in transfer-slot order

    bool m_world_frame_valid = false; // a world frame was submitted since the last ClearScreen()
    SDL_GPUTransferBuffer* m_readback_transfer = nullptr;
    uint32_t m_readback_capacity = 0; // bytes

    static constexpr uint32_t kLightGridSize = 512;
    SDL_GPUTexture* m_light_grid = nullptr;           // dynamic-light reach flags, R8 (per-pixel lighting)
    SDL_GPUTransferBuffer* m_light_grid_transfer = nullptr;
    SDL_GPUSampler* m_light_sampler = nullptr;

    SDL_GPUTexture* m_world_target = nullptr;
    SDL_GPUTexture* m_depth_target = nullptr; // D32_FLOAT, same size; cleared to 1 (far) each world frame
    int64_t m_world_target_w = 0;
    int64_t m_world_target_h = 0;

    SDL_GPUBuffer* m_vertex_buffer = nullptr;
    SDL_GPUTransferBuffer* m_vertex_transfer = nullptr;
    uint32_t m_vertex_buffer_capacity = 0; // in vertices
};

#endif // RENDERER_RENDERERGPU3D_H
