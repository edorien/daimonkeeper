#version 450

// gpu-v2 Phase C.1: RendererGpu3D's world-view vertex shader.
//
// Input layout mirrors struct PolyPoint (bflib_render.h) exactly, as
// flattened by WorldFramePolyItem (renderer/WorldFrame.h): X/Y are
// already-projected screen-space pixel coordinates (the same values
// draw_gpoly()/bflib_render_gpoly.c consumes directly, no further
// world/view/projection transform needed here -- that already happened on
// the CPU side, upstream of the WorldFrame IR). U/V are raw texel
// coordinates within a texture-cache block (0..block size, wrapped by the
// sampler); shade is bflib_render.h's 0..63 render_shade() input, passed
// through unresolved -- the fragment shader applies the exact same formula
// render_shade() uses, not a GPU-native lighting model, per
// docs/refactor/renderer/gpu-v2/04-architecture-and-ir-boundary.md's
// "translate the math, don't reinvent it" instruction.
layout(location = 0) in vec2 in_position; // screen-space pixels
layout(location = 1) in vec2 in_uv;       // raw texel coords into the bound block texture
layout(location = 2) in float in_shade;   // 0..63, bflib_render.h render_shade() domain
layout(location = 3) in float in_layer;   // texture-array layer of the block this triangle samples
layout(location = 5) in float in_zpos;    // hyperbolic view depth (PolyPoint::Z), for per-pixel lighting only
layout(location = 4) in float in_depth;   // 0 near .. 1 far (WorldFrameOp::depth); the depth buffer is cleared to 1

layout(location = 0) out vec2 v_uv;
layout(location = 1) out float v_shade;
layout(location = 2) out float v_layer;
layout(location = 3) out float v_depth; // 0 near .. 1 far, for per-pixel lighting's position reconstruction

// SDL_GPU's SPIR-V convention: vertex-stage uniform buffers are set = 1.
layout(set = 1, binding = 0) uniform ViewportUniforms {
    vec2 viewport_size; // pixels -- the same lbDrawSurface w/h the CPU path targets
};

void main()
{
    // Screen pixel -> clip-space NDC. SDL_GPU normalises every backend to
    // D3D/Metal-style conventions -- NDC Y points *up* (its Vulkan backend
    // flips the viewport to match), framebuffer origin top-left -- while
    // DK's screen-space Y grows downward, so Y is flipped here. (Found by
    // testing against a real Vulkan device, not assumed: a no-flip version
    // drew the mirrored half of the triangle.)
    vec2 ndc = vec2(in_position.x / viewport_size.x * 2.0 - 1.0,
                    1.0 - in_position.y / viewport_size.y * 2.0);
    gl_Position = vec4(ndc, in_depth, 1.0);
    v_uv = in_uv;
    v_shade = in_shade;
    v_layer = in_layer;
    v_depth = in_zpos;
}
