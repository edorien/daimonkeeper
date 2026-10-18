#include "pre_inc.h"
#include "renderer/GpuSpriteRenderer.h"
#include "renderer/RendererProfile.h"
#include "renderer/RendererManager.h" // RendererGetActivePalette
#include "bflib_video.h"              // expand_indexed_pixel
#include "bflib_colour_transform.h"   // HD sprites: the transform table
#include <SDL3/SDL_gpu.h>
#include "renderer/shaders/world_sprite.vert.spv.h"
#include "renderer/shaders/world_sprite.frag.spv.h"
#include "post_inc.h"

#include <cinttypes>
#include <cstring>

namespace {

const uint32_t kAtlasStagingBytes = 8u * 1024u * 1024u;

// Walks a sprite's RLE rows (see WorldFrameSpriteOp): FNV-1a over the bytes
// consumed. Returns the number of bytes consumed through `consumed`.
uint64_t hash_rle(const unsigned char* rle, int32_t h, size_t& consumed)
{
    uint64_t hash = 1469598103934665603ull;
    size_t pos = 0;
    for (int32_t row = 0; row < h; ++row)
    {
        for (;;)
        {
            const unsigned char b = rle[pos++];
            hash = (hash ^ b) * 1099511628211ull;
            if (b == 0)
                break;
            if (b < 0x80)
            {
                for (unsigned char k = 0; k < b; ++k)
                    hash = (hash ^ rle[pos + k]) * 1099511628211ull;
                pos += b;
            }
        }
    }
    consumed = pos;
    return hash;
}

// Decodes RLE rows into w*h texel indices (0 = transparent/uncovered).
void decode_rle(const unsigned char* rle, int32_t w, int32_t h, unsigned char* out)
{
    std::memset(out, 0, static_cast<size_t>(w) * h);
    size_t pos = 0;
    for (int32_t row = 0; row < h; ++row)
    {
        int32_t x = 0;
        for (;;)
        {
            const signed char b = static_cast<signed char>(rle[pos++]);
            if (b == 0)
                break;
            if (b < 0)
            {
                x += -b;
            }
            else
            {
                for (int k = 0; k < b; ++k)
                    if (x + k < w)
                        out[static_cast<size_t>(row) * w + x + k] = rle[pos + k];
                x += b;
                pos += b;
            }
        }
    }
}

} // namespace

int32_t GpuSpriteRenderer::hd_layer_limit = 0;

SDL_GPUTexture* GpuSpriteRenderer::create_atlas_texture(const GpuAtlas& atlas, int32_t size, int32_t layers)
{
    SDL_GPUTextureCreateInfo hi = {};
    hi.type = SDL_GPU_TEXTURETYPE_2D_ARRAY;
    hi.format = static_cast<SDL_GPUTextureFormat>(atlas.format);
    hi.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    hi.width = size; hi.height = size;
    hi.layer_count_or_depth = layers; hi.num_levels = 1;
    hi.sample_count = SDL_GPU_SAMPLECOUNT_1;
    return SDL_CreateGPUTexture(m_device, &hi);
}

