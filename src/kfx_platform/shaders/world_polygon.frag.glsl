#version 450

// gpu-v2 Phase C.1: RendererGpu3D's world-view fragment shader.
//
// Texture block size is a fixed 32x32 (block_dimension, engine_textures.c)
// -- in_uv arrives in raw texel units (0..31 for a normal tile, but not
// clamped to that range: scrolling textures, e.g. the abyss lava/water
// effect in engine_render.c, deliberately run U/V past the block edge and
// rely on wrapping, matching bflib_render_gpoly.c's TEXTURE_UV_WRAP_MASK
// bit-mask trick on the CPU side). The bound sampler's REPEAT address mode
// (set on the SDL_GPUSampler, not here) reproduces that same wrap exactly
// once UV is normalized to 0..1 by dividing by the block size below.
//
// The bound texture is already true-colour RGBA -- docs/refactor/renderer/
// gpu-v2/03-pixel-format-and-texture-cache.md's texture cache resolves the
// source 8-bit palette index through the active palette once, at cache-fill
// time, specifically so this shader never needs a second, palette-lookup
// texture fetch. See that file for why (a dependent read every fragment,
// and it fights per-pixel lighting).
#extension GL_GOOGLE_include_directive : require
#define LIGHT_GRID_BINDING 1
#define TEXTURE_BLOCK_SIZE 32.0

layout(location = 0) in vec2 v_uv;
layout(location = 1) in float v_shade;
layout(location = 2) in float v_layer;
layout(location = 3) in float v_depth;

layout(location = 0) out vec4 out_colour;

// SDL_GPU's SPIR-V convention: fragment-stage sampled textures are set = 2.
// One 32x32 layer per distinct texture block, managed by RendererGpu3D's block cache.
layout(set = 2, binding = 0) uniform sampler2DArray u_block_textures;
#include "lighting_common.glsl"
void main()
{
    // The CPU rasterizer (bflib_render_gpoly.c) evaluates U/V/shade at each
    // pixel's top-left corner, the GPU at its centre: step half a pixel back
    // along the screen-space gradient so nearest-sampled texel boundaries and
    // shade fall on the same pixels as the CPU path.
    vec2 uv = v_uv - 0.5 * (dFdx(v_uv) + dFdy(v_uv));
    float shade_at_corner = v_shade - 0.5 * (dFdx(v_shade) + dFdy(v_shade));
    vec4 texel = texture(u_block_textures, vec3(uv / TEXTURE_BLOCK_SIZE, floor(v_layer + 0.5)));

    // bflib_render.h's render_shade(), translated directly rather than
    // reinvented (see shade_factor() in lighting_common.glsl).
    vec3 factor = vec3(shade_factor(shade_at_corner));
    if (p0.x > 0.5)
    {
        float z = depth_to_view_z(v_depth);
        vec3 map = lighting_map_position(z, gl_FragCoord.xy);
        factor = max(factor, lighting_dynamic(map, 0.0, z, true));
    }
    vec3 shaded = clamp(texel.rgb * factor, 0.0, 1.0);

    if (texel.a == 0.0)
        discard; // transparent texel: must not write depth either
    out_colour = vec4(shaded, texel.a);
}
