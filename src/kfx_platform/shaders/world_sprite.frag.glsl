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
// A shadow (6) with 8 added is soft (Phase 10): its mask is coverage, sampled bilinear.
// HD sprites (docs/refactor/sprites/, Phase 4) add 8 to the mode (16 more to apply the colour transform's table;
// their atlas layer times 256): the texel comes from the RGBA HD atlas at the lookups' HD coordinates (alpha below 128 transparent), and its
// colour is the LUT row read as a curve row -- red from entry r's red, green from entry g's green, blue from entry
// b's blue, after the table; black skips the table and takes entries 0, 1, 2's alpha (renderer/WorldFrame.h).
// An HD sprite drawn as its original (no room in the HD atlas) has no 8 but its scale in bits 32..224 instead.
// An HD sprite's lookups are sixteenths of an HD texel (each pixel's centre); TEXTURE_FILTER in bits 16384..32768:
// 0 Nearest (the texel, value >> 4), 1 Sharp (the texel inside it, a blend across its edges), 2 Bilinear (Trilinear
// too: a sprite's level is chosen per draw). Filtered texels are blended after their colour transform, by opacity,
// and the silhouette cut where half of the blend is opaque; the atlas keeps a transparent texel round each frame.
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
layout(set = 2, binding = 4) uniform sampler2DArray u_hd_atlas; // RGBA8 layers: HD sprite texels
layout(set = 2, binding = 5) uniform sampler2D u_transform; // RGBA8 4096 x 64: the HD colour transform's table, entry r6 << 12 | g6 << 6 | b6

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

// An HD texel's colour (alpha 0 if transparent): the colour transform's table first when `table`, then the LUT row as
// a curve row; black skips the table and takes entries 0, 1, 2's alpha.
vec4 hd_colour(ivec3 at, int row, bool table)
{
    vec4 t = texelFetch(u_hd_atlas, at, 0);
    if (t.a < 0.5)
        return vec4(0.0);
    ivec3 t8 = ivec3(t.rgb * 255.0 + 0.5);
    if (t8 == ivec3(0))
        return vec4(texelFetch(u_lut, ivec2(0, row), 0).a, texelFetch(u_lut, ivec2(1, row), 0).a, texelFetch(u_lut, ivec2(2, row), 0).a, 1.0);
    if (table)
    {
        int k = ((t8.r >> 2) << 12) | ((t8.g >> 2) << 6) | (t8.b >> 2);
        t8 = ivec3(texelFetch(u_transform, ivec2(k & 4095, k >> 12), 0).rgb * 255.0 + 0.5);
    }
    return vec4(texelFetch(u_lut, ivec2(t8.r, row), 0).r, texelFetch(u_lut, ivec2(t8.g, row), 0).g,
                texelFetch(u_lut, ivec2(t8.b, row), 0).b, 1.0);
}

