// gpu-v2 lighting pass -- shared by world_polygon.frag.glsl (terrain) and world_sprite.frag.glsl
// (lit sprites). The includer defines LIGHT_GRID_BINDING (its set-2 sampler slot for the grid) first.
//
// Rebuilds a pixel's map position from its depth + screen position, evaluates the frame's dynamic
// lights there with the engine's classic falloff, and casts a shadow ray to each through a height
// field of the map's solid columns. Mirrors WorldFrameLighting (renderer/WorldFrame.h).
// Per-subtile solid column height in subtiles (R8, texel = subtile) -- what the shadow rays march through.
layout(set = 2, binding = LIGHT_GRID_BINDING) uniform sampler2D u_light_grid;

// gpu-v2 Phase C.5 lighting pass. Fragment uniform buffers are set = 3. Mirrors WorldFrameLighting.
#define MAX_LIGHTS 64
#define DEPTH_NEAR_Z 32.0
#define DEPTH_FAR_Z 65536.0
layout(set = 3, binding = 0) uniform Lighting {
    vec4 p0;        // x: enabled, yz: window-relative-to-target pixel centre (cx + view_x, cy + view_y), w: lens
    vec4 fade;      // fade_min, fade_max, fade_scaler, fade_range
    vec4 map_x;     // map x = dot(map_x.xyz, view) + map_x.w
    vec4 map_y;
    vec4 map_z;
    vec4 grid;      // x,y: grid texture size in texels; z: light count
    vec4 lights[MAX_LIGHTS * 2]; // per light: [2i] = x, y, z, radius; [2i+1] = intensity
};

#define SHADOW_STEPS 24
#define SUBTILE 256.0

// Soft shadows: a light is treated as a small disc, not a point. The occlusion of the segment
// pixel->light is measured over up to SOFT_RAYS rays (3 where the centre and both edges agree) aimed at points spread across the light's width (sideways
// in map xy), and each ray's own occlusion is a smooth ramp on how far the ray clears a column's top
// instead of a hard yes/no. Rays converge on the pixel, so the penumbra is sharp next to an occluder and
// widens with distance from it. SOFT_RAYS 1 with SOFT_LIGHT_SIZE 0 restores hard shadows.
#define SOFT_RAYS 5
#define SOFT_LIGHT_SIZE 128.0   // half-width of a light, map units (256 per subtile)
#define SOFT_BAND_BASE 16.0    // vertical ramp half-height at the pixel, map units
#define SOFT_BAND_SLOPE 0.10   // ... growing per unit of distance from the pixel

// Occlusion (0 lit .. 1 fully shadowed) of the segment from pixel position p to light position l.
// 2D grid DDA over subtiles; a cell shades in proportion to how far the ray, at its lowest point inside
// the cell, is below the cell's solid height. The pixel's own cell and the light's cell never occlude.
// p is nudged toward the light and up a little so surfaces don't shadow themselves.
float shadow_ray(vec3 p, vec3 l, float skip_dist)
{
    vec2 dir = l.xy - p.xy;
    float len = length(dir);
    if (len < 16.0)
        return 0.0;
    vec3 s = p + vec3(dir / len * 8.0, 8.0);
    vec2 s2 = s.xy / SUBTILE;
    vec2 e2 = l.xy / SUBTILE;
    vec2 d = e2 - s2;
    ivec2 cell = ivec2(floor(s2));
    ivec2 end_cell = ivec2(floor(e2));
    ivec2 stp = ivec2(d.x >= 0.0 ? 1 : -1, d.y >= 0.0 ? 1 : -1);
    vec2 t_delta = vec2(abs(d.x) > 1e-6 ? abs(1.0 / d.x) : 1e9, abs(d.y) > 1e-6 ? abs(1.0 / d.y) : 1e9);
    vec2 next_edge = vec2(stp.x > 0 ? float(cell.x + 1) : float(cell.x), stp.y > 0 ? float(cell.y + 1) : float(cell.y));
    vec2 t_max = vec2(abs(d.x) > 1e-6 ? (next_edge.x - s2.x) / d.x : 1e9, abs(d.y) > 1e-6 ? (next_edge.y - s2.y) / d.y : 1e9);
    float t_in = 0.0;
    float occ = 0.0;
    for (int i = 0; i < SHADOW_STEPS; ++i)
    {
        float t_out = min(min(t_max.x, t_max.y), 1.0);
        if (t_max.x < t_max.y) { cell.x += stp.x; t_max.x += t_delta.x; }
        else                    { cell.y += stp.y; t_max.y += t_delta.y; }
        t_in = t_out;
        if (t_in >= 1.0 || cell == end_cell)
            break;
        if (t_in * len < skip_dist)
            continue; // near the start: a sprite's own position may sit inside a wall column
        float solid = texelFetch(u_light_grid, cell, 0).r * 255.0 * SUBTILE;
        if (solid <= p.z + 96.0)
            continue; // a column no taller than the pixel's own height cannot shade it (wall tops, floors)
        float t_next = min(min(t_max.x, t_max.y), 1.0);
        float z_low = min(mix(s.z, l.z, t_in), mix(s.z, l.z, t_next));
        float band = SOFT_BAND_BASE + SOFT_BAND_SLOPE * t_in * len;
        occ = max(occ, 1.0 - smoothstep(-band, band, z_low - solid));
        if (occ >= 0.999)
            return 1.0;
    }
    return occ;
}

