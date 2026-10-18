#ifndef RENDERER_GPUSPRITERENDERER_H
#define RENDERER_GPUSPRITERENDERER_H

#include "renderer/WorldFrame.h"
#include "renderer/SpriteAtlasCache.h"

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
// Sprites are cached as 8-bit texel indices in an index atlas (RLE-decoded on first
// use, re-decoded if the source bytes change) and coloured in the shader
// through a 256-entry RGBA table per op -- the active palette or the remap /
// shade table the CPU path drew with -- so a palette fade or a per-creature
// shade never invalidates the atlas. Destination pixels find their source
// texel through the CPU path's own scaling step tables (uploaded as a
// lookup texture), so scaling, flips and clipping match it exactly.
//
// HD sprites (docs/refactor/sprites/, Phase 4; WorldFrameSpriteOp's hd_* fields) are cached as RGBA in a second
// atlas, keyed by their id. Their lookups give HD texel coordinates and their LUT row is a curve row (the remap after
// the HD colour transform), with the transform's 64^3 table uploaded when the op needs it (hd_table). The shader
// tells them apart by bit 8 of the vertex mode (bit 16: apply the table; the layer from bit 256 up). An HD op draws
// its original when its texels can't be placed this frame.
//
// Both atlases are 2D arrays of 4096x4096 layers (SpriteAtlasCache, docs/plans/sprite-atlas-thrash.md Phase 3): they
// grow by doubling (the layers copied on the GPU) up to a cap -- the index atlas kIndexMaxLayers, the HD one the HD
// sprite store's share of ASSET_PACK_MEMORY -- and past it evict their least recently used pages, never the ones the
// frame uses. The index atlas's layer is in bits 65536 up of the vertex mode.
//
// Creature shadows (WF_OP_SHADOW_SPRITE, Phase 2) sample their frame's own entry -- the index atlas's silhouette, or
// for a soft shadow the HD atlas's alpha -- so they take no atlas room of their own.
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
    // A creature shadow drawn from its frame's texels (WF_OP_SHADOW_SPRITE): 1 draw (two triangles), or 0 if skipped.
    int AddShadowSprite(const WorldFrameShadowSpriteOp& op, float off_x, float off_y, float depth, DrawCall out[1]);
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
        float aux[4];          // shadows from frames: the frame's width, height, HD scale (0: the index atlas), mirrored
    };
    // An index atlas entry: a sprite (its RLE, size) or a shadow mask (rle nullptr, the mask's content hash).
    struct Key {
        const unsigned char* rle; int32_t w, h; uint64_t mask;
        bool operator==(const Key& o) const { return rle == o.rle && w == o.w && h == o.h && mask == o.mask; }
    };
    struct KeyHash {
        size_t operator()(const Key& k) const noexcept {
            return std::hash<const void*>()(k.rle) ^ (static_cast<size_t>(k.w) * 0x9E3779B1u) ^ (static_cast<size_t>(k.h) << 20)
                ^ static_cast<size_t>(k.mask * 0x9E3779B97F4A7C15ull);
        }
    };
    // A growing 2D-array texture and the one it grew from this frame (copied into it in Upload()).
    struct GpuAtlas {
        SDL_GPUTexture* tex = nullptr;
        SDL_GPUTexture* old = nullptr;
        int32_t size = 0;          // a layer's side
        int32_t layers = 0;
        int32_t old_layers = 0;
        int32_t refused = 0;       // the most layers the GPU took when it refused more (0: none refused)
        int format = 0;            // SDL_GPUTextureFormat
        const char* name = "";
        uint64_t evictions_logged = 0;
    };
    struct PendingUpload {
        uint32_t staging_offset;
        int32_t x, y, w, h;
        int32_t layer = 0;
    };

    bool create_pipelines();
    bool resolve(const unsigned char* rle, int32_t src_w, int32_t src_h, int32_t& ax, int32_t& ay, int32_t& layer);
    bool resolve_mask(const WorldFrameShadowOp& op, const unsigned char* pixels, int32_t& ax, int32_t& ay, int32_t& layer);
    bool resolve_hd(const void* texels, const void* sub_texels, uint64_t id, int32_t hd_w, int32_t hd_h, int32_t stride, bool additive,
                    int32_t& ax, int32_t& ay, int32_t& layer);
    SDL_GPUTexture* create_atlas_texture(const GpuAtlas& atlas, int32_t size, int32_t layers);
    bool grow_atlas(GpuAtlas& atlas, int32_t layers);
    void copy_grown_atlas(SDL_GPUCopyPass* copy, GpuAtlas& atlas);
    void log_evictions(GpuAtlas& atlas, uint64_t evictions);
    void upload_transform_table(SDL_GPUCopyPass* copy);

    SDL_GPUDevice* m_device = nullptr;
    SDL_GPUShader* m_vs = nullptr;
    SDL_GPUShader* m_fs = nullptr;
    SDL_GPUGraphicsPipeline* m_pipelines[8] = {};
    SDL_GPUTexture* m_light_grid = nullptr;      // owned by RendererGpu3D (per-pixel lighting height field)
    SDL_GPUSampler* m_light_sampler = nullptr;
    SDL_GPUSampler* m_sampler = nullptr;

    // The index atlas: R8 texel indices of sprites and shadow masks.
    GpuAtlas m_index_atlas;
    SpriteAtlasCache<Key, KeyHash> m_index;
    static const int32_t kIndexMaxLayers = 16; // 256 MiB at 4096x4096; the shader has 4 bits for the layer
    // Frames a page may go unused before a full atlas reuses it rather than growing (some seconds of play): the atlases
    // grow with what recent frames draw, up to their caps, not with everything ever drawn.
    static const uint64_t kAtlasStaleFrames = 600;
    SDL_GPUTransferBuffer* m_atlas_staging = nullptr;
    uint32_t m_atlas_staging_capacity = 0;

    // HD sprites: RGBA array atlas, its uploads, and the colour transform's table.
    GpuAtlas m_hd_atlas;
    SpriteAtlasCache<uint64_t> m_hd;
    int32_t hd_layer_cap() const;              // the most it may grow to now
    SDL_GPUTransferBuffer* m_hd_staging = nullptr;
    std::vector<uint32_t> m_hd_pending_pixels; // RGBA for the HD uploads
    std::vector<PendingUpload> m_hd_pending;
    SDL_GPUTexture* m_transform_tex = nullptr;   // 4096 x 64 RGBA: entry (r6 << 12 | g6 << 6 | b6)
    SDL_GPUTransferBuffer* m_transform_staging = nullptr;
    unsigned char m_transform_palette[768] = {}; // the active palette the uploaded table is for
    bool m_transform_valid = false;
    bool m_transform_needed = false;             // an op this frame applies the table
    static const uint32_t kHdStagingBytes = 64u * 1024u * 1024u;

public:
    // Tests: the most layers the HD atlas may grow to (0: as RendererGetHdSpriteAtlasCap() allows). Read whenever it
    // grows.
    static int32_t hd_layer_limit;
private:

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
    uint64_t m_evictions_counted = 0; // both atlases' evictions already counted in RPROF
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
