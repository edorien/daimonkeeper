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
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN,
#if (BFDEBUG_LEVEL > 0)
        true
#else
        false
#endif
    );
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
    fs_info.num_samplers = 2; // set=2: binding 0 u_block_textures (2D array), binding 1 u_light_grid
    fs_info.num_uniform_buffers = 1; // set=3 binding=0: Lighting (per-pixel lighting pass)
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

    SDL_GPUVertexAttribute attrs[6] = {};
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
    pipe_info.vertex_input_state.num_vertex_attributes = 6;
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
    m_sampler = SDL_CreateGPUSampler(m_device, &sampler_info);
    if (m_sampler == nullptr)
    {
        ERRORLOG("SDL_CreateGPUSampler failed: %s", SDL_GetError());
        return false;
    }
    return true;
}

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
        tex_info.width = WORLDFRAME_BLOCK_SIZE;
        tex_info.height = WORLDFRAME_BLOCK_SIZE;
        tex_info.layer_count_or_depth = layers;
        tex_info.num_levels = 1;
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

    const Uint32 layer_bytes = WORLDFRAME_BLOCK_SIZE * WORLDFRAME_BLOCK_SIZE * 4;
    SDL_GPUTransferBufferCreateInfo tb_info = {};
    tb_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tb_info.size = layer_bytes * m_block_layers;
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
    SDL_memset(mapped, 255, layer_bytes);
    SDL_UnmapGPUTransferBuffer(m_device, m_block_transfer);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(m_device);
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureTransferInfo src = {};
    src.transfer_buffer = m_block_transfer;
    src.pixels_per_row = WORLDFRAME_BLOCK_SIZE;
    src.rows_per_layer = WORLDFRAME_BLOCK_SIZE;
    SDL_GPUTextureRegion dst = {};
    dst.texture = m_block_array;
    dst.layer = 0;
    dst.w = WORLDFRAME_BLOCK_SIZE;
    dst.h = WORLDFRAME_BLOCK_SIZE;
    dst.d = 1;
    SDL_UploadToGPUTexture(copy, &src, &dst, false);
    SDL_EndGPUCopyPass(copy);
    SDL_SubmitGPUCommandBuffer(cmd);
    return true;
}