float shadow_amount(vec3 p, vec3 l, float skip_dist)
{
#if SOFT_RAYS <= 1
    return shadow_ray(p, l, skip_dist);
#else
    vec2 dir = l.xy - p.xy;
    float len = length(dir);
    if (len < 16.0)
        return 0.0;
    vec2 side = vec2(-dir.y, dir.x) / len * SOFT_LIGHT_SIZE;
    float c = shadow_ray(p, l, skip_dist);
    float a = shadow_ray(p, vec3(l.xy + side, l.z), skip_dist);
    float b = shadow_ray(p, vec3(l.xy - side, l.z), skip_dist);
    if (abs(a - c) < 0.01 && abs(b - c) < 0.01)
        return c;   // centre and both edges agree: fully lit or fully shadowed, no penumbra here
    // in a penumbra: refine with two more rays halfway to each edge (5 evenly spread across the light)
    float a2 = shadow_ray(p, vec3(l.xy + side * 0.5, l.z), skip_dist);
    float b2 = shadow_ray(p, vec3(l.xy - side * 0.5, l.z), skip_dist);
    return (a + a2 + c + b2 + b) * 0.2;
#endif
}


// Depth (0 near .. 1 far, hyperbolic in view z -- WorldFrameOp::depth) to view-space z.
float depth_to_view_z(float d)
{
    d = clamp(d, 0.0, 0.999999);
    return DEPTH_NEAR_Z / (1.0 - d * (1.0 - DEPTH_NEAR_Z / DEPTH_FAR_Z));
}

// Map-space position of the pixel at fragcoord with view-space depth z.
vec3 lighting_map_position(float z, vec2 fragcoord)
{
    vec2 s = fragcoord - p0.yz;
    vec3 view = vec3(s.x * z / p0.w, -s.y * z / p0.w, z);
    return vec3(dot(map_x.xyz, view) + map_x.w, dot(map_y.xyz, view) + map_y.w, dot(map_z.xyz, view) + map_z.w);
}

// bflib_render.h render_shade(): shade 0..63 -> colour scale factor.
float shade_factor(float shade)
{
    shade = clamp(shade, 0.0, 63.0);
    return ((shade <= 31.0) ? shade : (3.0 * shade - 64.0)) / 32.0;
}

// The engine fades vertex shade with distance (render_distance = view z). fade.w is the engine's
// fade_range = (fade_max - fade_min) >> 8 -- units of 256 -- so the raw span is fade.w * 256.
float lighting_fade(float dyn, float z)
{
    return (z <= fade.x || fade.w <= 0.0) ? dyn
         : (z < fade.y) ? dyn * (fade.z - z) / (fade.w * 256.0) + 0.5
         : 0.5;
}

// Distance attenuation of a light, s = 1 - distance/radius (1 at the light, 0 at its radius). The
// original engine used the linear ramp s -- a cone with a hard edge, a limit of 1990s lighting tables.
// Now: smoothstep, so a light has a soft core and fades out with zero slope at its radius instead of
// ending in a visible ring. Set LIGHT_FALLOFF_LINEAR to 1 for the original curve.
#define LIGHT_FALLOFF_LINEAR 0
float light_attenuation(float s)
{
#if LIGHT_FALLOFF_LINEAR
    return s;
#else
    return s * s * (3.0 - 2.0 * s);
#endif
}

// The strongest unoccluded light at `map`, as a per-channel colour scale (what render_shade gives the
// light's shade, times the light's colour). Lights combine by per-channel maximum, like the engine's
// max-combined lightness, so overlapping coloured lights blend towards white. apply_fade: the terrain's
// distance fade at view depth z (sprites get none, as in the engine's lens mode 0).
vec3 lighting_dynamic(vec3 map, float skip_dist, float z, bool apply_fade)
{
    vec3 best = vec3(0.0);
    for (int i = 0; i < int(grid.z); ++i)
    {
        vec4 la = lights[2 * i];
        vec4 lb = lights[2 * i + 1];
        float s = clamp(1.0 - distance(map.xy, la.xy) / la.w, 0.0, 1.0);
        float shade = lb.x * light_attenuation(s);
        if (apply_fade)
            shade = lighting_fade(shade, z);
        vec3 f = shade_factor(shade) * lb.yzw;
        if (max(f.r, max(f.g, f.b)) > max(best.r, max(best.g, best.b)))
            best = max(best, f * (1.0 - shadow_amount(map, la.xyz, skip_dist)));
    }
    return best;
}
