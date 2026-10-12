#include "pre_inc.h"
#include "renderer/RendererGpu3D.h"
#include "renderer/RendererProfile.h"
#include "bflib_video.h"       // lbWindow, lbDrawSurface
#include "bflib_vidsurface.h"  // lbDrawSurface
#include "renderer/RendererManager.h" // RendererGetActivePalette
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_hints.h>
#include "renderer/shaders/world_polygon.vert.spv.h"
#include "renderer/shaders/world_polygon.frag.spv.h"
#include "post_inc.h"

#include <vector>

namespace {

// Matches the vertex_attributes layout below exactly -- converted from
// struct PolyPoint's int64_t fields (WorldFramePolyItem) since SDL_GPU
// vertex buffers want plain float attributes, not PolyPoint's fixed-width
// integer layout. See world_polygon.vert.glsl for the shader side.
struct GpuVertex {
    float x, y;
    float u, v;
    float shade;
    float layer;
    float depth; // 0 near .. 1 far -- WorldFrameOp::depth (visibility)
    float zpos;  // hyperbolic view depth for per-pixel lighting (PolyPoint::Z / WorldFrameOp::view_depth)
    float uw, vw; // u, v in whole texels (the CPU's 32-res truncation): an HD frame samples the original 32x32 level
                  // with these where a frame without HD would (world_polygon.frag)
};

} // namespace

// docs/refactor/renderer/gpu-v2/02-graphics-api-choice.md: force Vulkan as
// the SDL_GPU backend on every platform, not just take Linux's default --
// see that section for the shader-delivery and cross-platform-consistency
// reasoning. SDL_HINT_GPU_DRIVER is read by SDL_GPUSelectBackend() at
// device-creation time (SDL_gpu.c), so this must be set before
// SDL_CreateGPUDeviceWithProperties() below.
bool RendererGpu3D::init_gpu_device()
{
    SDL_SetHint(SDL_HINT_GPU_DRIVER, "vulkan");

    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true);
    // GPU_DEBUG option (RendererSetGpuDebug()): Vulkan validation layers.
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, RendererGetGpuDebug() != 0);
    m_device = SDL_CreateGPUDeviceWithProperties(props);
    SDL_DestroyProperties(props);

    if (m_device == nullptr)
    {
        ERRORLOG("SDL_CreateGPUDevice (vulkan) failed: %s", SDL_GetError());
        // Commonest cause seen so far: a driver package (AMDGPU) shadowing the
        // system libwayland-client, which breaks every Mesa Vulkan driver.
        WARNLOG("If `vulkaninfo` fails with 'undefined symbol: wl_fixes_interface', launch via scripts/run-keeperfx-vulkan.sh");
        return false;
    }
    SYNCLOG("RendererGpu3D: SDL_GPU device ready (%s)",
        SDL_GetStringProperty(SDL_GetGPUDeviceProperties(m_device), SDL_PROP_GPU_DEVICE_NAME_STRING, "?"));
    return true;
}

