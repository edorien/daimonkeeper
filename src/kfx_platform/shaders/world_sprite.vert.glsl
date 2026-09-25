#version 450

// gpu-v2 Phase C.2: RendererGpu3D's scaled-sprite vertex shader. One quad per
// captured sprite op (see renderer/WorldFrame.h's WorldFrameSpriteOp): the
// destination rectangle in window-relative screen pixels, plus per-sprite
// constants passed as flat attributes so the fragment shader can look up its
// source texel exactly the way the CPU scaling step tables would.
layout(location = 0) in vec2 in_position; // screen-space pixels (view offset already applied)
layout(location = 1) in vec2 in_local;    // pixel coordinates within the destination rectangle
layout(location = 2) in vec4 in_info0;    // atlas x, atlas y, xmap offset, ymap offset
layout(location = 3) in vec2 in_info1;    // LUT row, mode
layout(location = 4) in vec4 in_rgba;     // one-colour silhouette colour
layout(location = 5) in float in_depth;
layout(location = 6) in float in_view_depth; // hyperbolic view depth, for lit sprites   // 0 near .. 1 far (WorldFrameOp::depth)

layout(location = 0) out vec2 v_local;
layout(location = 1) flat out vec4 v_info0;
layout(location = 2) flat out vec2 v_info1;
layout(location = 3) flat out vec4 v_rgba;
layout(location = 4) flat out float v_depth; // 0 near .. 1 far (hyperbolic) -- for lit sprites' position reconstruction

// SDL_GPU's SPIR-V convention: vertex-stage uniform buffers are set = 1.
layout(set = 1, binding = 0) uniform ViewportUniforms {
    vec2 viewport_size;
};

void main()
{
    // Same NDC mapping as world_polygon.vert.glsl (SDL_GPU is NDC-Y-up).
    vec2 ndc = vec2(in_position.x / viewport_size.x * 2.0 - 1.0,
                    1.0 - in_position.y / viewport_size.y * 2.0);
    gl_Position = vec4(ndc, in_depth, 1.0);
    v_local = in_local;
    v_info0 = in_info0;
    v_info1 = in_info1;
    v_rgba = in_rgba;
    v_depth = in_view_depth;
}
