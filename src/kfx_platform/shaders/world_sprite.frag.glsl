#version 450

// gpu-v2 Phase C.2: RendererGpu3D's scaled-sprite fragment shader.
//
// Reproduces bflib_vidraw_spr_*.c's scaled sprite draws from a captured op:
// the destination pixel's source column/row come from the CPU path's own
// scaling step tables (uploaded as `u_lookup`), the texel index from the
// sprite atlas (8-bit indices, RLE-decoded on upload), and the colour from a
// 256-entry RGBA table (`u_lut`: row 0 = active palette, other rows = the
// remap/shade table the sprite was drawn with). The blend mode picks what is
// written; the pipeline's blend state (set per mode in RendererGpu3D.cpp)
// finishes the combination with what is already in the frame:
//   0 SOLID      colour                         (blend off)
//   1 GHOST1     ref/3, dest*2/3 via blend const  = (ref + 2*dest)/3
//   2 GHOST2     2*ref/3, dest*1/3                = (2*ref + dest)/3
//   3 ALPHA_ADD  |ramp delta|, dest + src         (render_alpha_blend, positive ramps)
//   4 ALPHA_SUB  |ramp delta|, dest - src         (its 'black' ramp)
//   5 ONECOLOUR  flat colour                       (blend off)
//   7 LIT        opaque, palette colour x max(base shade, dynamic lights) per pixel (blend off);
//                info1.x = 0 (palette row), v_rgba.rg = base shade x256 as two bytes
//   6 SHADOW     creature-shadow mask triangle: dest *= shade factor
//                (bflib_render_trig.c's VM_SpriteTranslucent / render_shade);
//                here info0 = mask atlas x, y, w, h and info1.x = the factor,
//                v_local = the interpolated mask coordinate (blend: dest * src)
layout(location = 0) in vec2 v_local;
layout(location = 1) flat in vec4 v_info0;
layout(location = 2) flat in vec2 v_info1;
layout(location = 3) flat in vec4 v_rgba;
layout(location = 4) flat in float v_depth;

layout(location = 0) out vec4 out_colour;

// SDL_GPU's SPIR-V convention: fragment-stage sampled textures are set = 2.
layout(set = 2, binding = 0) uniform sampler2D u_atlas;  // R8: sprite texel indices
layout(set = 2, binding = 1) uniform sampler2D u_lut;    // RGBA8 256 x rows
layout(set = 2, binding = 2) uniform sampler2D u_lookup; // R16_UNORM (SDL_GPU forbids sampling integer formats), 4096 wide: source column/row per destination column/row

#extension GL_GOOGLE_include_directive : require
#define LIGHT_GRID_BINDING 3
#include "lighting_common.glsl"

#define LOOKUP_WIDTH 4096
// A sprite's own map position (a billboard pixel) can fall inside a nearby wall column; ignore
// occluders this close to the start of the shadow ray (map units).
#define SPRITE_SHADOW_SKIP 300.0

// bflib_render.h's render_alpha_blend() ramp table (per-channel deltas in the
// original's 6-bit scale; ramp 6 is the negative 'black' one).
const ivec3 ramp_deltas[8] = ivec3[8](
    ivec3( 4,  4,  4), ivec3( 6,  4,  0), ivec3( 6,  1,  1), ivec3( 2,  2,  6),
    ivec3( 2,  6,  2), ivec3( 3,  0,  3), ivec3(-2, -2, -2), ivec3( 6,  3,  1));

void main()
{
    int mode = int(v_info1.y + 0.5);
    if (mode == 6)
    {
        // Sample the mask at the pixel's top-left corner, like the CPU
        // scanline stepping (see world_polygon.frag.glsl).
        vec2 uv = v_local - 0.5 * (dFdx(v_local) + dFdy(v_local));
        ivec2 t = clamp(ivec2(floor(uv)), ivec2(0), ivec2(int(v_info0.z + 0.5) - 1, int(v_info0.w + 0.5) - 1));
        int m = int(texelFetch(u_atlas, ivec2(int(v_info0.x + 0.5) + t.x, int(v_info0.y + 0.5) + t.y), 0).r * 255.0 + 0.5);
        if (m == 0)
            discard;
        out_colour = vec4(vec3(v_info1.x), 1.0);
        return;
    }

    ivec2 d = ivec2(floor(v_local));
    int xi = int(v_info0.z + 0.5) + d.x;
    int yi = int(v_info0.w + 0.5) + d.y;
    // UNORM v/65535 read back exactly by nearest texelFetch; round to recover the integer.
    uint sx = uint(texelFetch(u_lookup, ivec2(xi & (LOOKUP_WIDTH - 1), xi / LOOKUP_WIDTH), 0).r * 65535.0 + 0.5);
    uint sy = uint(texelFetch(u_lookup, ivec2(yi & (LOOKUP_WIDTH - 1), yi / LOOKUP_WIDTH), 0).r * 65535.0 + 0.5);
    if (sx == 0xFFFFu || sy == 0xFFFFu)
        discard;

    int idx = int(texelFetch(u_atlas, ivec2(int(v_info0.x + 0.5) + int(sx), int(v_info0.y + 0.5) + int(sy)), 0).r * 255.0 + 0.5);
    if (idx == 0)
        discard; // transparent texel

    vec4 c = texelFetch(u_lut, ivec2(idx, int(v_info1.x + 0.5)), 0);

    if (mode == 5)
    {
        out_colour = v_rgba;
    }
    else if (mode == 7)
    {
        if (c.a == 0.0)
            discard;
        float shade = (floor(v_rgba.r * 255.0 + 0.5) + 256.0 * floor(v_rgba.g * 255.0 + 0.5)) / 256.0;
        vec3 factor = vec3(shade_factor(shade));
        if (p0.x > 0.5)
        {
            // Sprites carry no vertex-shade distance fade in the engine (lens_mode 0), so none here.
            float z = depth_to_view_z(v_depth);
            vec3 map = lighting_map_position(z, gl_FragCoord.xy);
            factor = max(factor, lighting_dynamic(map, SPRITE_SHADOW_SKIP, z, false));
        }
        out_colour = vec4(clamp(c.rgb * factor, 0.0, 1.0), 1.0);
    }
    else if (mode == 3 || mode == 4)
    {
        if (idx < 1 || idx > 64)
            discard;
        int ramp = (idx - 1) / 8;
        int step = (idx - 1) % 8;
        if ((mode == 3) == (ramp == 6))
            discard; // the add pass draws every ramp but 'black', the subtract pass only it
        ivec3 delta = ramp_deltas[ramp] * step * 255 / 63;
        out_colour = vec4(vec3(abs(delta)) / 255.0, 1.0);
    }
    else if (mode == 1)
    {
        out_colour = vec4(c.rgb / 3.0, 1.0);
    }
    else if (mode == 2)
    {
        out_colour = vec4(c.rgb * (2.0 / 3.0), 1.0);
    }
    else
    {
        if (c.a == 0.0)
            discard;
        out_colour = c;
    }
}