bool RendererGpu3D::init_pipeline()
{
    SDL_GPUShaderCreateInfo vs_info = {};
    vs_info.code = reinterpret_cast<const Uint8*>(g_world_polygon_vert_spv);
    vs_info.code_size = g_world_polygon_vert_spv_size;
    vs_info.entrypoint = "main";
    vs_info.format = SDL_GPU_SHADERFORMAT_SPIRV;
    vs_info.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    vs_info.num_uniform_buffers = 1; // set=1 binding=0: ViewportUniforms
    m_vertex_shader = SDL_CreateGPUShader(m_device, &vs_info);
    if (m_vertex_shader == nullptr)
    {
        ERRORLOG("SDL_CreateGPUShader (vertex) failed: %s", SDL_GetError());
        return false;
    }

    SDL_GPUShaderCreateInfo fs_info = {};
    fs_info.code = reinterpret_cast<const Uint8*>(g_world_polygon_frag_spv);
    fs_info.code_size = g_world_polygon_frag_spv_size;
    fs_info.entrypoint = "main";
    fs_info.format = SDL_GPU_SHADERFORMAT_SPIRV;
    fs_info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    fs_info.num_samplers = 4; // set=2: 0 u_block_textures (2D array), 1 u_light_grid, 2 u_transform_curves, 3 u_transform_table
    fs_info.num_uniform_buffers = 2; // set=3: binding 0 Lighting (per-pixel lighting pass), binding 1 Transform (palette effects)
    m_fragment_shader = SDL_CreateGPUShader(m_device, &fs_info);
    if (m_fragment_shader == nullptr)
    {
        ERRORLOG("SDL_CreateGPUShader (fragment) failed: %s", SDL_GetError());
        return false;
    }

    SDL_GPUVertexBufferDescription vb_desc = {};
    vb_desc.slot = 0;
    vb_desc.pitch = sizeof(GpuVertex);
    vb_desc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    vb_desc.instance_step_rate = 0;

    SDL_GPUVertexAttribute attrs[7] = {};
    attrs[0].location = 0;
    attrs[0].buffer_slot = 0;
    attrs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    attrs[0].offset = offsetof(GpuVertex, x);
    attrs[1].location = 1;
    attrs[1].buffer_slot = 0;
    attrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    attrs[1].offset = offsetof(GpuVertex, u);
    attrs[2].location = 2;
    attrs[2].buffer_slot = 0;
    attrs[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT;
    attrs[2].offset = offsetof(GpuVertex, shade);
    attrs[3].location = 3;
    attrs[3].buffer_slot = 0;
    attrs[3].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT;
    attrs[3].offset = offsetof(GpuVertex, layer);
    attrs[4].location = 4;
    attrs[4].buffer_slot = 0;
    attrs[4].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT;
    attrs[4].offset = offsetof(GpuVertex, depth);
    attrs[5].location = 5;
    attrs[5].buffer_slot = 0;
    attrs[5].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT;
    attrs[5].offset = offsetof(GpuVertex, zpos);
    attrs[6].location = 6;
    attrs[6].buffer_slot = 0;
    attrs[6].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    attrs[6].offset = offsetof(GpuVertex, uw);

    SDL_GPUColorTargetDescription color_target = {};
    color_target.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    color_target.blend_state.enable_blend = true;
    color_target.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    color_target.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    color_target.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    color_target.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    color_target.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
    color_target.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;

    SDL_GPUGraphicsPipelineCreateInfo pipe_info = {};
    pipe_info.vertex_shader = m_vertex_shader;
    pipe_info.fragment_shader = m_fragment_shader;
    pipe_info.vertex_input_state.vertex_buffer_descriptions = &vb_desc;
    pipe_info.vertex_input_state.num_vertex_buffers = 1;
    pipe_info.vertex_input_state.vertex_attributes = attrs;
    pipe_info.vertex_input_state.num_vertex_attributes = 7;
    pipe_info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipe_info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pipe_info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE; // bucket order, no culling yet -- 04's "no depth buffer" scope
    pipe_info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    pipe_info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    pipe_info.target_info.color_target_descriptions = &color_target;
    pipe_info.target_info.num_color_targets = 1;
    // gpu-v2 Phase C.5: depth infrastructure -- see WorldFrameOp::depth.
    pipe_info.depth_stencil_state.enable_depth_test = true;
    pipe_info.depth_stencil_state.enable_depth_write = true;
    pipe_info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    pipe_info.target_info.has_depth_stencil_target = true;
    pipe_info.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;

    m_pipeline = SDL_CreateGPUGraphicsPipeline(m_device, &pipe_info);
    if (m_pipeline == nullptr)
    {
        ERRORLOG("SDL_CreateGPUGraphicsPipeline failed: %s", SDL_GetError());
        return false;
    }

    SDL_GPUSamplerCreateInfo sampler_info = {};
    sampler_info.min_filter = SDL_GPU_FILTER_NEAREST; // matches the CPU rasterizer's unfiltered indexed sampling
    sampler_info.mag_filter = SDL_GPU_FILTER_NEAREST;
    sampler_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sampler_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT; // reproduces TEXTURE_UV_WRAP_MASK's wrap (bflib_render_gpoly.c)
    sampler_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    sampler_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    // Mip levels (texture resolution Phase 4): the hardware picks per pixel among what the array has.
    sampler_info.min_lod = 0.0f;
    sampler_info.max_lod = 1000.0f;
    m_sampler = SDL_CreateGPUSampler(m_device, &sampler_info);
    // TEXTURE_FILTER Sharp/Bilinear: linear, clamped at the block's edge, or repeating for scrolled polygons.
    sampler_info.min_filter = SDL_GPU_FILTER_LINEAR;
    sampler_info.mag_filter = SDL_GPU_FILTER_LINEAR;
    m_sampler_linear_repeat = SDL_CreateGPUSampler(m_device, &sampler_info);
    sampler_info.address_mode_u = sampler_info.address_mode_v = sampler_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    m_sampler_linear_clamp = SDL_CreateGPUSampler(m_device, &sampler_info);
    sampler_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    m_sampler_trilinear_clamp = SDL_CreateGPUSampler(m_device, &sampler_info);
    sampler_info.address_mode_u = sampler_info.address_mode_v = sampler_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    m_sampler_trilinear_repeat = SDL_CreateGPUSampler(m_device, &sampler_info);
    if (m_sampler == nullptr || m_sampler_linear_repeat == nullptr || m_sampler_linear_clamp == nullptr ||
        m_sampler_trilinear_repeat == nullptr || m_sampler_trilinear_clamp == nullptr)
    {
        ERRORLOG("SDL_CreateGPUSampler failed: %s", SDL_GetError());
        return false;
    }
    return true;
}

uint32_t RendererGpu3D::layer_bytes() const
{
    uint32_t bytes = 0;
    for (uint32_t l = 0; l < m_block_levels; ++l)
        bytes += (m_block_side >> l) * (m_block_side >> l) * 4;
    return bytes;
}

// Copies one staged layer (all its levels, one after another in the ring) to its place in the array.
void RendererGpu3D::copy_layer_levels(SDL_GPUCopyPass* copy, uint32_t ring_slot, uint32_t layer)
{
    Uint32 offset = ring_slot * layer_bytes();
    for (uint32_t l = 0; l < m_block_levels; ++l)
    {
        const Uint32 side = m_block_side >> l;
        SDL_GPUTextureTransferInfo src = {};
        src.transfer_buffer = m_block_transfer;
        src.offset = offset;
        src.pixels_per_row = side;
        src.rows_per_layer = side;
        SDL_GPUTextureRegion dst = {};
        dst.texture = m_block_array;
        dst.mip_level = l;
        dst.layer = layer;
        dst.w = side;
        dst.h = side;
        dst.d = 1;
        SDL_UploadToGPUTexture(copy, &src, &dst, false);
        offset += side * side * 4;
    }
}

// The texture array, m_block_side texels a layer: 32 until a frame draws HD blocks, with m_block_levels levels
// (ensure_block_array()).
bool RendererGpu3D::init_block_array()
{
    // Try a generous layer count first; Vulkan only guarantees 256.
    static const uint32_t layer_options[] = { 1024, 256 };
    for (uint32_t layers : layer_options)
    {
        SDL_GPUTextureCreateInfo tex_info = {};
        tex_info.type = SDL_GPU_TEXTURETYPE_2D_ARRAY;
        tex_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        tex_info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        tex_info.width = m_block_side;
        tex_info.height = m_block_side;
        tex_info.layer_count_or_depth = layers;
        tex_info.num_levels = m_block_levels;
        tex_info.sample_count = SDL_GPU_SAMPLECOUNT_1;
        m_block_array = SDL_CreateGPUTexture(m_device, &tex_info);
        if (m_block_array != nullptr)
        {
            m_block_layers = layers;
            break;
        }
    }
    if (m_block_array == nullptr)
    {
        ERRORLOG("SDL_CreateGPUTexture (block array) failed: %s", SDL_GetError());
        return false;
    }

    const Uint32 layer_size = layer_bytes();
    m_upload_ring_capacity = kUploadRingBytes / layer_size;
    if (m_upload_ring_capacity > m_block_layers)
        m_upload_ring_capacity = m_block_layers;
    if (m_upload_ring_capacity < 1)
        m_upload_ring_capacity = 1;
    m_upload_ring_layers = m_upload_ring_capacity;
    SDL_GPUTransferBufferCreateInfo tb_info = {};
    tb_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tb_info.size = layer_size * m_upload_ring_capacity;
    m_block_transfer = SDL_CreateGPUTransferBuffer(m_device, &tb_info);
    if (m_block_transfer == nullptr)
    {
        ERRORLOG("SDL_CreateGPUTransferBuffer (block cache) failed: %s", SDL_GetError());
        return false;
    }

    // Layer 0: solid white.
    unsigned char* mapped = static_cast<unsigned char*>(SDL_MapGPUTransferBuffer(m_device, m_block_transfer, false));
    if (mapped == nullptr)
    {
        ERRORLOG("SDL_MapGPUTransferBuffer (block cache) failed: %s", SDL_GetError());
        return false;
    }
    SDL_memset(mapped, 255, layer_size);
    SDL_UnmapGPUTransferBuffer(m_device, m_block_transfer);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(m_device);
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    copy_layer_levels(copy, 0, 0);
    SDL_EndGPUCopyPass(copy);
    SDL_SubmitGPUCommandBuffer(cmd);
    return true;
}

// HD blocks of `side` texels need layers that big, TEXTURE_MIPMAPS levels below 32: the array and the upload ring
// are made again (the cache flushed). 32x32 blocks then upload nearest-neighbour upscaled, which Nearest samples
// exactly as before; their levels down to 32 are the same texels. The layers only ever grow.
bool RendererGpu3D::ensure_block_array(uint32_t side, uint32_t levels)
{
    if (side < m_block_side)
        side = m_block_side;
    if ((side == m_block_side) && (levels == m_block_levels))
        return true;
    if (m_block_array != nullptr) { SDL_ReleaseGPUTexture(m_device, m_block_array); m_block_array = nullptr; }
    if (m_block_transfer != nullptr) { SDL_ReleaseGPUTransferBuffer(m_device, m_block_transfer); m_block_transfer = nullptr; }
    flush_block_cache();
    m_upload_layers.clear();
    const uint32_t old_side = m_block_side, old_levels = m_block_levels;
    m_block_side = side;
    m_block_levels = levels;
    if (!init_block_array())
    {
        ERRORLOG("RendererGpu3D: no %u-texel, %u-level texture array; back to %u, %u", side, levels, old_side, old_levels);
        m_block_side = old_side;
        m_block_levels = old_levels;
        init_block_array();
        return false;
    }
    JUSTLOG("RendererGpu3D: texture array now %u texels a layer, %u levels, %u layers", m_block_side, m_block_levels, m_block_layers);
    return true;
}

void RendererGpu3D::SetUploadRingLayersForTesting(uint32_t layers)
{
    m_upload_ring_layers = (layers < 1) ? 1 : ((layers > m_upload_ring_capacity) ? m_upload_ring_capacity : layers);
}

// The upload ring is full mid-frame: submit the batch so far (the queue runs it before this frame's draws) and
// map a fresh ring for the rest.
void RendererGpu3D::flush_block_uploads(uint32_t& upload_count, unsigned char*& upload_dst)
{
    SDL_UnmapGPUTransferBuffer(m_device, m_block_transfer);
    upload_dst = nullptr;
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(m_device);
    if (cmd != nullptr)
    {
        SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
        for (uint32_t k = 0; k < upload_count; ++k)
            copy_layer_levels(copy, k, m_upload_layers[k]);
        SDL_EndGPUCopyPass(copy);
        SDL_SubmitGPUCommandBuffer(cmd);
    }
    else
        ERRORLOG("SDL_AcquireGPUCommandBuffer (upload batch) failed: %s", SDL_GetError());
    upload_count = 0;
    m_upload_layers.clear();
    upload_dst = static_cast<unsigned char*>(SDL_MapGPUTransferBuffer(m_device, m_block_transfer, true));
    if (upload_dst == nullptr)
        ERRORLOG("SDL_MapGPUTransferBuffer (block cache) failed: %s", SDL_GetError());
}

uint32_t RendererGpu3D::resolve_block_layer(const unsigned char* texture, const TbPixel* texture_rgba, uint8_t rgba_shift,
    bool original_at_32, uint32_t& upload_count, unsigned char*& upload_dst)
{
    if (texture == nullptr)
        return 0;

    // An HD block is keyed by its own page: the same 32x32 block may also draw as itself (a frame HD can't follow
    // the palette in).
    const void* key = (texture_rgba != nullptr) ? static_cast<const void*>(texture_rgba) : static_cast<const void*>(texture);
    auto it = m_block_cache.find(key);
    if (it != m_block_cache.end() && it->second.frame_seen == m_frame_id)
        return it->second.layer; // already seen this frame
    ++m_texture_stats.blocks;
    ++m_texture_stats.last_frame_blocks;

    uint32_t layer;
    if (it == m_block_cache.end())
    {
        if (m_next_layer >= m_block_layers && m_free_layers.empty())
        {
            // Full: reclaim the layers of blocks not used this frame (a
            // camera fly-through accumulates far more distinct blocks over
            // time than are ever visible at once) before giving up.
            ++m_texture_stats.reclaims;
            for (auto e = m_block_cache.begin(); e != m_block_cache.end(); )
            {
                if (e->second.frame_seen != m_frame_id)
                {
                    m_free_layers.push_back(e->second.layer);
                    e = m_block_cache.erase(e);
                }
                else
                    ++e;
            }
            if (m_free_layers.empty())
            {
                // Genuinely more distinct blocks in one frame than layers.
                if (!m_overflow_logged)
                {
                    WARNLOG("RendererGpu3D: more than %u distinct texture blocks in one frame; extras draw white", m_block_layers);
                    m_overflow_logged = true;
                }
                return 0;
            }
        }
        if (!m_free_layers.empty())
        {
            layer = m_free_layers.back();
            m_free_layers.pop_back();
        }
        else
            layer = m_next_layer++;
        it = m_block_cache.emplace(key, BlockEntry()).first;
        it->second.layer = layer;
    }
    else
    {
        // Cached: its texels only change through a texture-set load, which flushes (WorldTexturesChanged()).
        it->second.frame_seen = m_frame_id;
        return it->second.layer;
    }
    it->second.frame_seen = m_frame_id;

    if (upload_count >= m_upload_ring_layers)
        flush_block_uploads(upload_count, upload_dst);
    if (upload_dst == nullptr)
    {
        // Can't stage it: forget the block so a later frame uploads it; white meanwhile.
        m_block_cache.erase(it);
        m_free_layers.push_back(layer);
        return 0;
    }
    const uint32_t side = m_block_side;
    unsigned char* out = upload_dst + static_cast<size_t>(upload_count) * layer_bytes();
    if (texture_rgba != nullptr)
    {
        // True-colour HD, in the reference palette's colours (the layer palette while HD draws:
        // update_palette_state()), each texel repeated to fill the layer; alpha 0 is texture index 0's black,
        // opaque as for the 32x32 blocks below.
        const uint32_t bside = 32u << rgba_shift;
        const uint32_t rep = (bside < side) ? (side / bside) : 1;
        for (uint32_t y = 0; y < side; ++y)
        {
            const TbPixel* row = texture_rgba + static_cast<size_t>(y / rep) * WORLDFRAME_BLOCK_STRIDE;
            for (uint32_t x = 0; x < side; ++x)
            {
                const TbPixel px = row[x / rep];
                unsigned char* o = out + (static_cast<size_t>(y) * side + x) * 4;
                o[0] = px.r; o[1] = px.g; o[2] = px.b; o[3] = 255;
            }
        }
    }
    else
    {
        unsigned char gathered[WORLDFRAME_BLOCK_SIZE * WORLDFRAME_BLOCK_SIZE];
        for (int row = 0; row < WORLDFRAME_BLOCK_SIZE; ++row)
            SDL_memcpy(gathered + row * WORLDFRAME_BLOCK_SIZE, texture + row * WORLDFRAME_BLOCK_STRIDE, WORLDFRAME_BLOCK_SIZE);
        const unsigned char* palette = m_layer_palette; // see update_palette_state()
        const uint32_t rep = side / WORLDFRAME_BLOCK_SIZE; // 1, or the HD layers' size over 32: nearest upscale
        for (uint32_t y = 0; y < side; ++y)
        {
            for (uint32_t x = 0; x < side; ++x)
            {
                // Same expansion the CPU rasterizer applies per texel
                // (bflib_render_gpoly.c), except index 0: in terrain it is art
                // (bookshelves, reliefs), not a hole. The CPU writes it as
                // (0,0,0,0) over whatever was there and presents it black; left
                // transparent here, the shader would discard it and show what lies
                // behind. So it is opaque black, whatever palette entry 0 holds.
                const unsigned char index = gathered[(y / rep) * WORLDFRAME_BLOCK_SIZE + x / rep];
                const TbPixel px = (index == 0) ? TbPixel_RGB(0, 0, 0) : expand_indexed_pixel(index, palette);
                unsigned char* o = out + (static_cast<size_t>(y) * side + x) * 4;
                o[0] = px.r; o[1] = px.g; o[2] = px.b; o[3] = px.a;
            }
        }
    }
    // The mip levels, each after the one above in the ring: box-filtered, except an HD block's 32x32 level when it
    // is the original block (use_original_at_32: the far zooms look as they always did).
    const unsigned char* above = out;
    unsigned char* level = out + static_cast<size_t>(side) * side * 4;
    for (uint32_t l = 1; l < m_block_levels; ++l)
    {
        const uint32_t ls = side >> l;
        if ((texture_rgba != nullptr) && original_at_32 && (ls == WORLDFRAME_BLOCK_SIZE))
        {
            for (uint32_t y = 0; y < ls; ++y)
                for (uint32_t x = 0; x < ls; ++x)
                {
                    const unsigned char index = texture[y * WORLDFRAME_BLOCK_STRIDE + x];
                    const TbPixel px = (index == 0) ? TbPixel_RGB(0, 0, 0) : expand_indexed_pixel(index, m_layer_palette);
                    unsigned char* o = level + (static_cast<size_t>(y) * ls + x) * 4;
                    o[0] = px.r; o[1] = px.g; o[2] = px.b; o[3] = 255;
                }
        }
        else
        {
            const uint32_t as = ls * 2;
            for (uint32_t y = 0; y < ls; ++y)
                for (uint32_t x = 0; x < ls; ++x)
                {
                    const unsigned char* a = above + (static_cast<size_t>(2 * y) * as + 2 * x) * 4;
                    const unsigned char* b = a + static_cast<size_t>(as) * 4;
                    unsigned char* o = level + (static_cast<size_t>(y) * ls + x) * 4;
                    for (int c = 0; c < 4; ++c)
                        o[c] = static_cast<unsigned char>((a[c] + a[4 + c] + b[c] + b[4 + c] + 2) >> 2);
                }
        }
        above = level;
        level += static_cast<size_t>(ls) * ls * 4;
    }
    ++upload_count;
    ++m_texture_stats.uploads;
    ++m_texture_stats.last_frame_uploads;
    m_texture_stats.upload_bytes += layer_bytes();
    m_upload_layers.push_back(layer);
    return layer;
}

bool RendererGpu3D::ensure_world_target(int64_t width, int64_t height)
{
    if (m_world_target != nullptr && m_world_target_w == width && m_world_target_h == height)
        return true;
    if (m_world_target != nullptr)
    {
        SDL_ReleaseGPUTexture(m_device, m_world_target);
        m_world_target = nullptr;
    }
    if (m_depth_target != nullptr)
    {
        SDL_ReleaseGPUTexture(m_device, m_depth_target);
        m_depth_target = nullptr;
    }
    if (width <= 0 || height <= 0)
        return false;

    SDL_GPUTextureCreateInfo tex_info = {};
    tex_info.type = SDL_GPU_TEXTURETYPE_2D;
    tex_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    tex_info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    tex_info.width = static_cast<Uint32>(width);
    tex_info.height = static_cast<Uint32>(height);
    tex_info.layer_count_or_depth = 1;
    tex_info.num_levels = 1;
    tex_info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    m_world_target = SDL_CreateGPUTexture(m_device, &tex_info);
    if (m_world_target == nullptr)
    {
        ERRORLOG("SDL_CreateGPUTexture (world target) failed: %s", SDL_GetError());
        return false;
    }
    // Depth buffer, same size. SAMPLER usage so a later per-pixel lighting
    // pass can read it (gpu-v2 Phase C.5 infrastructure).
    SDL_GPUTextureCreateInfo depth_info = tex_info;
    depth_info.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    depth_info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    m_depth_target = SDL_CreateGPUTexture(m_device, &depth_info);
    if (m_depth_target == nullptr)
    {
        ERRORLOG("SDL_CreateGPUTexture (depth target) failed: %s", SDL_GetError());
        SDL_ReleaseGPUTexture(m_device, m_world_target);
        m_world_target = nullptr;
        return false;
    }
    m_world_target_w = width;
    m_world_target_h = height;
    return true;
}

bool RendererGpu3D::ensure_vertex_buffer(uint32_t vertex_count)
{
    if (m_vertex_buffer != nullptr && m_vertex_buffer_capacity >= vertex_count)
        return true;

    if (m_vertex_buffer != nullptr)
    {
        SDL_ReleaseGPUBuffer(m_device, m_vertex_buffer);
        m_vertex_buffer = nullptr;
    }
    if (m_vertex_transfer != nullptr)
    {
        SDL_ReleaseGPUTransferBuffer(m_device, m_vertex_transfer);
        m_vertex_transfer = nullptr;
    }

    const Uint32 size = static_cast<Uint32>(vertex_count * sizeof(GpuVertex));

    SDL_GPUBufferCreateInfo vb_info = {};
    vb_info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    vb_info.size = size;
    m_vertex_buffer = SDL_CreateGPUBuffer(m_device, &vb_info);
    if (m_vertex_buffer == nullptr)
    {
        ERRORLOG("SDL_CreateGPUBuffer (vertex) failed: %s", SDL_GetError());
        return false;
    }

    SDL_GPUTransferBufferCreateInfo tb_info = {};
    tb_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tb_info.size = size;
    m_vertex_transfer = SDL_CreateGPUTransferBuffer(m_device, &tb_info);
    if (m_vertex_transfer == nullptr)
    {
        ERRORLOG("SDL_CreateGPUTransferBuffer (vertex) failed: %s", SDL_GetError());
        return false;
    }

    m_vertex_buffer_capacity = vertex_count;
    return true;
}

bool RendererGpu3D::Init()
{
    if (!init_gpu_device())
        return false;
    if (!init_pipeline() || !init_block_array() || !init_light_grid() || !init_colour_transform() || !m_sprites.Init(m_device))
    {
        destroy_gpu();
        return false;
    }
    // gpu-v2 Phase C.1: share this device with the compositing/present
    // machinery -- see RendererSoftware::UseGpuDevice()'s own comment.
    m_sprites.SetLightGrid(m_light_grid, m_light_sampler);
    m_software.UseGpuDevice(m_device);
    return m_software.Init();
}

// gpu-v2 Phase C.5 lighting pass: the dynamic-light reach grid (one R8 texel per
// map subtile, 512x512 covers MAX_SUBTILES+1) and its bilinear sampler.
bool RendererGpu3D::init_light_grid()
{
    SDL_GPUTextureCreateInfo ti = {};
    ti.type = SDL_GPU_TEXTURETYPE_2D;
    ti.format = SDL_GPU_TEXTUREFORMAT_R8_UNORM;
    ti.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    ti.width = kLightGridSize; ti.height = kLightGridSize;
    ti.layer_count_or_depth = 1; ti.num_levels = 1;
    ti.sample_count = SDL_GPU_SAMPLECOUNT_1;
    m_light_grid = SDL_CreateGPUTexture(m_device, &ti);
    SDL_GPUTransferBufferCreateInfo tb = {};
    tb.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tb.size = kLightGridSize * kLightGridSize;
    m_light_grid_transfer = SDL_CreateGPUTransferBuffer(m_device, &tb);
    SDL_GPUSamplerCreateInfo si = {};
    si.min_filter = SDL_GPU_FILTER_LINEAR;
    si.mag_filter = SDL_GPU_FILTER_LINEAR;
    si.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    si.address_mode_u = si.address_mode_v = si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    m_light_sampler = SDL_CreateGPUSampler(m_device, &si);
    if (m_light_grid == nullptr || m_light_grid_transfer == nullptr || m_light_sampler == nullptr)
    {
        ERRORLOG("RendererGpu3D: light grid creation failed: %s", SDL_GetError());
        return false;
    }
    return true;
}

// Palette effects as a colour transform (bflib_colour_transform.h, docs/newFeatures/texture-resolution/02-design.md
// section 5.1.3): the curves and the 64^3 table the fragment shader reads with texelFetch.
bool RendererGpu3D::init_colour_transform()
{
    SDL_GPUTextureCreateInfo ti = {};
    ti.type = SDL_GPU_TEXTURETYPE_2D;
    ti.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ti.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    ti.width = 256; ti.height = 1;
    ti.layer_count_or_depth = 1; ti.num_levels = 1;
    ti.sample_count = SDL_GPU_SAMPLECOUNT_1;
    m_transform_curves_tex = SDL_CreateGPUTexture(m_device, &ti);
    ti.type = SDL_GPU_TEXTURETYPE_3D;
    ti.width = ti.height = ti.layer_count_or_depth = COLOUR_TRANSFORM_TABLE_SIDE;
    m_transform_table_tex = SDL_CreateGPUTexture(m_device, &ti);
    SDL_GPUTransferBufferCreateInfo tb = {};
    tb.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tb.size = 256 * 4 + 4 * COLOUR_TRANSFORM_TABLE_SIZE;
    m_transform_transfer = SDL_CreateGPUTransferBuffer(m_device, &tb);
    SDL_GPUSamplerCreateInfo si = {};
    si.min_filter = SDL_GPU_FILTER_NEAREST;
    si.mag_filter = SDL_GPU_FILTER_NEAREST;
    si.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    si.address_mode_u = si.address_mode_v = si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    m_transform_sampler = SDL_CreateGPUSampler(m_device, &si);
    if (m_transform_curves_tex == nullptr || m_transform_table_tex == nullptr || m_transform_transfer == nullptr || m_transform_sampler == nullptr)
    {
        ERRORLOG("RendererGpu3D: colour transform resources failed: %s", SDL_GetError());
        return false;
    }
    m_transform_table.assign(4 * COLOUR_TRANSFORM_TABLE_SIZE, 0);
    m_transform_nearest.assign(COLOUR_TRANSFORM_TABLE_SIZE, 0);
    m_transform.table = m_transform_table.data();
    return true;
}

// Every cached layer is stale: layers are simply reassigned.
void RendererGpu3D::flush_block_cache()
{
    m_block_cache.clear();
    m_free_layers.clear();
    m_next_layer = 1;
    m_overflow_logged = false;
}

// Decides how this frame's palette is drawn: the layers as they are (the palette they were expanded through is
// active), through the colour transform (a palette effect a colour function reproduces exactly), or flushed and
// re-expanded through the new palette (anything else). Output equals expanding every texel through the active
// palette in all three cases.
void RendererGpu3D::update_palette_state(const unsigned char* active_palette, const unsigned char* hd_reference)
{
    // A frame with HD blocks: they are in the reference palette's colours, so the layers must be too (one
    // transform for both; the engine draws HD only while that transform is exact, RendererHdUsable()).
    if ((hd_reference != nullptr) && (!m_layer_palette_valid || SDL_memcmp(m_layer_palette, hd_reference, sizeof(m_layer_palette)) != 0))
    {
        flush_block_cache();
        SDL_memcpy(m_layer_palette, hd_reference, sizeof(m_layer_palette));
        m_layer_palette_valid = true;
        m_transform_valid = false;
        m_transform_nearest_valid = false;
        m_transform.mode = CTM_Identity;
    }
    if (m_layer_palette_valid && SDL_memcmp(m_layer_palette, active_palette, sizeof(m_layer_palette)) == 0)
        return;
    if (m_layer_palette_valid)
    {
        if (!m_transform_valid || SDL_memcmp(m_transform_palette, active_palette, sizeof(m_transform_palette)) != 0)
        {
            enum ColourTransformMode mode = colour_transform_build(&m_transform, m_layer_palette, active_palette,
                m_transform_nearest_valid ? m_transform_nearest.data() : nullptr);
            if ((mode == CTM_Unsupported) && !m_transform_nearest_valid)
            {
                // A table is needed: its nearest-entry map, once per layer palette.
                colour_transform_nearest_entries(m_layer_palette, m_transform_nearest.data());
                m_transform_nearest_valid = true;
                mode = colour_transform_build(&m_transform, m_layer_palette, active_palette, m_transform_nearest.data());
            }
            SDL_memcpy(m_transform_palette, active_palette, sizeof(m_transform_palette));
            m_transform_valid = true;
            m_transform_upload_pending = (mode == CTM_Curves) || (mode == CTM_Table);
        }
        if (m_transform.mode != CTM_Unsupported)
            return;
        ++m_texture_stats.palette_flushes;
    }
    flush_block_cache();
    SDL_memcpy(m_layer_palette, active_palette, sizeof(m_layer_palette));
    m_layer_palette_valid = true;
    m_transform_valid = false;
    m_transform_nearest_valid = false;
    m_transform.mode = CTM_Identity;
}

void RendererGpu3D::destroy_gpu()
{
    if (m_device == nullptr)
        return;
    if (m_transform_curves_tex != nullptr) { SDL_ReleaseGPUTexture(m_device, m_transform_curves_tex); m_transform_curves_tex = nullptr; }
    if (m_transform_table_tex != nullptr) { SDL_ReleaseGPUTexture(m_device, m_transform_table_tex); m_transform_table_tex = nullptr; }
    if (m_transform_transfer != nullptr) { SDL_ReleaseGPUTransferBuffer(m_device, m_transform_transfer); m_transform_transfer = nullptr; }
    if (m_transform_sampler != nullptr) { SDL_ReleaseGPUSampler(m_device, m_transform_sampler); m_transform_sampler = nullptr; }
    m_layer_palette_valid = false;
    m_transform_valid = false;
    m_transform_nearest_valid = false;
    if (m_light_grid != nullptr) { SDL_ReleaseGPUTexture(m_device, m_light_grid); m_light_grid = nullptr; }
    if (m_light_grid_transfer != nullptr) { SDL_ReleaseGPUTransferBuffer(m_device, m_light_grid_transfer); m_light_grid_transfer = nullptr; }
    if (m_light_sampler != nullptr) { SDL_ReleaseGPUSampler(m_device, m_light_sampler); m_light_sampler = nullptr; }
    m_sprites.Destroy();
    if (m_vertex_buffer != nullptr) { SDL_ReleaseGPUBuffer(m_device, m_vertex_buffer); m_vertex_buffer = nullptr; }
    if (m_vertex_transfer != nullptr) { SDL_ReleaseGPUTransferBuffer(m_device, m_vertex_transfer); m_vertex_transfer = nullptr; }
    m_vertex_buffer_capacity = 0;
    if (m_readback_transfer != nullptr) { SDL_ReleaseGPUTransferBuffer(m_device, m_readback_transfer); m_readback_transfer = nullptr; }
    m_readback_capacity = 0;
    m_world_frame_valid = false;
    if (m_depth_target != nullptr) { SDL_ReleaseGPUTexture(m_device, m_depth_target); m_depth_target = nullptr; }
    if (m_world_target != nullptr) { SDL_ReleaseGPUTexture(m_device, m_world_target); m_world_target = nullptr; }
    m_world_target_w = 0;
    m_world_target_h = 0;
    if (m_block_array != nullptr) { SDL_ReleaseGPUTexture(m_device, m_block_array); m_block_array = nullptr; }
    if (m_block_transfer != nullptr) { SDL_ReleaseGPUTransferBuffer(m_device, m_block_transfer); m_block_transfer = nullptr; }
    m_block_cache.clear();
    m_free_layers.clear();
    m_next_layer = 1;
    if (m_sampler != nullptr) { SDL_ReleaseGPUSampler(m_device, m_sampler); m_sampler = nullptr; }
    if (m_sampler_linear_repeat != nullptr) { SDL_ReleaseGPUSampler(m_device, m_sampler_linear_repeat); m_sampler_linear_repeat = nullptr; }
    if (m_sampler_linear_clamp != nullptr) { SDL_ReleaseGPUSampler(m_device, m_sampler_linear_clamp); m_sampler_linear_clamp = nullptr; }
    if (m_sampler_trilinear_repeat != nullptr) { SDL_ReleaseGPUSampler(m_device, m_sampler_trilinear_repeat); m_sampler_trilinear_repeat = nullptr; }
    if (m_sampler_trilinear_clamp != nullptr) { SDL_ReleaseGPUSampler(m_device, m_sampler_trilinear_clamp); m_sampler_trilinear_clamp = nullptr; }
    if (m_pipeline != nullptr) { SDL_ReleaseGPUGraphicsPipeline(m_device, m_pipeline); m_pipeline = nullptr; }
    if (m_vertex_shader != nullptr) { SDL_ReleaseGPUShader(m_device, m_vertex_shader); m_vertex_shader = nullptr; }
    if (m_fragment_shader != nullptr) { SDL_ReleaseGPUShader(m_device, m_fragment_shader); m_fragment_shader = nullptr; }
    SDL_DestroyGPUDevice(m_device);
    m_device = nullptr;
}

void RendererGpu3D::Shutdown()
{
    m_software.Shutdown(); // destroys its SDL_Renderer (and the wrapper of our world texture) first
    destroy_gpu();
}

void RendererGpu3D::SubmitWorldFrame(const WorldFrame& frame)
{
    if (m_device == nullptr || m_pipeline == nullptr || lbDrawSurface == nullptr)
        return;
    if (!ensure_world_target(lbDrawSurface->w, lbDrawSurface->h))
        return;
    RPROF_SCOPE(RPS_SUBMIT);

    ++m_frame_id;
    ++m_texture_stats.frames;
    m_texture_stats.last_frame_blocks = 0;
    m_texture_stats.last_frame_uploads = 0;
    if (m_textures_changed)
    {
        flush_block_cache();
        m_textures_changed = false;
    }
    // HD blocks in this frame: their size sets the layers' (ensure_block_array()), their colours the layer palette.
    uint8_t hd_shift = 0;
    bool frame_has_hd = false;
    for (int64_t i = 0; i < frame.op_count; ++i)
    {
        if ((frame.ops[i].kind == WF_OP_POLY) && (frame.ops[i].u.poly.texture_rgba != nullptr))
        {
            frame_has_hd = true;
            if (frame.ops[i].u.poly.block_shift > hd_shift)
                hd_shift = frame.ops[i].u.poly.block_shift;
        }
    }
    // Levels: down to 32, and to 1x1 with TEXTURE_MIPMAPS.
    {
        const uint32_t side = frame_has_hd ? (static_cast<uint32_t>(WORLDFRAME_BLOCK_SIZE) << hd_shift) : m_block_side;
        const uint32_t final_side = (side > m_block_side) ? side : m_block_side;
        uint32_t levels = 1;
        while ((final_side >> (levels - 1)) > WORLDFRAME_BLOCK_SIZE)
            ++levels;
        if (RendererGetTextureMipmaps())
            levels += 5;
        if (!ensure_block_array(side, levels))
            frame_has_hd = frame_has_hd && (m_block_side >= side);
    }
    update_palette_state(RendererGetActivePalette(), frame_has_hd ? RendererHdReferencePalette() : nullptr);

    // Walk the ops once, in draw order: terrain triangles become float
    // vertices in one stream, sprite ops quads in GpuSpriteRenderer's; draws
    // are recorded per contiguous run so the two interleave exactly as the
    // painter's algorithm did on the CPU.
    RPROF_BEGIN(RPS_BUILD);
    // wrap: a terrain run whose polygons may leave their block (scrolled UVs) and so sample with REPEAT; the rest
    // clamp at the block's edge when a filter is on (texture-resolution 02-design.md section 5.1.3).
    struct DrawRun { bool sprite; GpuSpriteRenderer::DrawCall sprite_draw; uint32_t first, count; bool wrap; };
    const int texture_filter = RendererGetTextureFilter();
    const bool filtered = (texture_filter != RENDERER_TEXFILTER_NEAREST);
    std::vector<DrawRun> runs;
    // Consecutive sprite draws of the same mode over adjacent vertices (a
    // creature shadow's two triangles, runs of plain sprites) merge into one draw.
    auto push_sprite_run = [&runs](const GpuSpriteRenderer::DrawCall& d) {
        if (!runs.empty() && runs.back().sprite && runs.back().sprite_draw.mode == d.mode &&
            runs.back().sprite_draw.first + runs.back().sprite_draw.count == d.first)
            runs.back().sprite_draw.count += d.count;
        else
            runs.push_back({ true, d, 0, 0, false });
    };
    std::vector<GpuVertex> vertices;
    vertices.reserve(static_cast<size_t>(frame.op_count) * 3);
    m_upload_layers.clear();
    m_sprites.BeginFrame(frame);
    uint32_t upload_count = 0;
    unsigned char* upload_dst = nullptr;
    bool have_poly = false;
    for (int64_t i = 0; i < frame.op_count; ++i)
        if (frame.ops[i].kind == WF_OP_POLY) { have_poly = true; break; }
    if (have_poly)
    {
        upload_dst = static_cast<unsigned char*>(SDL_MapGPUTransferBuffer(m_device, m_block_transfer, true));
        if (upload_dst == nullptr)
        {
            ERRORLOG("SDL_MapGPUTransferBuffer (block cache) failed: %s", SDL_GetError());
            return;
        }
    }

    const float off_x = static_cast<float>(frame.view_w > 0 ? frame.view_x : 0);
    const float off_y = static_cast<float>(frame.view_w > 0 ? frame.view_y : 0);
    for (int64_t i = 0; i < frame.op_count; ++i)
    {
        const WorldFrameOp& op = frame.ops[i];
        if (op.kind == WF_OP_POLY)
        {
            const WorldFramePolyItem& item = op.u.poly;
            const TbPixel* rgba = frame_has_hd ? item.texture_rgba : nullptr;
            const float layer = static_cast<float>(resolve_block_layer(item.texture, rgba, item.block_shift, item.original_at_32 != 0,
                upload_count, upload_dst));
            const struct PolyPoint* src[3] = { &item.v0, &item.v1, &item.v2 };
            const uint32_t first = static_cast<uint32_t>(vertices.size());
            // Nearest samples like the CPU path: UVs truncated to whole texels, REPEAT (its mask wraps). Filtered:
            // the UVs' fractions, and REPEAT only for a polygon whose UVs leave [0, 32) -- inside, every point is
            // a mix of its corners' UVs, so it stays in its block and can clamp at the block's edge.
            bool wrap = true;
            if (filtered)
            {
                const int64_t block_end = (int64_t)WORLDFRAME_BLOCK_SIZE << 16;
                wrap = false;
                for (int v = 0; v < 3; ++v)
                    wrap |= (src[v]->U < 0) || (src[v]->U >= block_end) || (src[v]->V < 0) || (src[v]->V >= block_end);
            }
            for (int v = 0; v < 3; ++v)
            {
                GpuVertex dst;
                dst.x = static_cast<float>(src[v]->X) + off_x;
                dst.y = static_cast<float>(src[v]->Y) + off_y;
                // Fractions kept when filtered, or for an HD block in precision mode (Nearest then samples it at
                // the UV's own position); whole texels otherwise -- the CPU's 32-res truncation, which parity keeps.
                if (filtered || ((rgba != nullptr) && !item.parity))
                {
                    dst.u = static_cast<float>(static_cast<double>(src[v]->U) / 65536.0);
                    dst.v = static_cast<float>(static_cast<double>(src[v]->V) / 65536.0);
                }
                else
                {
                    dst.u = static_cast<float>(src[v]->U >> 16);
                    dst.v = static_cast<float>(src[v]->V >> 16);
                }
                dst.uw = static_cast<float>(src[v]->U >> 16);
                dst.vw = static_cast<float>(src[v]->V >> 16);
                dst.shade = static_cast<float>(src[v]->S >> 16);
                dst.layer = layer;
                dst.depth = op.depth;
                {
                    const float zp = static_cast<float>(src[v]->Z) / static_cast<float>(WORLDFRAME_DEPTH_ONE);
                    dst.zpos = zp < 0.0f ? 0.0f : (zp > 1.0f ? 1.0f : zp);
                }
                if (frame.true_depth)
                {
                    float z = static_cast<float>(src[v]->Z) / static_cast<float>(WORLDFRAME_DEPTH_ONE);
                    dst.depth = z < 0.0f ? 0.0f : (z > 1.0f ? 1.0f : z);
                }
                vertices.push_back(dst);
            }
            if (!runs.empty() && !runs.back().sprite && (runs.back().wrap == wrap) && runs.back().first + runs.back().count == first)
                runs.back().count += 3;
            else
                runs.push_back({ false, {}, first, 3, wrap });
        }
        else if (op.kind == WF_OP_SPRITE)
        {
            GpuSpriteRenderer::DrawCall draws[2];
            const int n = m_sprites.AddOp(op.u.sprite, off_x, off_y, op.depth, op.view_depth, draws);
            for (int k = 0; k < n; ++k)
                push_sprite_run(draws[k]);
        }
        else if (op.kind == WF_OP_SHADOW)
        {
            GpuSpriteRenderer::DrawCall draws[1];
            if (frame.pixels != nullptr && op.u.shadow.pix_off + static_cast<int64_t>(op.u.shadow.w) * op.u.shadow.h <= frame.pixel_count &&
                m_sprites.AddShadow(op.u.shadow, frame.pixels + op.u.shadow.pix_off, off_x, off_y, op.depth, op.view_depth, draws) == 1)
                push_sprite_run(draws[0]);
        }
    }
    if (upload_dst != nullptr)
        SDL_UnmapGPUTransferBuffer(m_device, m_block_transfer);
    RPROF_END(RPS_BUILD);

    const uint32_t vertex_count = static_cast<uint32_t>(vertices.size());
    if (vertex_count > 0)
    {
        if (!ensure_vertex_buffer(vertex_count))
            return;
        void* mapped = SDL_MapGPUTransferBuffer(m_device, m_vertex_transfer, true);
        if (mapped == nullptr)
        {
            ERRORLOG("SDL_MapGPUTransferBuffer (vertex) failed: %s", SDL_GetError());
            return;
        }
        SDL_memcpy(mapped, vertices.data(), vertices.size() * sizeof(GpuVertex));
        SDL_UnmapGPUTransferBuffer(m_device, m_vertex_transfer);
    }

    RPROF_BEGIN(RPS_ENCODE);
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(m_device);
    if (cmd == nullptr)
    {
        ERRORLOG("SDL_AcquireGPUCommandBuffer failed: %s", SDL_GetError());
        return;
    }

    const WorldFrameLighting* lighting = frame.lighting;
    const bool lit = lighting != nullptr && lighting->grid != nullptr && lighting->grid_w > 0 && lighting->grid_h > 0 &&
                     lighting->grid_w <= static_cast<int32_t>(kLightGridSize) && lighting->grid_h <= static_cast<int32_t>(kLightGridSize);
    if (vertex_count > 0 || upload_count > 0 || m_sprites.HasWork() || lit || m_transform_upload_pending)
    {
        SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
        if (m_transform_upload_pending)
        {
            unsigned char* tmap = static_cast<unsigned char*>(SDL_MapGPUTransferBuffer(m_device, m_transform_transfer, true));
            if (tmap != nullptr)
            {
                for (int x = 0; x < 256; ++x)
                {
                    tmap[x * 4 + 0] = m_transform.curves[0][x];
                    tmap[x * 4 + 1] = m_transform.curves[1][x];
                    tmap[x * 4 + 2] = m_transform.curves[2][x];
                    tmap[x * 4 + 3] = 255;
                }
                const bool table = (m_transform.mode == CTM_Table);
                if (table)
                    SDL_memcpy(tmap + 256 * 4, m_transform_table.data(), m_transform_table.size());
                SDL_UnmapGPUTransferBuffer(m_device, m_transform_transfer);
                SDL_GPUTextureTransferInfo csrc = {};
                csrc.transfer_buffer = m_transform_transfer;
                csrc.pixels_per_row = 256;
                csrc.rows_per_layer = 1;
                SDL_GPUTextureRegion cdst = {};
                cdst.texture = m_transform_curves_tex;
                cdst.w = 256; cdst.h = 1; cdst.d = 1;
                SDL_UploadToGPUTexture(copy, &csrc, &cdst, true);
                if (table)
                {
                    SDL_GPUTextureTransferInfo tsrc = {};
                    tsrc.transfer_buffer = m_transform_transfer;
                    tsrc.offset = 256 * 4;
                    tsrc.pixels_per_row = COLOUR_TRANSFORM_TABLE_SIDE;
                    tsrc.rows_per_layer = COLOUR_TRANSFORM_TABLE_SIDE;
                    SDL_GPUTextureRegion tdst = {};
                    tdst.texture = m_transform_table_tex;
                    tdst.w = tdst.h = tdst.d = COLOUR_TRANSFORM_TABLE_SIDE;
                    SDL_UploadToGPUTexture(copy, &tsrc, &tdst, true);
                }
            }
            m_transform_upload_pending = false;
        }
        if (lit)
        {
            unsigned char* gmap = static_cast<unsigned char*>(SDL_MapGPUTransferBuffer(m_device, m_light_grid_transfer, true));
            if (gmap != nullptr)
            {
                SDL_memcpy(gmap, lighting->grid, static_cast<size_t>(lighting->grid_w) * lighting->grid_h);
                SDL_UnmapGPUTransferBuffer(m_device, m_light_grid_transfer);
                SDL_GPUTextureTransferInfo gsrc = {};
                gsrc.transfer_buffer = m_light_grid_transfer;
                gsrc.pixels_per_row = static_cast<Uint32>(lighting->grid_w);
                gsrc.rows_per_layer = static_cast<Uint32>(lighting->grid_h);
                SDL_GPUTextureRegion gdst = {};
                gdst.texture = m_light_grid;
                gdst.w = static_cast<Uint32>(lighting->grid_w);
                gdst.h = static_cast<Uint32>(lighting->grid_h);
                gdst.d = 1;
                SDL_UploadToGPUTexture(copy, &gsrc, &gdst, true);
            }
        }
        for (uint32_t k = 0; k < upload_count; ++k)
            copy_layer_levels(copy, k, m_upload_layers[k]);
        if (vertex_count > 0)
        {
            SDL_GPUTransferBufferLocation src_loc = {};
            src_loc.transfer_buffer = m_vertex_transfer;
            SDL_GPUBufferRegion dst_region = {};
            dst_region.buffer = m_vertex_buffer;
            dst_region.size = static_cast<Uint32>(vertices.size() * sizeof(GpuVertex));
            SDL_UploadToGPUBuffer(copy, &src_loc, &dst_region, true);
        }
        m_sprites.Upload(copy);
        SDL_EndGPUCopyPass(copy);
    }

    SDL_GPUColorTargetInfo color_target = {};
    color_target.texture = m_world_target;
    color_target.load_op = frame.overlay ? SDL_GPU_LOADOP_LOAD : SDL_GPU_LOADOP_CLEAR;
    color_target.store_op = SDL_GPU_STOREOP_STORE;
    color_target.clear_color = SDL_FColor{ frame.clear_r / 255.0f, frame.clear_g / 255.0f, frame.clear_b / 255.0f, 1.0f };

    // Uniform data is pushed before the render pass begins, as SDL's own
    // testgpu_spinning_cube.c does. Both the terrain and sprite vertex shaders
    // read the same viewport uniform from slot 0.
    struct { float w, h; } viewport_uniform = { static_cast<float>(m_world_target_w), static_cast<float>(m_world_target_h) };
    SDL_PushGPUVertexUniformData(cmd, 0, &viewport_uniform, sizeof(viewport_uniform));
    // Per-pixel lighting inputs (mirrors the shader's Lighting block); disabled = all zero.
    struct LightingUniform { float p0[4], fade[4], map_x[4], map_y[4], map_z[4], grid[4], lights[WORLDFRAME_MAX_LIGHTS][8]; };
    LightingUniform lighting_uniform = {};
    if (lit)
    {
        lighting_uniform.p0[0] = 1.0f;
        lighting_uniform.p0[1] = static_cast<float>(lighting->centre_x + (frame.view_w > 0 ? frame.view_x : 0));
        lighting_uniform.p0[2] = static_cast<float>(lighting->centre_y + (frame.view_w > 0 ? frame.view_y : 0));
        lighting_uniform.p0[3] = static_cast<float>(lighting->lens);
        lighting_uniform.fade[0] = static_cast<float>(lighting->fade_min);
        lighting_uniform.fade[1] = static_cast<float>(lighting->fade_max);
        lighting_uniform.fade[2] = static_cast<float>(lighting->fade_scaler);
        lighting_uniform.fade[3] = static_cast<float>(lighting->fade_range);
        for (int k = 0; k < 4; ++k)
        {
            lighting_uniform.map_x[k] = static_cast<float>(lighting->map_x[k]);
            lighting_uniform.map_y[k] = static_cast<float>(lighting->map_y[k]);
            lighting_uniform.map_z[k] = static_cast<float>(lighting->map_z[k]);
        }
        lighting_uniform.grid[0] = lighting_uniform.grid[1] = static_cast<float>(kLightGridSize);
        lighting_uniform.grid[2] = static_cast<float>(lighting->light_count);
        for (int k = 0; k < lighting->light_count && k < WORLDFRAME_MAX_LIGHTS; ++k)
        {
            lighting_uniform.lights[k][0] = lighting->lights[k].x;
            lighting_uniform.lights[k][1] = lighting->lights[k].y;
            lighting_uniform.lights[k][2] = lighting->lights[k].z;
            lighting_uniform.lights[k][3] = lighting->lights[k].radius;
            lighting_uniform.lights[k][4] = lighting->lights[k].intensity;
            lighting_uniform.lights[k][5] = lighting->lights[k].r;
            lighting_uniform.lights[k][6] = lighting->lights[k].g;
            lighting_uniform.lights[k][7] = lighting->lights[k].b;
        }
    }
    SDL_PushGPUFragmentUniformData(cmd, 0, &lighting_uniform, sizeof(lighting_uniform));
    {
        // world_polygon.frag's Transform: x = 0 identity, 1 curves, 2 table (bflib_colour_transform.h).
        const bool layers_current = m_layer_palette_valid && (SDL_memcmp(m_layer_palette, RendererGetActivePalette(), sizeof(m_layer_palette)) == 0);
        // y: the texture filter (RENDERER_TEXFILTER_*). z: in a Nearest frame with HD blocks, the level of the original
        // 32x32 block in the layers (log2(side / 32)), where the shader samples as a frame without HD would; 0 else.
        float transform_uniform[4] = { 0.0f, static_cast<float>(RendererGetTextureFilter()), 0.0f, 0.0f };
        if (frame_has_hd && (RendererGetTextureFilter() == RENDERER_TEXFILTER_NEAREST))
        {
            uint32_t level32 = 0;
            while ((m_block_side >> level32) > WORLDFRAME_BLOCK_SIZE)
                ++level32;
            transform_uniform[2] = static_cast<float>(level32);
        }
        if (!layers_current && (m_transform.mode == CTM_Curves || m_transform.mode == CTM_Table))
            transform_uniform[0] = static_cast<float>(m_transform.mode);
        SDL_PushGPUFragmentUniformData(cmd, 1, transform_uniform, sizeof(transform_uniform));
    }

    SDL_GPUDepthStencilTargetInfo depth_target = {};
    depth_target.texture = m_depth_target;
    depth_target.clear_depth = 1.0f; // far
    depth_target.load_op = frame.overlay ? SDL_GPU_LOADOP_LOAD : SDL_GPU_LOADOP_CLEAR;
    depth_target.store_op = SDL_GPU_STOREOP_STORE; // kept for later passes (per-pixel lighting) and read-back
    depth_target.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depth_target.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &color_target, 1, &depth_target);
    if (!runs.empty())
    {
        SDL_Rect scissor = { 0, 0, static_cast<int>(m_world_target_w), static_cast<int>(m_world_target_h) };
        if (frame.view_w > 0 && frame.view_h > 0)
            scissor = { static_cast<int>(frame.view_x), static_cast<int>(frame.view_y), static_cast<int>(frame.view_w), static_cast<int>(frame.view_h) };
        SDL_SetGPUScissor(pass, &scissor);

        for (const DrawRun& run : runs)
        {
            if (run.sprite)
            {
                m_sprites.Draw(pass, run.sprite_draw);
                continue;
            }
            SDL_BindGPUGraphicsPipeline(pass, m_pipeline);
            SDL_GPUBufferBinding vb_binding = {};
            vb_binding.buffer = m_vertex_buffer;
            SDL_BindGPUVertexBuffers(pass, 0, &vb_binding, 1);
            SDL_GPUTextureSamplerBinding tex_bindings[4] = {};
            tex_bindings[0].texture = m_block_array;
            tex_bindings[0].sampler = !filtered ? m_sampler :
                ((texture_filter == RENDERER_TEXFILTER_TRILINEAR) ? (run.wrap ? m_sampler_trilinear_repeat : m_sampler_trilinear_clamp) :
                    (run.wrap ? m_sampler_linear_repeat : m_sampler_linear_clamp));
            tex_bindings[1].texture = m_light_grid;
            tex_bindings[1].sampler = m_light_sampler;
            tex_bindings[2].texture = m_transform_curves_tex;
            tex_bindings[2].sampler = m_transform_sampler;
            tex_bindings[3].texture = m_transform_table_tex;
            tex_bindings[3].sampler = m_transform_sampler;
            SDL_BindGPUFragmentSamplers(pass, 0, tex_bindings, 4);
            SDL_DrawGPUPrimitives(pass, run.count, 1, run.first, 0);
        }
    }
    SDL_EndGPURenderPass(pass);
    // Counters for the RPROF report (Debug log level; see renderer/RendererProfile.h).
    if (RPROF_ACTIVE())
    {
        int64_t polys = 0, sprites = 0, shadows = 0;
        for (int64_t i = 0; i < frame.op_count; ++i)
        {
            if (frame.ops[i].kind == WF_OP_POLY) ++polys;
            else if (frame.ops[i].kind == WF_OP_SPRITE) ++sprites;
            else ++shadows;
        }
        RPROF_COUNT(RPC_OPS, frame.op_count);
        RPROF_COUNT(RPC_POLYS, polys);
        RPROF_COUNT(RPC_SPRITES, sprites);
        RPROF_COUNT(RPC_SHADOWS, shadows);
        RPROF_COUNT(RPC_VERTICES, vertex_count);
        RPROF_COUNT(RPC_DRAW_RUNS, runs.size());
        RPROF_COUNT(RPC_BLOCK_UPLOADS, (int64_t)m_texture_stats.last_frame_uploads);
    }
    if (RPROF_SYNC_GPU())
    {
        SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
        RPROF_END(RPS_ENCODE);
        if (fence != nullptr)
        {
            RPROF_BEGIN(RPS_GPUWAIT);
            SDL_WaitForGPUFences(m_device, true, &fence, 1);
            SDL_ReleaseGPUFence(m_device, fence);
            RPROF_END(RPS_GPUWAIT);
        }
    }
    else
    {
        SDL_SubmitGPUCommandBuffer(cmd);
        RPROF_END(RPS_ENCODE);
    }

    m_software.SetWorldUnderlay(m_world_target, m_world_target_w, m_world_target_h);
    m_world_frame_valid = true;
}

