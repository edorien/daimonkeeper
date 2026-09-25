#include "pre_inc.h"
#include "renderer/GpuSpriteRenderer.h"
#include "renderer/RendererProfile.h"
#include "renderer/RendererManager.h" // RendererGetActivePalette
#include "bflib_video.h"              // expand_indexed_pixel
#include <SDL3/SDL_gpu.h>
#include "renderer/shaders/world_sprite.vert.spv.h"
#include "renderer/shaders/world_sprite.frag.spv.h"
#include "post_inc.h"

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
    fs_info.num_samplers = 4; // atlas, lut, lookup, light grid
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
    SDL_GPUVertexAttribute attrs[7] = {};
    attrs[0].location = 0; attrs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;      attrs[0].offset = offsetof(SpriteVertex, x);
    attrs[1].location = 1; attrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;      attrs[1].offset = offsetof(SpriteVertex, lx);
    attrs[2].location = 2; attrs[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;      attrs[2].offset = offsetof(SpriteVertex, atlas_x);
    attrs[3].location = 3; attrs[3].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;      attrs[3].offset = offsetof(SpriteVertex, lut_row);
    attrs[4].location = 4; attrs[4].format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM; attrs[4].offset = offsetof(SpriteVertex, rgba);
    attrs[5].location = 5; attrs[5].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT;       attrs[5].offset = offsetof(SpriteVertex, depth);
    attrs[6].location = 6; attrs[6].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT;       attrs[6].offset = offsetof(SpriteVertex, view_depth);

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
        pi.vertex_input_state.num_vertex_attributes = 7;
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

    static const int32_t sizes[] = { 4096, 2048 };
    for (int32_t size : sizes)
    {
        SDL_GPUTextureCreateInfo ti = {};
        ti.type = SDL_GPU_TEXTURETYPE_2D;
        ti.format = SDL_GPU_TEXTUREFORMAT_R8_UNORM;
        ti.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        ti.width = size; ti.height = size;
        ti.layer_count_or_depth = 1; ti.num_levels = 1;
        ti.sample_count = SDL_GPU_SAMPLECOUNT_1;
        m_atlas = SDL_CreateGPUTexture(m_device, &ti);
        if (m_atlas != nullptr) { m_atlas_size = size; break; }
    }
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
    if (m_atlas == nullptr || m_lut == nullptr || m_lookup == nullptr)
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
    if (m_atlas_staging == nullptr || m_lut_staging == nullptr || m_lookup_staging == nullptr)
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
    if (m_atlas != nullptr) SDL_ReleaseGPUTexture(m_device, m_atlas);
    if (m_lut != nullptr) SDL_ReleaseGPUTexture(m_device, m_lut);
    if (m_lookup != nullptr) SDL_ReleaseGPUTexture(m_device, m_lookup);
    if (m_sampler != nullptr) SDL_ReleaseGPUSampler(m_device, m_sampler);
    for (SDL_GPUGraphicsPipeline*& p : m_pipelines)
        if (p != nullptr) { SDL_ReleaseGPUGraphicsPipeline(m_device, p); p = nullptr; }
    if (m_vs != nullptr) SDL_ReleaseGPUShader(m_device, m_vs);
    if (m_fs != nullptr) SDL_ReleaseGPUShader(m_device, m_fs);
    m_vertex_buffer = nullptr; m_vertex_staging = nullptr; m_atlas_staging = nullptr;
    m_lut_staging = nullptr; m_lookup_staging = nullptr; m_atlas = nullptr; m_lut = nullptr;
    m_lookup = nullptr; m_sampler = nullptr; m_vs = nullptr; m_fs = nullptr;
    m_entries.clear();
    m_mask_entries.clear();
    m_device = nullptr;
}

void GpuSpriteRenderer::BeginFrame(const WorldFrame& frame)
{
    ++m_frame_id;
    m_frame = &frame;
    m_vertices.clear();
    m_pending_pixels.clear();
    m_pending.clear();
    m_bound_pipeline = -1;
    if (m_atlas_reset_pending)
    {
        // Last frame ran out of atlas: start over (visible sprites re-upload on demand).
        m_entries.clear();
        m_mask_entries.clear();
        m_cursor_x = m_cursor_y = m_shelf_h = 0;
        m_atlas_full = false;
        m_atlas_reset_pending = false;
    }
}