void main()
{
    int mode_bits = int(v_info1.y + 0.5);
    int mode = mode_bits & 7;
    bool hd = (mode_bits & 8) != 0;
    if (mode == 6 && hd)
    {
        // A soft shadow (Phase 10): the mask holds coverage 0..255; sampled bilinear at the pixel centre, the frame
        // darkened as far as it is covered.
        vec2 q = v_local - 0.5;
        ivec2 i0 = ivec2(floor(q));
        vec2 f = q - floor(q);
        ivec2 lim = ivec2(int(v_info0.z + 0.5) - 1, int(v_info0.w + 0.5) - 1);
        ivec2 at0 = ivec2(int(v_info0.x + 0.5), int(v_info0.y + 0.5));
        float cover = 0.0;
        for (int k = 0; k < 4; k++)
        {
            ivec2 o = ivec2(k & 1, k >> 1);
            float w = ((o.x == 1) ? f.x : 1.0 - f.x) * ((o.y == 1) ? f.y : 1.0 - f.y);
            ivec2 t = clamp(i0 + o, ivec2(0), lim);
            cover += w * texelFetch(u_atlas, at0 + t, 0).r;
        }
        if (cover <= 0.0)
            discard;
        out_colour = vec4(mix(vec3(1.0), vec3(v_info1.x), cover), 1.0);
        return;
    }
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
    // HD: the pixel's centre in HD texels, and how many texels a pixel steps (Sharp's blend width), taken before any
    // discard (derivatives need the whole quad); a skipped neighbour's 0xFFFF makes it meaningless, hence the clamp.
    vec2 hd_pos = vec2(float(sx), float(sy)) / 16.0;
    vec2 hd_step = clamp(fwidth(hd_pos), vec2(1.0 / 64.0), vec2(2.0));
    if (sx == 0xFFFFu || sy == 0xFFFFu)
        discard;
    // An HD sprite drawn as its original (the HD atlas full): its lookups are HD coordinates in sixteenths, scale
    // (bits 32..224) times the original's.
    int hd_scale = (mode_bits >> 5) & 7;
    if (!hd && hd_scale >= 1)
    {
        sx /= uint(16 * hd_scale);
        sy /= uint(16 * hd_scale);
    }

    int idx = 1;
    vec4 c;
    if (hd)
    {
        ivec2 origin = ivec2(int(v_info0.x + 0.5), int(v_info0.y + 0.5));
        int layer = (mode_bits >> 8) & 63;
        int filtering = (mode_bits >> 14) & 3;
        int row = int(v_info1.x + 0.5);
        bool table = (mode_bits & 16) != 0;
        // Additive art (alpha-ramp draws, Phase 9): the add pass (3) writes the add texels, the subtract pass (4) the
        // sub texels beside them (v_rgba's first two bytes: their offset); no colour transform; coverage from add.
        bool additive = (mode == 3 || mode == 4);
        int sub_dx = int(v_rgba.r * 255.0 + 0.5) + 256 * int(v_rgba.g * 255.0 + 0.5);
        if (additive)
        {
            vec2 p = hd_pos;
            if (filtering == 1)
            {
                vec2 seam = floor(p + 0.5);
                p = seam + clamp((p - seam) / hd_step, -0.5, 0.5);
            }
            vec4 acc = vec4(0.0);
            if (filtering == 0)
            {
                ivec2 at = origin + ivec2(int(sx >> 4u), int(sy >> 4u));
                vec4 a = texelFetch(u_hd_atlas, ivec3(at, layer), 0);
                vec4 m = texelFetch(u_hd_atlas, ivec3(at + ivec2(sub_dx, 0), layer), 0);
                acc = (a.a < 0.5) ? vec4(0.0) : vec4((mode == 4) ? m.rgb : a.rgb, 1.0);
            }
            else
            {
                vec2 q = p - 0.5;
                ivec2 i0 = ivec2(floor(q));
                vec2 f = q - floor(q);
                for (int k = 0; k < 4; k++)
                {
                    ivec2 o = ivec2(k & 1, k >> 1);
                    float w = ((o.x == 1) ? f.x : 1.0 - f.x) * ((o.y == 1) ? f.y : 1.0 - f.y);
                    ivec2 at = origin + i0 + o;
                    vec4 a = texelFetch(u_hd_atlas, ivec3(at, layer), 0);
                    float cov = (a.a < 0.5) ? 0.0 : 1.0;
                    vec3 v = (mode == 4) ? texelFetch(u_hd_atlas, ivec3(at + ivec2(sub_dx, 0), layer), 0).rgb : a.rgb;
                    acc += vec4(v * cov, cov) * w;
                }
                acc = (acc.a > 0.0) ? vec4(acc.rgb / acc.a, acc.a) : vec4(0.0);
            }
            if (acc.a < 0.5)
                discard;
            out_colour = vec4(acc.rgb, 1.0);
            return;
        }
        if (filtering == 0)
        {
            c = hd_colour(ivec3(origin + ivec2(int(sx >> 4u), int(sy >> 4u)), layer), row, table);
        }
        else
        {
            vec2 p = hd_pos;
            if (filtering == 1)
            {
                // Sharp, as terrain's: the texel's own colour inside it, a blend over the one pixel across its edge.
                vec2 seam = floor(p + 0.5);
                p = seam + clamp((p - seam) / hd_step, -0.5, 0.5);
            }
            vec2 q = p - 0.5;
            ivec2 i0 = ivec2(floor(q));
            vec2 f = q - floor(q);
            vec4 acc = vec4(0.0);
            for (int k = 0; k < 4; k++)
            {
                ivec2 o = ivec2(k & 1, k >> 1);
                float w = ((o.x == 1) ? f.x : 1.0 - f.x) * ((o.y == 1) ? f.y : 1.0 - f.y);
                vec4 t = hd_colour(ivec3(origin + i0 + o, layer), row, table);
                acc += vec4(t.rgb * t.a, t.a) * w;
            }
            c = (acc.a > 0.0) ? vec4(acc.rgb / acc.a, acc.a) : vec4(0.0);
        }
        if (c.a < 0.5)
            discard; // transparent texel (filtered: less than half the blend opaque)
        c.a = 1.0;
    }
    else
    {
        idx = int(texelFetch(u_atlas, ivec2(int(v_info0.x + 0.5) + int(sx), int(v_info0.y + 0.5) + int(sy)), 0).r * 255.0 + 0.5);
        if (idx == 0)
            discard; // transparent texel
        c = texelFetch(u_lut, ivec2(idx, int(v_info1.x + 0.5)), 0);
    }

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