bool GpuSpriteRenderer::create_pipelines()
{
    SDL_GPUShaderCreateInfo vs_info = {};
    vs_info.code = reinterpret_cast<const Uint8*>(g_world_sprite_vert_spv);
    vs_info.code_size = g_world_sprite_vert_spv_size;
    vs_info.entrypoint = "main";
    vs_info.format = SDL_GPU_SHADERFORMAT_SPIRV;
    vs_info.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    vs_info.num_uniform_buffers = 1;
    m_vs = SDL_CreateGPUShader(m_device, &vs_info);
    SDL_GPUShaderCreateInfo fs_info = {};
    fs_info.code = reinterpret_cast<const Uint8*>(g_world_sprite_frag_spv);
    fs_info.code_size = g_world_sprite_frag_spv_size;
    fs_info.entrypoint = "main";
    fs_info.format = SDL_GPU_SHADERFORMAT_SPIRV;
    fs_info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    fs_info.num_samplers = 6; // atlas, lut, lookup, light grid, HD atlas, HD colour transform table
    fs_info.num_uniform_buffers = 1; // set=3 binding=0: Lighting (shared with the terrain shader)
    m_fs = SDL_CreateGPUShader(m_device, &fs_info);
    if (m_vs == nullptr || m_fs == nullptr)
    {
        ERRORLOG("GpuSpriteRenderer: shader creation failed: %s", SDL_GetError());
        return false;
    }

    SDL_GPUVertexBufferDescription vb_desc = {};
    vb_desc.slot = 0;
    vb_desc.pitch = sizeof(SpriteVertex);
    vb_desc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_GPUVertexAttribute attrs[8] = {};
    attrs[0].location = 0; attrs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;      attrs[0].offset = offsetof(SpriteVertex, x);
    attrs[1].location = 1; attrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;      attrs[1].offset = offsetof(SpriteVertex, lx);
    attrs[2].location = 2; attrs[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;      attrs[2].offset = offsetof(SpriteVertex, atlas_x);
    attrs[3].location = 3; attrs[3].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;      attrs[3].offset = offsetof(SpriteVertex, lut_row);
    attrs[4].location = 4; attrs[4].format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM; attrs[4].offset = offsetof(SpriteVertex, rgba);
    attrs[5].location = 5; attrs[5].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT;       attrs[5].offset = offsetof(SpriteVertex, depth);
    attrs[6].location = 6; attrs[6].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT;       attrs[6].offset = offsetof(SpriteVertex, view_depth);
    attrs[7].location = 7; attrs[7].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;      attrs[7].offset = offsetof(SpriteVertex, aux);

    for (uint32_t mode = 0; mode < 8; ++mode)
    {
        SDL_GPUColorTargetDescription ct = {};
        ct.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        SDL_GPUColorTargetBlendState& b = ct.blend_state;
        b.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        b.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
        b.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
        switch (mode)
        {
        case WFS_GHOST1:
        case WFS_GHOST2: // out = src + dest * blend_constant
            b.enable_blend = true;
            b.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            b.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_CONSTANT_COLOR;
            b.color_blend_op = SDL_GPU_BLENDOP_ADD;
            break;
        case WFS_ALPHA:     // dest + ramp delta, dest alpha kept
            b.enable_blend = true;
            b.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            b.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            b.color_blend_op = SDL_GPU_BLENDOP_ADD;
            b.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
            b.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            break;
        case 4:             // dest - ramp delta ('black' ramp), dest alpha kept
            b.enable_blend = true;
            b.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            b.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            b.color_blend_op = SDL_GPU_BLENDOP_REVERSE_SUBTRACT;
            b.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
            b.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            break;
        case 6:             // shadow mask: dest * factor, dest alpha kept
            b.enable_blend = true;
            b.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
            b.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_COLOR;
            b.color_blend_op = SDL_GPU_BLENDOP_ADD;
            b.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
            b.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            break;
        default:            // solid / one-colour: overwrite
            break;
        }

        SDL_GPUGraphicsPipelineCreateInfo pi = {};
        pi.vertex_shader = m_vs;
        pi.fragment_shader = m_fs;
        pi.vertex_input_state.vertex_buffer_descriptions = &vb_desc;
        pi.vertex_input_state.num_vertex_buffers = 1;
        pi.vertex_input_state.vertex_attributes = attrs;
        pi.vertex_input_state.num_vertex_attributes = 8;
        pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        pi.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        pi.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
        pi.target_info.color_target_descriptions = &ct;
        pi.target_info.num_color_targets = 1;
        // gpu-v2 Phase C.5: depth-tested against the terrain/sprites drawn so far;
        // only opaque overwrites (solid / one-colour) write depth -- blended
        // draws (ghost, alpha, shadow) test but leave the buffer alone.
        pi.depth_stencil_state.enable_depth_test = true;
        pi.depth_stencil_state.enable_depth_write = (mode == WFS_SOLID || mode == WFS_ONECOLOUR || mode == WFS_LIT);
        pi.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
        pi.target_info.has_depth_stencil_target = true;
        pi.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
        m_pipelines[mode] = SDL_CreateGPUGraphicsPipeline(m_device, &pi);
        if (m_pipelines[mode] == nullptr)
        {
            ERRORLOG("GpuSpriteRenderer: pipeline %u creation failed: %s", mode, SDL_GetError());
            return false;
        }
    }

    SDL_GPUSamplerCreateInfo si = {};
    si.min_filter = SDL_GPU_FILTER_NEAREST;
    si.mag_filter = SDL_GPU_FILTER_NEAREST;
    si.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    si.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    si.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    m_sampler = SDL_CreateGPUSampler(m_device, &si);
    return m_sampler != nullptr;
}

bool GpuSpriteRenderer::Init(SDL_GPUDevice* device)
{
    m_device = device;
    if (!create_pipelines())
        return false;

    // The atlases: one layer to start (they grow as sprites arrive), pages a quarter (index) or half (HD) a layer wide.
    static const int32_t sizes[] = { 4096, 2048 };
    m_index_atlas = GpuAtlas();
    m_index_atlas.format = SDL_GPU_TEXTUREFORMAT_R8_UNORM;
    m_index_atlas.name = "sprite";
    for (int32_t size : sizes)
    {
        m_index_atlas.tex = create_atlas_texture(m_index_atlas, size, 1);
        if (m_index_atlas.tex != nullptr) { m_index_atlas.size = size; m_index_atlas.layers = 1; break; }
    }
    m_index.Reset(m_index_atlas.size, m_index_atlas.size / 4, 1);
    m_index.SetStaleAge(kAtlasStaleFrames);
    SDL_GPUTextureCreateInfo ti = {};
    ti.type = SDL_GPU_TEXTURETYPE_2D;
    ti.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    ti.layer_count_or_depth = 1; ti.num_levels = 1;
    ti.sample_count = SDL_GPU_SAMPLECOUNT_1;
    ti.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ti.width = 256; ti.height = kLutRows;
    m_lut = SDL_CreateGPUTexture(m_device, &ti);
    ti.format = SDL_GPU_TEXTUREFORMAT_R16_UNORM; // not R16_UINT: SDL_GPU forbids SAMPLER usage on integer formats
    ti.width = kLookupWidth; ti.height = kLookupHeight;
    m_lookup = SDL_CreateGPUTexture(m_device, &ti);
    // HD sprites: an RGBA array atlas, and the colour transform's 64^3 table as 4096 x 64.
    m_hd_atlas = GpuAtlas();
    m_hd_atlas.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    m_hd_atlas.name = "HD sprite";
    for (int32_t size : sizes)
    {
        m_hd_atlas.tex = create_atlas_texture(m_hd_atlas, size, 1);
        if (m_hd_atlas.tex != nullptr) { m_hd_atlas.size = size; m_hd_atlas.layers = 1; break; }
    }
    m_hd.Reset(m_hd_atlas.size, m_hd_atlas.size / 2, 1);
    m_hd.SetStaleAge(kAtlasStaleFrames);
    ti.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ti.width = 4096; ti.height = 64;
    m_transform_tex = SDL_CreateGPUTexture(m_device, &ti);
    if (m_index_atlas.tex == nullptr || m_lut == nullptr || m_lookup == nullptr || m_hd_atlas.tex == nullptr || m_transform_tex == nullptr)
    {
        ERRORLOG("GpuSpriteRenderer: texture creation failed: %s", SDL_GetError());
        return false;
    }

    SDL_GPUTransferBufferCreateInfo tb = {};
    tb.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tb.size = kAtlasStagingBytes;
    m_atlas_staging = SDL_CreateGPUTransferBuffer(m_device, &tb);
    m_atlas_staging_capacity = kAtlasStagingBytes;
    tb.size = 256 * kLutRows * 4;
    m_lut_staging = SDL_CreateGPUTransferBuffer(m_device, &tb);
    tb.size = kLookupWidth * kLookupHeight * 2;
    m_lookup_staging = SDL_CreateGPUTransferBuffer(m_device, &tb);
    tb.size = kHdStagingBytes;
    m_hd_staging = SDL_CreateGPUTransferBuffer(m_device, &tb);
    tb.size = 4096 * 64 * 4;
    m_transform_staging = SDL_CreateGPUTransferBuffer(m_device, &tb);
    if (m_atlas_staging == nullptr || m_lut_staging == nullptr || m_lookup_staging == nullptr || m_hd_staging == nullptr || m_transform_staging == nullptr)
    {
        ERRORLOG("GpuSpriteRenderer: staging buffer creation failed: %s", SDL_GetError());
        return false;
    }
    return true;
}

void GpuSpriteRenderer::Destroy()
{
    if (m_device == nullptr)
        return;
    if (m_vertex_buffer != nullptr) SDL_ReleaseGPUBuffer(m_device, m_vertex_buffer);
    if (m_vertex_staging != nullptr) SDL_ReleaseGPUTransferBuffer(m_device, m_vertex_staging);
    if (m_atlas_staging != nullptr) SDL_ReleaseGPUTransferBuffer(m_device, m_atlas_staging);
    if (m_lut_staging != nullptr) SDL_ReleaseGPUTransferBuffer(m_device, m_lut_staging);
    if (m_lookup_staging != nullptr) SDL_ReleaseGPUTransferBuffer(m_device, m_lookup_staging);
    if (m_hd_staging != nullptr) SDL_ReleaseGPUTransferBuffer(m_device, m_hd_staging);
    if (m_transform_staging != nullptr) SDL_ReleaseGPUTransferBuffer(m_device, m_transform_staging);
    for (GpuAtlas* a : { &m_index_atlas, &m_hd_atlas })
    {
        if (a->tex != nullptr) SDL_ReleaseGPUTexture(m_device, a->tex);
        if (a->old != nullptr) SDL_ReleaseGPUTexture(m_device, a->old);
        *a = GpuAtlas();
    }
    m_index.Reset(0, 0, 0);
    m_hd.Reset(0, 0, 0);
    if (m_transform_tex != nullptr) SDL_ReleaseGPUTexture(m_device, m_transform_tex);
    m_hd_staging = nullptr; m_transform_staging = nullptr; m_transform_tex = nullptr;
    m_transform_valid = false;
    if (m_lut != nullptr) SDL_ReleaseGPUTexture(m_device, m_lut);
    if (m_lookup != nullptr) SDL_ReleaseGPUTexture(m_device, m_lookup);
    if (m_sampler != nullptr) SDL_ReleaseGPUSampler(m_device, m_sampler);
    for (SDL_GPUGraphicsPipeline*& p : m_pipelines)
        if (p != nullptr) { SDL_ReleaseGPUGraphicsPipeline(m_device, p); p = nullptr; }
    if (m_vs != nullptr) SDL_ReleaseGPUShader(m_device, m_vs);
    if (m_fs != nullptr) SDL_ReleaseGPUShader(m_device, m_fs);
    m_vertex_buffer = nullptr; m_vertex_staging = nullptr; m_atlas_staging = nullptr;
    m_lut_staging = nullptr; m_lookup_staging = nullptr; m_lut = nullptr;
    m_lookup = nullptr; m_sampler = nullptr; m_vs = nullptr; m_fs = nullptr;
    m_device = nullptr;
}

void GpuSpriteRenderer::BeginFrame(const WorldFrame& frame)
{
    ++m_frame_id;
    m_frame = &frame;
    m_vertices.clear();
    m_pending_pixels.clear();
    m_pending.clear();
    m_hd_pending_pixels.clear();
    m_hd_pending.clear();
    m_transform_needed = false;
    m_bound_pipeline = -1;
    // The caps can change live (ASSET_PACK_MEMORY), and a GPU that refused a size stays below it.
    int32_t index_cap = kIndexMaxLayers;
    if (m_index_atlas.refused > 0 && m_index_atlas.refused < index_cap) index_cap = m_index_atlas.refused;
    m_index.SetCap(index_cap);
    m_hd.SetCap(hd_layer_cap());
}

bool GpuSpriteRenderer::resolve(const unsigned char* rle, int32_t src_w, int32_t src_h, int32_t& ax, int32_t& ay, int32_t& layer)
{
    const Key key = { rle, src_w, src_h, 0 };
    auto* e = m_index.Find(key, m_frame_id);
    if (e != nullptr && e->stamp == m_frame_id)
    {
        ax = e->x; ay = e->y; layer = e->layer;
        return true;
    }

    size_t consumed = 0;
    const uint64_t hash = hash_rle(rle, src_h, consumed);
    if (e != nullptr && e->hash == hash)
    {
        e->stamp = m_frame_id;
        ax = e->x; ay = e->y; layer = e->layer;
        return true;
    }

    const size_t bytes = static_cast<size_t>(src_w) * src_h;
    if (m_pending_pixels.size() + bytes > m_atlas_staging_capacity)
        return false; // this frame's upload budget is spent; the sprite is skipped this frame
    if (e == nullptr) // else the same size, the same rect: re-decoded in place
    {
        e = m_index.Insert(key, src_w, src_h, m_frame_id, [this](int32_t layers) { return grow_atlas(m_index_atlas, layers); });
        if (e == nullptr)
            return false;
    }
    e->hash = hash;
    e->stamp = m_frame_id;

    PendingUpload up;
    up.staging_offset = static_cast<uint32_t>(m_pending_pixels.size());
    up.x = e->x; up.y = e->y; up.w = src_w; up.h = src_h; up.layer = e->layer;
    m_pending_pixels.resize(m_pending_pixels.size() + bytes);
    decode_rle(rle, src_w, src_h, m_pending_pixels.data() + up.staging_offset);
    m_pending.push_back(up);
    ax = e->x; ay = e->y; layer = e->layer;
    return true;
}

// The most layers the HD atlas may grow to now: RendererGetHdSpriteAtlasCap() (ASSET_PACK_MEMORY's sprite share, live),
// what the GPU took when it refused more, the tests' limit, and the shader's 6 bits for the layer.
int32_t GpuSpriteRenderer::hd_layer_cap() const
{
    const uint64_t layer_bytes = static_cast<uint64_t>(m_hd_atlas.size) * m_hd_atlas.size * 4;
    int32_t cap = (layer_bytes > 0) ? static_cast<int32_t>(RendererGetHdSpriteAtlasCap() / layer_bytes) : 1;
    if (cap > 64) cap = 64;
    if (cap < 1) cap = 1;
    if (m_hd_atlas.refused > 0 && m_hd_atlas.refused < cap) cap = m_hd_atlas.refused;
    if (hd_layer_limit > 0 && hd_layer_limit < cap) cap = hd_layer_limit;
    return cap;
}

// More layers for an atlas: a new texture; Upload() copies the old layers into it before this frame's uploads. False
// when the GPU refused a bigger texture (the atlas then stays at its size).
bool GpuSpriteRenderer::grow_atlas(GpuAtlas& atlas, int32_t layers)
{
    if (layers <= atlas.layers)
        return false;
    SDL_GPUTexture* grown = create_atlas_texture(atlas, atlas.size, layers);
    if (grown == nullptr)
    {
        WARNLOG("GpuSpriteRenderer: the GPU refused a %s atlas of %d layers (%s); staying at %d", atlas.name, (int)layers, SDL_GetError(), (int)atlas.layers);
        atlas.refused = atlas.layers;
        return false;
    }
    if (atlas.old == nullptr)
    {
        // The current texture is copied into the new one in Upload().
        atlas.old = atlas.tex;
        atlas.old_layers = atlas.layers;
    }
    else
    {
        // Grown twice in one frame: the first new texture never got anything; copy from the original.
        SDL_ReleaseGPUTexture(m_device, atlas.tex);
    }
    atlas.tex = grown;
    atlas.layers = layers;
    const int bpp = (atlas.format == SDL_GPU_TEXTUREFORMAT_R8_UNORM) ? 1 : 4;
    JUSTLOG("GpuSpriteRenderer: %s atlas grown to %d layers of %dx%d (%d MiB)", atlas.name, (int)layers, (int)atlas.size, (int)atlas.size,
        (int)((static_cast<int64_t>(layers) * atlas.size * atlas.size * bpp) >> 20));
    return true;
}

void GpuSpriteRenderer::copy_grown_atlas(SDL_GPUCopyPass* copy, GpuAtlas& atlas)
{
    if (atlas.old == nullptr)
        return;
    for (int32_t l = 0; l < atlas.old_layers; ++l)
    {
        SDL_GPUTextureLocation from = {};
        from.texture = atlas.old; from.layer = static_cast<Uint32>(l);
        SDL_GPUTextureLocation to = {};
        to.texture = atlas.tex; to.layer = static_cast<Uint32>(l);
        SDL_CopyGPUTextureToTexture(copy, &from, &to, static_cast<Uint32>(atlas.size), static_cast<Uint32>(atlas.size), 1, false);
    }
    SDL_ReleaseGPUTexture(m_device, atlas.old);
    atlas.old = nullptr;
    atlas.old_layers = 0;
}

// Pages reused (stale ones, or the atlas full at its cap): logged the first time and then at each tenfold count, not
// per page.
void GpuSpriteRenderer::log_evictions(GpuAtlas& atlas, uint64_t evictions)
{
    if (evictions == 0 || evictions < atlas.evictions_logged * 10)
        return;
    JUSTLOG("GpuSpriteRenderer: %s atlas at %d layers (cap %d): %" PRIu64 " least recently used pages reused so far", atlas.name,
        (int)atlas.layers, (int)((&atlas == &m_hd_atlas) ? m_hd.Cap() : m_index.Cap()), evictions);
    atlas.evictions_logged = evictions;
}

// An HD sprite's texels in the HD atlas: uploaded the first time its id is seen (its texels never change under one id).
bool GpuSpriteRenderer::resolve_hd(const void* texels, const void* sub_texels, uint64_t id, int32_t hd_w, int32_t hd_h, int32_t stride,
    bool additive, int32_t& ax, int32_t& ay, int32_t& layer)
{
    const auto* found = m_hd.Find(id, m_frame_id);
    if (found != nullptr)
    {
        ax = found->x + 1; ay = found->y + 1; layer = found->layer;
        return true;
    }
    // A transparent texel round the frame, so a filtered sample at its edge never reaches a neighbour's texels. Additive
    // art (WFS_ALPHA) has its sub texels beside its add texels, at x + hd_w + 2 (zero when it has none).
    const int32_t pw1 = hd_w + 2, pw = additive ? 2 * pw1 : pw1, ph = hd_h + 2;
    if ((m_hd_pending_pixels.size() + static_cast<size_t>(pw) * ph) * 4 > kHdStagingBytes)
        return false; // this frame's upload budget is spent
    const auto* e = m_hd.Insert(id, pw, ph, m_frame_id, [this](int32_t layers) { return grow_atlas(m_hd_atlas, layers); });
    if (e == nullptr)
        return false;
    PendingUpload up;
    up.staging_offset = static_cast<uint32_t>(m_hd_pending_pixels.size() * 4);
    up.x = e->x; up.y = e->y; up.w = pw; up.h = ph; up.layer = e->layer;
    const uint32_t* src = static_cast<const uint32_t*>(texels);
    m_hd_pending_pixels.insert(m_hd_pending_pixels.end(), static_cast<size_t>(pw), 0u);
    const uint32_t* sub = static_cast<const uint32_t*>(sub_texels);
    for (int32_t row = 0; row < hd_h; ++row)
    {
        m_hd_pending_pixels.push_back(0u);
        m_hd_pending_pixels.insert(m_hd_pending_pixels.end(), src + static_cast<size_t>(row) * stride,
            src + static_cast<size_t>(row) * stride + hd_w);
        m_hd_pending_pixels.push_back(0u);
        if (additive)
        {
            m_hd_pending_pixels.push_back(0u);
            if (sub != nullptr)
                m_hd_pending_pixels.insert(m_hd_pending_pixels.end(), sub + static_cast<size_t>(row) * stride,
                    sub + static_cast<size_t>(row) * stride + hd_w);
            else
                m_hd_pending_pixels.insert(m_hd_pending_pixels.end(), static_cast<size_t>(hd_w), 0u);
            m_hd_pending_pixels.push_back(0u);
        }
    }
    m_hd_pending_pixels.insert(m_hd_pending_pixels.end(), static_cast<size_t>(pw), 0u);
    m_hd_pending.push_back(up);
    RPROF_COUNT(RPC_HD_BYTES, static_cast<int64_t>(pw) * ph * 4);
    ax = e->x + 1; ay = e->y + 1; layer = e->layer;
    return true;
}

// The HD colour transform's table for this frame's palette, when an HD op needs it and it changed.
void GpuSpriteRenderer::upload_transform_table(SDL_GPUCopyPass* copy)
{
    if (!m_transform_needed)
        return;
    const unsigned char* active = RendererGetActivePalette();
    if (m_transform_valid && active != nullptr && std::memcmp(m_transform_palette, active, sizeof(m_transform_palette)) == 0)
        return;
    const struct ColourTransform* ct = RendererHdColourTransform();
    if (ct == nullptr || ct->mode != CTM_Table || ct->table == nullptr || active == nullptr)
        return;
    void* tmap = SDL_MapGPUTransferBuffer(m_device, m_transform_staging, true);
    if (tmap == nullptr)
        return;
    std::memcpy(tmap, ct->table, static_cast<size_t>(4) * COLOUR_TRANSFORM_TABLE_SIZE);
    SDL_UnmapGPUTransferBuffer(m_device, m_transform_staging);
    SDL_GPUTextureTransferInfo src = {};
    src.transfer_buffer = m_transform_staging;
    src.pixels_per_row = 4096;
    src.rows_per_layer = 64;
    SDL_GPUTextureRegion dst = {};
    dst.texture = m_transform_tex;
    dst.w = 4096; dst.h = 64; dst.d = 1;
    SDL_UploadToGPUTexture(copy, &src, &dst, false);
    std::memcpy(m_transform_palette, active, sizeof(m_transform_palette));
    m_transform_valid = true;
}

bool GpuSpriteRenderer::resolve_mask(const WorldFrameShadowOp& op, const unsigned char* pixels, int32_t& ax, int32_t& ay, int32_t& layer)
{
    const Key key = { nullptr, op.w, op.h, op.hash };
    auto* e = m_index.Find(key, m_frame_id);
    if (e != nullptr)
    {
        ax = e->x; ay = e->y; layer = e->layer;
        return true;
    }
    const size_t bytes = static_cast<size_t>(op.w) * op.h;
    if (m_pending_pixels.size() + bytes > m_atlas_staging_capacity)
        return false;
    e = m_index.Insert(key, op.w, op.h, m_frame_id, [this](int32_t layers) { return grow_atlas(m_index_atlas, layers); });
    if (e == nullptr)
        return false;
    e->hash = op.hash;
    e->stamp = m_frame_id;

    PendingUpload up;
    up.staging_offset = static_cast<uint32_t>(m_pending_pixels.size());
    up.x = e->x; up.y = e->y; up.w = op.w; up.h = op.h; up.layer = e->layer;
    m_pending_pixels.insert(m_pending_pixels.end(), pixels, pixels + bytes);
    m_pending.push_back(up);
    RPROF_COUNT(RPC_MASK_BYTES, bytes);
    ax = e->x; ay = e->y; layer = e->layer;
    return true;
}

int GpuSpriteRenderer::AddShadow(const WorldFrameShadowOp& op, const unsigned char* pixels, float off_x, float off_y, float depth, float view_depth, DrawCall out[1])
{
    if (m_device == nullptr || m_frame == nullptr || op.w <= 0 || op.h <= 0)
        return 0;
    int32_t ax, ay, layer;
    if (!resolve_mask(op, pixels, ax, ay, layer))
    {
        RPROF_COUNT(RPC_SKIPPED, 1);
        return 0;
    }
    // render_shade()'s factor: linear darken up to 31, non-linear brighten beyond.
    const float shade = static_cast<float>(op.shade);
    const float factor = ((shade <= 31.0f) ? shade : (3.0f * shade - 64.0f)) / 32.0f;
    SpriteVertex v = {};
    v.depth = depth;
    v.view_depth = view_depth;
    v.atlas_x = static_cast<float>(ax); v.atlas_y = static_cast<float>(ay);
    v.xmap_off = static_cast<float>(op.w); v.ymap_off = static_cast<float>(op.h);
    v.lut_row = factor;
    // 8: coverage, sampled filtered (soft shadows); the index atlas's layer from bit 65536 up.
    v.mode = static_cast<float>(WFS_SHADOW | (op.coverage ? 8u : 0u) | (static_cast<uint32_t>(layer) << 16));
    out[0].mode = WFS_SHADOW;
    out[0].first = static_cast<uint32_t>(m_vertices.size());
    out[0].count = 3;
    for (int i = 0; i < 3; ++i)
    {
        v.x = static_cast<float>(op.x[i]) + off_x;
        v.y = static_cast<float>(op.y[i]) + off_y;
        v.lx = op.u[i];
        v.ly = op.v[i];
        m_vertices.push_back(v);
    }
    return 1;
}

// A creature shadow from its frame's own atlas entry (WF_OP_SHADOW_SPRITE): the shader maps each pixel's shadow-buffer
// coordinate to the frame's texel. Mode bits: 6, 8 soft, 16 "from a frame", the HD layer from bit 256 up or the index
// atlas's from bit 65536 up; aux = the frame's width, height, its HD texels' scale (0: the index atlas), mirrored.
int GpuSpriteRenderer::AddShadowSprite(const WorldFrameShadowSpriteOp& op, float off_x, float off_y, float depth, DrawCall out[1])
{
    if (m_device == nullptr || m_frame == nullptr || op.rle == nullptr || op.src_w <= 0 || op.src_h <= 0)
        return 0;
    int32_t ax = 0, ay = 0, layer = 0;
    uint32_t bits = WFS_SHADOW | 16u | (op.soft ? 8u : 0u);
    int32_t hd_scale = 0;
    if (op.soft && op.hd_texels != nullptr && op.hd_scale >= 1
        && resolve_hd(op.hd_texels, nullptr, op.hd_id, op.hd_w, op.hd_h, op.hd_stride, false, ax, ay, layer))
    {
        hd_scale = op.hd_scale;
        bits |= static_cast<uint32_t>(layer) << 8;
    }
    else if (resolve(op.rle, op.src_w, op.src_h, ax, ay, layer))
    {
        bits |= static_cast<uint32_t>(layer) << 16;
    }
    else
    {
        RPROF_COUNT(RPC_SKIPPED, 1);
        return 0;
    }
    const float shade = static_cast<float>(op.shade);
    const float factor = ((shade <= 31.0f) ? shade : (3.0f * shade - 64.0f)) / 32.0f;
    SpriteVertex v = {};
    v.depth = depth;
    v.view_depth = depth;
    v.atlas_x = static_cast<float>(ax); v.atlas_y = static_cast<float>(ay);
    v.xmap_off = static_cast<float>(op.base_x); v.ymap_off = static_cast<float>(op.base_y);
    v.lut_row = factor;
    v.mode = static_cast<float>(bits);
    v.aux[0] = static_cast<float>(op.src_w);
    v.aux[1] = static_cast<float>(op.src_h);
    v.aux[2] = static_cast<float>(hd_scale);
    v.aux[3] = op.flip ? 1.0f : 0.0f;
    out[0].mode = WFS_SHADOW;
    out[0].first = static_cast<uint32_t>(m_vertices.size());
    out[0].count = 6;
    static const int corner[6] = { 0, 1, 2, 0, 2, 3 }; // the CPU's triangles: P1 P2 P3 and P1 P3 P4
    for (int i : corner)
    {
        v.x = static_cast<float>(op.x[i]) + off_x;
        v.y = static_cast<float>(op.y[i]) + off_y;
        v.lx = op.u[i];
        v.ly = op.v[i];
        m_vertices.push_back(v);
    }
    return 1;
}

int GpuSpriteRenderer::AddOp(const WorldFrameSpriteOp& op, float off_x, float off_y, float depth, float view_depth, DrawCall out[2])
{
    if (m_device == nullptr || m_frame == nullptr)
        return 0;
    const int64_t cap = static_cast<int64_t>(kLookupWidth) * kLookupHeight;
    if (static_cast<int64_t>(op.xmap_off) + op.dst_w > m_frame->lookup_count || static_cast<int64_t>(op.ymap_off) + op.dst_h > m_frame->lookup_count ||
        static_cast<int64_t>(op.xmap_off) + op.dst_w > cap || static_cast<int64_t>(op.ymap_off) + op.dst_h > cap)
        return 0;
    int32_t ax, ay, hd_layer = 0, index_layer = 0;
    // HD: bit 8 of the shader's mode, bit 16 to apply the transform table (a curve row is never row 0), the atlas
    // layer from bit 256 up. An HD sprite
    // whose texels can't be placed (the HD atlas full) draws its original: the shader divides the lookups' HD
    // coordinates by the scale in bits 32..224. An original's index atlas layer is from bit 65536 up.
    uint32_t hd_bits = 0;
    uint32_t row = op.lut_row;
    uint32_t rgba = op.rgba;
    if (op.hd_texels != nullptr)
    {
        if ((op.lut_row != 0) && (op.lut_row < static_cast<uint32_t>(kLutRows))
            && resolve_hd(op.hd_texels, op.hd_sub_texels, op.hd_id, op.hd_w, op.hd_h, op.hd_stride, op.mode == WFS_ALPHA, ax, ay, hd_layer))
        {
            // TEXTURE_FILTER (the shader: 0 Nearest, 1 Sharp, 2 Bilinear, 3 Trilinear = Bilinear, the level being
            // chosen per draw) from bit 16384 up, above the layer; parity mode draws nearest.
            const uint32_t filter = (op.hd_table & HD_OP_NEAREST) ? 0u : static_cast<uint32_t>(RendererGetTextureFilter()) & 3u;
            hd_bits = 8u | ((op.hd_table & HD_OP_TABLE) ? 16u : 0u) | (static_cast<uint32_t>(hd_layer) << 8) | (filter << 14);
            if ((op.hd_table & HD_OP_TABLE) && (op.mode != WFS_ALPHA))
                m_transform_needed = true;
            if (op.mode == WFS_ALPHA)
                rgba = static_cast<uint32_t>(op.hd_w + 2); // where the sub texels start, for the shader's subtract pass
        }
        else if ((op.rle != nullptr) && (op.hd_scale >= 1) && (op.hd_scale <= 7) && resolve(op.rle, op.src_w, op.src_h, ax, ay, index_layer))
        {
            hd_bits = (op.hd_scale << 5) | (static_cast<uint32_t>(index_layer) << 16);
            row = op.index_lut_row;
        }
        else
        {
            RPROF_COUNT(RPC_SKIPPED, 1);
            return 0;
        }
    }
    else if (resolve(op.rle, op.src_w, op.src_h, ax, ay, index_layer))
        hd_bits = static_cast<uint32_t>(index_layer) << 16;
    else
    {
        RPROF_COUNT(RPC_SKIPPED, 1);
        return 0;
    }

    const uint32_t lut_row = (row < static_cast<uint32_t>(kLutRows)) ? row : 0;
    const int quads = (op.mode == WFS_ALPHA) ? 2 : 1;
    const float x0 = static_cast<float>(op.dst_x) + off_x, y0 = static_cast<float>(op.dst_y) + off_y;
    const float x1 = x0 + op.dst_w, y1 = y0 + op.dst_h;
    for (int q = 0; q < quads; ++q)
    {
        SpriteVertex v = {};
        v.depth = depth;
        v.view_depth = view_depth;
        v.atlas_x = static_cast<float>(ax); v.atlas_y = static_cast<float>(ay);
        v.xmap_off = static_cast<float>(op.xmap_off); v.ymap_off = static_cast<float>(op.ymap_off);
        v.lut_row = static_cast<float>(lut_row);
        const uint32_t pipeline_mode = (op.mode == WFS_ALPHA) ? (q == 0 ? static_cast<uint32_t>(WFS_ALPHA) : 4u) : op.mode;
        v.mode = static_cast<float>(pipeline_mode | hd_bits);
        v.rgba[0] = rgba & 0xFF; v.rgba[1] = (rgba >> 8) & 0xFF; v.rgba[2] = (rgba >> 16) & 0xFF; v.rgba[3] = (rgba >> 24) & 0xFF;
        const float xs[6] = { x0, x1, x0, x1, x1, x0 };
        const float ys[6] = { y0, y0, y1, y0, y1, y1 };
        const float ls[6] = { 0, static_cast<float>(op.dst_w), 0, static_cast<float>(op.dst_w), static_cast<float>(op.dst_w), 0 };
        const float ts[6] = { 0, 0, static_cast<float>(op.dst_h), 0, static_cast<float>(op.dst_h), static_cast<float>(op.dst_h) };
        out[q].mode = pipeline_mode;
        out[q].first = static_cast<uint32_t>(m_vertices.size());
        out[q].count = 6;
        for (int i = 0; i < 6; ++i)
        {
            v.x = xs[i]; v.y = ys[i]; v.lx = ls[i]; v.ly = ts[i];
            m_vertices.push_back(v);
        }
    }
    return quads;
}

void GpuSpriteRenderer::Upload(SDL_GPUCopyPass* copy)
{
    if (m_vertices.empty() || m_frame == nullptr)
        return;

    // A grown atlas gets the old one's layers first, then the newly seen texels; the HD transform table.
    copy_grown_atlas(copy, m_index_atlas);
    copy_grown_atlas(copy, m_hd_atlas);
    const uint64_t evictions = m_index.Evictions() + m_hd.Evictions();
    RPROF_COUNT(RPC_ATLAS_CLEARS, evictions - m_evictions_counted);
    m_evictions_counted = evictions;
    log_evictions(m_index_atlas, m_index.Evictions());
    log_evictions(m_hd_atlas, m_hd.Evictions());
    if (!m_hd_pending.empty())
    {
        unsigned char* hmap = static_cast<unsigned char*>(SDL_MapGPUTransferBuffer(m_device, m_hd_staging, true));
        if (hmap != nullptr)
        {
            std::memcpy(hmap, m_hd_pending_pixels.data(), m_hd_pending_pixels.size() * 4);
            SDL_UnmapGPUTransferBuffer(m_device, m_hd_staging);
            for (const PendingUpload& p : m_hd_pending)
            {
                SDL_GPUTextureTransferInfo src = {};
                src.transfer_buffer = m_hd_staging;
                src.offset = p.staging_offset;
                src.pixels_per_row = p.w;
                src.rows_per_layer = p.h;
                SDL_GPUTextureRegion dst = {};
                dst.texture = m_hd_atlas.tex;
                dst.layer = static_cast<Uint32>(p.layer);
                dst.x = p.x; dst.y = p.y; dst.w = p.w; dst.h = p.h; dst.d = 1;
                SDL_UploadToGPUTexture(copy, &src, &dst, false);
            }
        }
    }
    upload_transform_table(copy);

    // Vertices.
    const uint32_t vcount = static_cast<uint32_t>(m_vertices.size());
    RPROF_COUNT(RPC_SPRITE_QUADS, vcount);
    RPROF_COUNT(RPC_ATLAS_BYTES, m_pending_pixels.size());
    RPROF_COUNT(RPC_LOOKUP_ENTRIES, m_frame->lookup_count);
    if (m_vertex_buffer == nullptr || m_vertex_capacity < vcount)
    {
        if (m_vertex_buffer != nullptr) SDL_ReleaseGPUBuffer(m_device, m_vertex_buffer);
        if (m_vertex_staging != nullptr) SDL_ReleaseGPUTransferBuffer(m_device, m_vertex_staging);
        m_vertex_capacity = vcount * 2;
        SDL_GPUBufferCreateInfo bi = {};
        bi.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
        bi.size = m_vertex_capacity * sizeof(SpriteVertex);
        m_vertex_buffer = SDL_CreateGPUBuffer(m_device, &bi);
        SDL_GPUTransferBufferCreateInfo ti = {};
        ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        ti.size = bi.size;
        m_vertex_staging = SDL_CreateGPUTransferBuffer(m_device, &ti);
    }
    if (m_vertex_buffer == nullptr || m_vertex_staging == nullptr)
        return;
    void* vmap = SDL_MapGPUTransferBuffer(m_device, m_vertex_staging, true);
    if (vmap != nullptr)
    {
        std::memcpy(vmap, m_vertices.data(), vcount * sizeof(SpriteVertex));
        SDL_UnmapGPUTransferBuffer(m_device, m_vertex_staging);
        SDL_GPUTransferBufferLocation src = {};
        src.transfer_buffer = m_vertex_staging;
        SDL_GPUBufferRegion dst = {};
        dst.buffer = m_vertex_buffer;
        dst.size = vcount * static_cast<Uint32>(sizeof(SpriteVertex));
        SDL_UploadToGPUBuffer(copy, &src, &dst, true);
    }

    // Newly seen / changed sprites.
    if (!m_pending.empty())
    {
        unsigned char* amap = static_cast<unsigned char*>(SDL_MapGPUTransferBuffer(m_device, m_atlas_staging, true));
        if (amap != nullptr)
        {
            std::memcpy(amap, m_pending_pixels.data(), m_pending_pixels.size());
            SDL_UnmapGPUTransferBuffer(m_device, m_atlas_staging);
            for (const PendingUpload& p : m_pending)
            {
                SDL_GPUTextureTransferInfo src = {};
                src.transfer_buffer = m_atlas_staging;
                src.offset = p.staging_offset;
                src.pixels_per_row = p.w;
                src.rows_per_layer = p.h;
                SDL_GPUTextureRegion dst = {};
                dst.texture = m_index_atlas.tex;
                dst.layer = static_cast<Uint32>(p.layer);
                dst.x = p.x; dst.y = p.y; dst.w = p.w; dst.h = p.h; dst.d = 1;
                SDL_UploadToGPUTexture(copy, &src, &dst, false);
            }
        }
    }

    // Scaling lookups: the frame's whole table (sprites index into it).
    if (m_frame->lookup_count > 0)
    {
        const int64_t cap = static_cast<int64_t>(kLookupWidth) * kLookupHeight;
        const uint32_t count = static_cast<uint32_t>(m_frame->lookup_count < cap ? m_frame->lookup_count : cap);
        const uint32_t rows = (count + kLookupWidth - 1) / kLookupWidth;
        unsigned char* lmap = static_cast<unsigned char*>(SDL_MapGPUTransferBuffer(m_device, m_lookup_staging, true));
        if (lmap != nullptr)
        {
            std::memcpy(lmap, m_frame->lookup, count * sizeof(uint16_t));
            std::memset(lmap + count * sizeof(uint16_t), 0xFF, static_cast<size_t>(rows) * kLookupWidth * 2 - count * sizeof(uint16_t));
            SDL_UnmapGPUTransferBuffer(m_device, m_lookup_staging);
            SDL_GPUTextureTransferInfo src = {};
            src.transfer_buffer = m_lookup_staging;
            src.pixels_per_row = kLookupWidth;
            src.rows_per_layer = rows;
            SDL_GPUTextureRegion dst = {};
            dst.texture = m_lookup;
            dst.w = kLookupWidth; dst.h = rows; dst.d = 1;
            SDL_UploadToGPUTexture(copy, &src, &dst, false);
        }
    }

    // Colour tables: row 0 = the active palette, then the frame's remap tables.
    uint32_t* lut = static_cast<uint32_t*>(SDL_MapGPUTransferBuffer(m_device, m_lut_staging, true));
    if (lut != nullptr)
    {
        const unsigned char* palette = RendererGetActivePalette();
        for (int i = 0; i < 256; ++i)
        {
            const TbPixel px = expand_indexed_pixel(static_cast<uint8_t>(i), palette);
            lut[i] = static_cast<uint32_t>(px.r) | (static_cast<uint32_t>(px.g) << 8) | (static_cast<uint32_t>(px.b) << 16) | (static_cast<uint32_t>(px.a) << 24);
        }
        int64_t rows = m_frame->lut_rows < kLutRows - 1 ? m_frame->lut_rows : kLutRows - 1;
        if (rows > 0)
            std::memcpy(lut + 256, m_frame->lut, static_cast<size_t>(rows) * 256 * sizeof(uint32_t));
        SDL_UnmapGPUTransferBuffer(m_device, m_lut_staging);
        SDL_GPUTextureTransferInfo src = {};
        src.transfer_buffer = m_lut_staging;
        src.pixels_per_row = 256;
        src.rows_per_layer = static_cast<Uint32>(rows + 1);
        SDL_GPUTextureRegion dst = {};
        dst.texture = m_lut;
        dst.w = 256; dst.h = static_cast<Uint32>(rows + 1); dst.d = 1;
        SDL_UploadToGPUTexture(copy, &src, &dst, false);
    }
}

void GpuSpriteRenderer::Draw(SDL_GPURenderPass* pass, const DrawCall& d)
{
    if (d.mode > 7 || m_vertex_buffer == nullptr || m_pipelines[d.mode] == nullptr)
        return;
    SDL_BindGPUGraphicsPipeline(pass, m_pipelines[d.mode]);
    SDL_GPUBufferBinding vb = {};
    vb.buffer = m_vertex_buffer;
    SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
    SDL_GPUTextureSamplerBinding tex[6] = {};
    tex[0].texture = m_index_atlas.tex; tex[0].sampler = m_sampler;
    tex[1].texture = m_lut;    tex[1].sampler = m_sampler;
    tex[2].texture = m_lookup; tex[2].sampler = m_sampler;
    tex[3].texture = m_light_grid; tex[3].sampler = m_light_sampler;
    tex[4].texture = m_hd_atlas.tex; tex[4].sampler = m_sampler;
    tex[5].texture = m_transform_tex; tex[5].sampler = m_sampler;
    SDL_BindGPUFragmentSamplers(pass, 0, tex, 6);
    if (d.mode == WFS_GHOST1)
        SDL_SetGPUBlendConstants(pass, SDL_FColor{ 2.0f / 3.0f, 2.0f / 3.0f, 2.0f / 3.0f, 1.0f });
    else if (d.mode == WFS_GHOST2)
        SDL_SetGPUBlendConstants(pass, SDL_FColor{ 1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f, 1.0f });
    SDL_DrawGPUPrimitives(pass, d.count, 1, d.first, 0);
}