bool GpuSpriteRenderer::resolve(const WorldFrameSpriteOp& op, int32_t& ax, int32_t& ay)
{
    const Key key = { op.rle, op.src_w, op.src_h };
    auto it = m_entries.find(key);
    if (it != m_entries.end() && it->second.frame_seen == m_frame_id)
    {
        ax = it->second.x; ay = it->second.y;
        return true;
    }

    size_t consumed = 0;
    const uint64_t hash = hash_rle(op.rle, op.src_h, consumed);
    if (it != m_entries.end() && it->second.hash == hash)
    {
        it->second.frame_seen = m_frame_id;
        ax = it->second.x; ay = it->second.y;
        return true;
    }

    const size_t bytes = static_cast<size_t>(op.src_w) * op.src_h;
    if (m_pending_pixels.size() + bytes > m_atlas_staging_capacity)
        return false; // this frame's upload budget is spent; the sprite is skipped this frame

    int32_t x, y;
    if (it != m_entries.end())
    {
        x = it->second.x; y = it->second.y; // same size, same rect: re-decode in place
    }
    else
    {
        if (!alloc_rect(op.src_w, op.src_h, x, y))
            return false;
        it = m_entries.emplace(key, Entry()).first;
        it->second.x = x; it->second.y = y;
    }
    it->second.hash = hash;
    it->second.frame_seen = m_frame_id;

    PendingUpload up;
    up.staging_offset = static_cast<uint32_t>(m_pending_pixels.size());
    up.x = x; up.y = y; up.w = op.src_w; up.h = op.src_h;
    m_pending_pixels.resize(m_pending_pixels.size() + bytes);
    decode_rle(op.rle, op.src_w, op.src_h, m_pending_pixels.data() + up.staging_offset);
    m_pending.push_back(up);
    ax = x; ay = y;
    return true;
}

bool GpuSpriteRenderer::alloc_rect(int32_t w, int32_t h, int32_t& x, int32_t& y)
{
    if (m_atlas_full || w > m_atlas_size || h > m_atlas_size)
        return false;
    if (m_cursor_x + w > m_atlas_size)
    {
        m_cursor_y += m_shelf_h; m_cursor_x = 0; m_shelf_h = 0;
    }
    if (m_cursor_y + h > m_atlas_size)
    {
        m_atlas_full = true;
        m_atlas_reset_pending = true;
        WARNLOG("GpuSpriteRenderer: sprite atlas full; clearing it next frame");
        return false;
    }
    x = m_cursor_x; y = m_cursor_y;
    m_cursor_x += w;
    if (h > m_shelf_h) m_shelf_h = h;
    return true;
}

bool GpuSpriteRenderer::resolve_mask(const WorldFrameShadowOp& op, const unsigned char* pixels, int32_t& ax, int32_t& ay)
{
    auto it = m_mask_entries.find(op.hash);
    if (it != m_mask_entries.end())
    {
        it->second.frame_seen = m_frame_id;
        ax = it->second.x; ay = it->second.y;
        return true;
    }
    const size_t bytes = static_cast<size_t>(op.w) * op.h;
    if (m_pending_pixels.size() + bytes > m_atlas_staging_capacity)
        return false;
    int32_t x, y;
    if (!alloc_rect(op.w, op.h, x, y))
        return false;
    Entry e;
    e.x = x; e.y = y; e.hash = op.hash; e.frame_seen = m_frame_id;
    m_mask_entries.emplace(op.hash, e);

    PendingUpload up;
    up.staging_offset = static_cast<uint32_t>(m_pending_pixels.size());
    up.x = x; up.y = y; up.w = op.w; up.h = op.h;
    m_pending_pixels.insert(m_pending_pixels.end(), pixels, pixels + bytes);
    m_pending.push_back(up);
    ax = x; ay = y;
    return true;
}