// Returns the texture-array layer for a block pointer, queueing an upload
// (RGBA into upload_dst's next slot) when the block is new or its palette
// indices changed since it was last uploaded. Layer 0 (white) for null or
// once the array is exhausted (the cache is then flushed at the next frame).
uint32_t RendererGpu3D::resolve_block_layer(const unsigned char* texture, uint32_t& upload_count, unsigned char* upload_dst)
{
    if (texture == nullptr)
        return 0;

    unsigned char gathered[WORLDFRAME_BLOCK_SIZE * WORLDFRAME_BLOCK_SIZE];
    auto it = m_block_cache.find(texture);
    if (it != m_block_cache.end() && it->second.frame_seen == m_frame_id)
        return it->second.layer; // already verified this frame

    for (int row = 0; row < WORLDFRAME_BLOCK_SIZE; ++row)
        SDL_memcpy(gathered + row * WORLDFRAME_BLOCK_SIZE, texture + row * WORLDFRAME_BLOCK_STRIDE, WORLDFRAME_BLOCK_SIZE);

    uint32_t layer;
    if (it == m_block_cache.end())
    {
        if (m_next_layer >= m_block_layers && m_free_layers.empty())
        {
            // Full: reclaim the layers of blocks not used this frame (a
            // camera fly-through accumulates far more distinct blocks over
            // time than are ever visible at once) before giving up.
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
        it = m_block_cache.emplace(texture, BlockEntry()).first;
        it->second.layer = layer;
    }
    else
    {
        layer = it->second.layer;
        if (SDL_memcmp(it->second.indices, gathered, sizeof(gathered)) == 0)
        {
            it->second.frame_seen = m_frame_id;
            return layer;
        }
    }
    SDL_memcpy(it->second.indices, gathered, sizeof(gathered));
    it->second.frame_seen = m_frame_id;

    const unsigned char* palette = RendererGetActivePalette();
    unsigned char* out = upload_dst + static_cast<size_t>(upload_count) * WORLDFRAME_BLOCK_SIZE * WORLDFRAME_BLOCK_SIZE * 4;
    for (int i = 0; i < WORLDFRAME_BLOCK_SIZE * WORLDFRAME_BLOCK_SIZE; ++i)
    {
        // Same expansion the CPU rasterizer applies per texel
        // (bflib_render_gpoly.c): index 0 is transparent.
        TbPixel px = expand_indexed_pixel(gathered[i], palette);
        out[i * 4 + 0] = px.r;
        out[i * 4 + 1] = px.g;
        out[i * 4 + 2] = px.b;
        out[i * 4 + 3] = px.a;
    }
    ++upload_count;
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
    if (!init_pipeline() || !init_block_array() || !init_light_grid() || !m_sprites.Init(m_device))
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

void RendererGpu3D::destroy_gpu()
{
    if (m_device == nullptr)
        return;
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
    const unsigned char* palette = RendererGetActivePalette();
    if (!m_palette_valid || SDL_memcmp(m_palette_copy, palette, sizeof(m_palette_copy)) != 0)
    {
        // Palette changed (fade, lighting churn): every cached RGBA layer is
        // stale. Layers are simply reassigned.
        m_block_cache.clear();
        m_free_layers.clear();
        m_next_layer = 1;
        SDL_memcpy(m_palette_copy, palette, sizeof(m_palette_copy));
        m_palette_valid = true;
        m_overflow_logged = false;
    }

    // Walk the ops once, in draw order: terrain triangles become float
    // vertices in one stream, sprite ops quads in GpuSpriteRenderer's; draws
    // are recorded per contiguous run so the two interleave exactly as the
    // painter's algorithm did on the CPU.
    RPROF_BEGIN(RPS_BUILD);
    struct DrawRun { bool sprite; GpuSpriteRenderer::DrawCall sprite_draw; uint32_t first, count; };
    std::vector<DrawRun> runs;
    // Consecutive sprite draws of the same mode over adjacent vertices (a
    // creature shadow's two triangles, runs of plain sprites) merge into one draw.
    auto push_sprite_run = [&runs](const GpuSpriteRenderer::DrawCall& d) {
        if (!runs.empty() && runs.back().sprite && runs.back().sprite_draw.mode == d.mode &&
            runs.back().sprite_draw.first + runs.back().sprite_draw.count == d.first)
            runs.back().sprite_draw.count += d.count;
        else
            runs.push_back({ true, d, 0, 0 });
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
            const float layer = static_cast<float>(resolve_block_layer(item.texture, upload_count, upload_dst));
            const struct PolyPoint* src[3] = { &item.v0, &item.v1, &item.v2 };
            const uint32_t first = static_cast<uint32_t>(vertices.size());
            for (int v = 0; v < 3; ++v)
            {
                GpuVertex dst;
                dst.x = static_cast<float>(src[v]->X) + off_x;
                dst.y = static_cast<float>(src[v]->Y) + off_y;
                dst.u = static_cast<float>(src[v]->U >> 16);
                dst.v = static_cast<float>(src[v]->V >> 16);
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
            if (!runs.empty() && !runs.back().sprite && runs.back().first + runs.back().count == first)
                runs.back().count += 3;
            else
                runs.push_back({ false, {}, first, 3 });
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
    if (vertex_count > 0 || upload_count > 0 || m_sprites.HasWork() || lit)
    {
        SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
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
        const Uint32 layer_bytes = WORLDFRAME_BLOCK_SIZE * WORLDFRAME_BLOCK_SIZE * 4;
        for (uint32_t k = 0; k < upload_count; ++k)
        {
            SDL_GPUTextureTransferInfo src = {};
            src.transfer_buffer = m_block_transfer;
            src.offset = k * layer_bytes;
            src.pixels_per_row = WORLDFRAME_BLOCK_SIZE;
            src.rows_per_layer = WORLDFRAME_BLOCK_SIZE;
            SDL_GPUTextureRegion dst = {};
            dst.texture = m_block_array;
            dst.layer = m_upload_layers[k];
            dst.w = WORLDFRAME_BLOCK_SIZE;
            dst.h = WORLDFRAME_BLOCK_SIZE;
            dst.d = 1;
            SDL_UploadToGPUTexture(copy, &src, &dst, false);
        }
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
            SDL_GPUTextureSamplerBinding tex_bindings[2] = {};
            tex_bindings[0].texture = m_block_array;
            tex_bindings[0].sampler = m_sampler;
            tex_bindings[1].texture = m_light_grid;
            tex_bindings[1].sampler = m_light_sampler;
            SDL_BindGPUFragmentSamplers(pass, 0, tex_bindings, 2);
            SDL_DrawGPUPrimitives(pass, run.count, 1, run.first, 0);
        }
    }
    SDL_EndGPURenderPass(pass);
#if (BFDEBUG_LEVEL > 0)
    // Counters for the heavy-log RPROF report (see renderer/RendererProfile.h).
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
        RPROF_COUNT(RPC_BLOCK_UPLOADS, upload_count);
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
#endif
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
