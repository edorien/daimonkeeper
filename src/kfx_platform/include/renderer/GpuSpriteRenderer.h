#ifndef RENDERER_GPUSPRITERENDERER_H
#define RENDERER_GPUSPRITERENDERER_H

#include "renderer/WorldFrame.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

struct SDL_GPUDevice;
struct SDL_GPUShader;
struct SDL_GPUGraphicsPipeline;
struct SDL_GPUSampler;
struct SDL_GPUTexture;
struct SDL_GPUBuffer;
struct SDL_GPUTransferBuffer;
struct SDL_GPUCopyPass;
struct SDL_GPURenderPass;

// gpu-v2 Phase C.2: the GPU side of WF_OP_SPRITE (see renderer/WorldFrame.h).
// Owned by RendererGpu3D, which drives it once per frame:
//   BeginFrame -> AddOp for every sprite op (in draw order) -> Upload (inside
//   the frame's copy pass) -> Draw for each recorded SpriteDraw (inside the
//   render pass, interleaved with terrain draws to keep painter's order).
//
// Sprites are cached as 8-bit texel indices in one atlas (RLE-decoded on first
// use, re-decoded if the source bytes change) and coloured in the shader
// through a 256-entry RGBA table per op -- the active palette or the remap /
// shade table the CPU path drew with -- so a palette fade or a per-creature
// shade never invalidates the atlas. Destination pixels find their source
// texel through the CPU path's own scaling step tables (uploaded as a
// lookup texture), so scaling, flips and clipping match it exactly.
class GpuSpriteRenderer {
public:
    struct DrawCall {
        uint32_t mode;   // WorldFrameSpriteMode; picks pipeline + blend constants
        uint32_t first;  // first vertex
        uint32_t count;
    };

    bool Init(SDL_GPUDevice* device);
    void Destroy();

    void BeginFrame(const WorldFrame& frame);
    // Appends the op's quad(s); returns the number of draws written to out
    // (0 = op skipped: atlas/lookup full, or nothing visible).
    // The lighting resources the lit-sprite shader samples (owned by the caller); set once after Init().
    void SetLightGrid(SDL_GPUTexture* grid, SDL_GPUSampler* sampler) { m_light_grid = grid; m_light_sampler = sampler; }
    int AddOp(const WorldFrameSpriteOp& op, float off_x, float off_y, float depth, float view_depth, DrawCall out[2]);
    // Same for one shadow-mask triangle (WF_OP_SHADOW): 1 draw, or 0 if skipped.
    int AddShadow(const WorldFrameShadowOp& op, const unsigned char* pixels, float off_x, float off_y, float depth, float view_depth, DrawCall out[1]);
    bool HasWork() const { return !m_vertices.empty(); }
    void Upload(SDL_GPUCopyPass* copy);
    void Draw(SDL_GPURenderPass* pass, const DrawCall& d);

private:
    struct SpriteVertex {
        float x, y;            // screen pixels
        float lx, ly;          // pixel coordinates within the destination rect
        float atlas_x, atlas_y, xmap_off, ymap_off;
        float lut_row, mode;
        uint8_t rgba[4];
        float depth;           // 0 near .. 1 far (visibility)
        float view_depth;      // hyperbolic view depth (lit sprites' position reconstruction)
    };
    struct Key {
        const unsigned char* rle; int32_t w, h;
        bool operator==(const Key& o) const { return rle == o.rle && w == o.w && h == o.h; }
    };
    struct KeyHash {
        size_t operator()(const Key& k) const noexcept {
            return std::hash<const void*>()(k.rle) ^ (static_cast<size_t>(k.w) * 0x9E3779B1u) ^ (static_cast<size_t>(k.h) << 20);
        }
    };
    struct Entry {
        int32_t x = 0, y = 0;
        uint64_t hash = 0;
        uint64_t frame_seen = 0;
    };
    struct PendingUpload {
        uint32_t staging_offset;
        int32_t x, y, w, h;
    };

    bool create_pipelines();
    bool resolve(const WorldFrameSpriteOp& op, int32_t& ax, int32_t& ay);
    bool resolve_mask(const WorldFrameShadowOp& op, const unsigned char* pixels, int32_t& ax, int32_t& ay);
    bool alloc_rect(int32_t w, int32_t h, int32_t& x, int32_t& y);

    SDL_GPUDevice* m_device = nullptr;
    SDL_GPUShader* m_vs = nullptr;
    SDL_GPUShader* m_fs = nullptr;
    SDL_GPUGraphicsPipeline* m_pipelines[8] = {};
    SDL_GPUTexture* m_light_grid = nullptr;      // owned by RendererGpu3D (per-pixel lighting height field)
    SDL_GPUSampler* m_light_sampler = nullptr;
    SDL_GPUSampler* m_sampler = nullptr;

    SDL_GPUTexture* m_atlas = nullptr;
    int32_t m_atlas_size = 0;
    SDL_GPUTransferBuffer* m_atlas_staging = nullptr;
    uint32_t m_atlas_staging_capacity = 0;
    std::unordered_map<Key, Entry, KeyHash> m_entries;
    std::unordered_map<uint64_t, Entry> m_mask_entries; // shadow masks, keyed by content hash
    int32_t m_cursor_x = 0, m_cursor_y = 0, m_shelf_h = 0;
    bool m_atlas_full = false;
    bool m_atlas_reset_pending = false;

    SDL_GPUTexture* m_lut = nullptr;
    SDL_GPUTexture* m_lookup = nullptr;
    SDL_GPUTransferBuffer* m_lut_staging = nullptr;
    SDL_GPUTransferBuffer* m_lookup_staging = nullptr;
    static const int kLutRows = 64;
    static const int kLookupWidth = 4096;
    static const int kLookupHeight = 512;

    SDL_GPUBuffer* m_vertex_buffer = nullptr;
    SDL_GPUTransferBuffer* m_vertex_staging = nullptr;
    uint32_t m_vertex_capacity = 0; // vertices

    // Per-frame state.
    uint64_t m_frame_id = 0;
    const WorldFrame* m_frame = nullptr;
    std::vector<SpriteVertex> m_vertices;
    std::vector<unsigned char> m_pending_pixels; // decoded indices for the uploads below
    std::vector<PendingUpload> m_pending;
    uint32_t m_lut_rows_used = 0;
    unsigned char m_palette_copy[768] = {};
    bool m_palette_valid = false;
    int m_bound_pipeline = -1;
};

#endif // RENDERER_GPUSPRITERENDERER_H
