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
layout(location = 4) in vec2 v_uv_whole; // v_uv in whole texels, as a frame without HD has it under Nearest

layout(location = 0) out vec4 out_colour;

// SDL_GPU's SPIR-V convention: fragment-stage sampled textures are set = 2.
// One 32x32 layer per distinct texture block, managed by RendererGpu3D's block cache.
layout(set = 2, binding = 0) uniform sampler2DArray u_block_textures;
#include "lighting_common.glsl"

// Palette effects as a colour transform (bflib_colour_transform.h; docs/newFeatures/texture-resolution/
// 02-design.md section 5.1.3). The cached layers keep the colours of the palette they were expanded through;
// while a palette effect a colour function reproduces exactly is on, transform_mode.x = 1 maps a texel through
// per-channel curves (the fades, white) and 2 through a 64^3 table (freeze, lightning). Texel 0's black stays
// black, as on the CPU.
layout(set = 2, binding = 2) uniform sampler2D u_transform_curves; // 256 x 1: r, g, b curves in their channels
layout(set = 2, binding = 3) uniform sampler3D u_transform_table;  // 64^3, indexed by 6-bit channels
layout(set = 3, binding = 1) uniform Transform {
    vec4 transform_mode; // x: colour transform (0 none, 1 curves, 2 table); y: TEXTURE_FILTER (RENDERER_TEXFILTER_*);
                         // z: in a Nearest frame with HD blocks, the layers' level of the original 32x32 block
};

vec3 colour_transform(vec3 c)
{
    if (transform_mode.x < 0.5)
        return c;
    ivec3 i = ivec3(c * 255.0 + 0.5);
    if ((i.r | i.g | i.b) == 0)
        return c;
    if (transform_mode.x < 1.5)
        return vec3(texelFetch(u_transform_curves, ivec2(i.r, 0), 0).r,
                    texelFetch(u_transform_curves, ivec2(i.g, 0), 0).g,
                    texelFetch(u_transform_curves, ivec2(i.b, 0), 0).b);
    // Stored with blue varying fastest (bflib_colour_transform.h: index r6 << 12 | g6 << 6 | b6), so blue is x.
    return texelFetch(u_transform_table, ivec3(i.b, i.g, i.r) >> 2, 0).rgb;
}
void main()
{
    // The CPU rasterizer (bflib_render_gpoly.c) evaluates U/V/shade at each
    // pixel's top-left corner, the GPU at its centre: step half a pixel back
    // along the screen-space gradient so nearest-sampled texel boundaries and
    // shade fall on the same pixels as the CPU path.
    float shade_at_corner = v_shade - 0.5 * (dFdx(v_shade) + dFdy(v_shade));
    float layer = floor(v_layer + 0.5);
    vec4 texel;
    if (transform_mode.y < 0.5)
    {
        // TEXTURE_FILTER Nearest: what the CPU draws (whole-texel UVs from the vertex shader, REPEAT).
        vec2 uv = v_uv - 0.5 * (dFdx(v_uv) + dFdy(v_uv));
        if (transform_mode.z > 0.5)
        {
            // A frame with HD blocks (texture resolution): where a frame without HD would sample the original 32x32
            // block or coarser, sample the same -- its level in these bigger layers (z), at the same whole-texel
            // UV and the same level offset (a nearest mip: ceil(lambda + 0.5) - 1) -- so the far zooms draw as they
            // always did; finer, the HD block at its own UV.
            vec2 uvw = v_uv_whole - 0.5 * (dFdx(v_uv_whole) + dFdy(v_uv_whole));
            float lambda = textureQueryLod(u_block_textures, uvw / TEXTURE_BLOCK_SIZE).y - transform_mode.z;
            float level = ceil(lambda + 0.5) - 1.0;
            if (level >= 0.0)
            {
                float last = float(textureQueryLevels(u_block_textures) - 1);
                texel = textureLod(u_block_textures, vec3(uvw / TEXTURE_BLOCK_SIZE, layer), min(transform_mode.z + level, last));
            }
            else
                texel = texture(u_block_textures, vec3(uv / TEXTURE_BLOCK_SIZE, layer));
        }
        else
            texel = texture(u_block_textures, vec3(uv / TEXTURE_BLOCK_SIZE, layer));
    }
    else
    {
        // Sharp (transform_mode.y 1) and Bilinear (2), linear sampler, the UVs' fractions kept. Sharp: a texel's
        // own colour inside it, a blend over the one pixel across its edge (nearest-to-centre snapping by
        // fwidth). Gradients from the unsnapped UV, so a mip level (Phase 4) isn't disturbed by the snapping.
        vec2 uv = v_uv;
        if (transform_mode.y < 1.5)
        {
            vec2 seam = floor(uv + 0.5);
            vec2 w = max(fwidth(uv), vec2(1.0 / 4096.0));
            uv = seam + clamp((uv - seam) / w, -0.5, 0.5);
        }
        texel = textureGrad(u_block_textures, vec3(uv / TEXTURE_BLOCK_SIZE, layer),
                            dFdx(v_uv) / TEXTURE_BLOCK_SIZE, dFdy(v_uv) / TEXTURE_BLOCK_SIZE);
    }
    texel.rgb = colour_transform(texel.rgb);

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