bool RendererGpu3D::ReadbackWorldLayer(TbPixel* dst, int64_t dst_pitch, int64_t x, int64_t y, int64_t w, int64_t h)
{
    if (!m_world_frame_valid || m_device == nullptr || m_world_target == nullptr || dst == nullptr)
        return false;
    if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > m_world_target_w || y + h > m_world_target_h)
        return false;
    const uint32_t bytes = static_cast<uint32_t>(w * h * 4);
    if (m_readback_transfer == nullptr || m_readback_capacity < bytes)
    {
        if (m_readback_transfer != nullptr)
            SDL_ReleaseGPUTransferBuffer(m_device, m_readback_transfer);
        SDL_GPUTransferBufferCreateInfo info = {};
        info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
        info.size = bytes;
        m_readback_transfer = SDL_CreateGPUTransferBuffer(m_device, &info);
        m_readback_capacity = (m_readback_transfer != nullptr) ? bytes : 0;
        if (m_readback_transfer == nullptr)
        {
            ERRORLOG("SDL_CreateGPUTransferBuffer (read-back) failed: %s", SDL_GetError());
            return false;
        }
    }
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(m_device);
    if (cmd == nullptr)
        return false;
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureRegion region = {};
    region.texture = m_world_target;
    region.x = static_cast<Uint32>(x);
    region.y = static_cast<Uint32>(y);
    region.w = static_cast<Uint32>(w);
    region.h = static_cast<Uint32>(h);
    region.d = 1;
    SDL_GPUTextureTransferInfo xfer = {};
    xfer.transfer_buffer = m_readback_transfer;
    xfer.pixels_per_row = static_cast<Uint32>(w);
    xfer.rows_per_layer = static_cast<Uint32>(h);
    SDL_DownloadFromGPUTexture(copy, &region, &xfer);
    SDL_EndGPUCopyPass(copy);
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (fence == nullptr)
        return false;
    SDL_WaitForGPUFences(m_device, true, &fence, 1);
    SDL_ReleaseGPUFence(m_device, fence);

    const unsigned char* mapped = static_cast<const unsigned char*>(SDL_MapGPUTransferBuffer(m_device, m_readback_transfer, false));
    if (mapped == nullptr)
        return false;
    for (int64_t row = 0; row < h; row++)
    {
        TbPixel* out = dst + (y + row) * dst_pitch + x;
        const unsigned char* in = mapped + row * w * 4;
        for (int64_t i = 0; i < w; i++)
        {
            out[i].r = in[i * 4 + 0];
            out[i].g = in[i * 4 + 1];
            out[i].b = in[i * 4 + 2];
            out[i].a = 255;
        }
    }
    SDL_UnmapGPUTransferBuffer(m_device, m_readback_transfer);
    return true;
}