int GpuSpriteRenderer::AddShadow(const WorldFrameShadowOp& op, const unsigned char* pixels, float off_x, float off_y, float depth, float view_depth, DrawCall out[1])
{
    if (m_device == nullptr || m_frame == nullptr || op.w <= 0 || op.h <= 0)
        return 0;
    int32_t ax, ay;
    if (!resolve_mask(op, pixels, ax, ay))
        return 0;
    // render_shade()'s factor: linear darken up to 31, non-linear brighten beyond.
    const float shade = static_cast<float>(op.shade);
    const float factor = ((shade <= 31.0f) ? shade : (3.0f * shade - 64.0f)) / 32.0f;
    SpriteVertex v = {};
    v.depth = depth;
    v.view_depth = view_depth;
    v.atlas_x = static_cast<float>(ax); v.atlas_y = static_cast<float>(ay);
    v.xmap_off = static_cast<float>(op.w); v.ymap_off = static_cast<float>(op.h);
    v.lut_row = factor;
    v.mode = static_cast<float>(WFS_SHADOW);
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

int GpuSpriteRenderer::AddOp(const WorldFrameSpriteOp& op, float off_x, float off_y, float depth, float view_depth, DrawCall out[2])
{
    if (m_device == nullptr || m_frame == nullptr)
        return 0;
    const int64_t cap = static_cast<int64_t>(kLookupWidth) * kLookupHeight;
    if (static_cast<int64_t>(op.xmap_off) + op.dst_w > m_frame->lookup_count || static_cast<int64_t>(op.ymap_off) + op.dst_h > m_frame->lookup_count ||
        static_cast<int64_t>(op.xmap_off) + op.dst_w > cap || static_cast<int64_t>(op.ymap_off) + op.dst_h > cap)
        return 0;
    int32_t ax, ay;
    if (!resolve(op, ax, ay))
        return 0;

    const uint32_t lut_row = (op.lut_row < static_cast<uint32_t>(kLutRows)) ? op.lut_row : 0;
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
        v.mode = static_cast<float>((op.mode == WFS_ALPHA) ? (q == 0 ? static_cast<uint32_t>(WFS_ALPHA) : 4u) : op.mode);
        v.rgba[0] = op.rgba & 0xFF; v.rgba[1] = (op.rgba >> 8) & 0xFF; v.rgba[2] = (op.rgba >> 16) & 0xFF; v.rgba[3] = (op.rgba >> 24) & 0xFF;
        const float xs[6] = { x0, x1, x0, x1, x1, x0 };
        const float ys[6] = { y0, y0, y1, y0, y1, y1 };
        const float ls[6] = { 0, static_cast<float>(op.dst_w), 0, static_cast<float>(op.dst_w), static_cast<float>(op.dst_w), 0 };
        const float ts[6] = { 0, 0, static_cast<float>(op.dst_h), 0, static_cast<float>(op.dst_h), static_cast<float>(op.dst_h) };
        out[q].mode = static_cast<uint32_t>(v.mode);
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
                dst.texture = m_atlas;
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
    SDL_GPUTextureSamplerBinding tex[4] = {};
    tex[0].texture = m_atlas;  tex[0].sampler = m_sampler;
    tex[1].texture = m_lut;    tex[1].sampler = m_sampler;
    tex[2].texture = m_lookup; tex[2].sampler = m_sampler;
    tex[3].texture = m_light_grid; tex[3].sampler = m_light_sampler;
    SDL_BindGPUFragmentSamplers(pass, 0, tex, 4);
    if (d.mode == WFS_GHOST1)
        SDL_SetGPUBlendConstants(pass, SDL_FColor{ 2.0f / 3.0f, 2.0f / 3.0f, 2.0f / 3.0f, 1.0f });
    else if (d.mode == WFS_GHOST2)
        SDL_SetGPUBlendConstants(pass, SDL_FColor{ 1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f, 1.0f });
    SDL_DrawGPUPrimitives(pass, d.count, 1, d.first, 0);
}
