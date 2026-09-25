/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file WorldFrame.h
 *     The gpu-v2 Phase C seam: an ordered, backend-agnostic list of the
 *     world-view draw operations for one frame, handed down from the
 *     software rasterizer's draw primitives to a GPU-backed IRenderer.
 * @par Purpose:
 *     docs/refactor/renderer/gpu-v2/04-architecture-and-ir-boundary.md's
 *     "WorldFrame IR". Ops are recorded in draw (painter's) order while the
 *     engine walks its bucket list, so terrain and sprites interleave
 *     correctly on the GPU (C.2). Recorded through the WorldFrameBuilder
 *     (RendererWorldFrame* in RendererManager.h), consumed by
 *     IRenderer::SubmitWorldFrame().
 * @par Op kinds:
 *     WF_OP_POLY   -- a textured world triangle (terrain/walls; the CPU
 *                     path's draw_gpoly()); texture is a block_ptrs[] pointer.
 *     WF_OP_SPRITE -- a scaled sprite, captured from the software sprite
 *                     dispatchers (bflib_vidraw*.c) with the exact scaling
 *                     step tables the CPU path would have used, so the GPU
 *                     reproduces the same source->destination pixel mapping.
 *     WF_OP_SHADOW -- one triangle of a creature shadow, captured from the
 *                     software trig() (VM_SpriteTranslucent): darkens what is
 *                     already in the frame wherever the small shadow mask
 *                     (copied at capture time, since the engine reuses its
 *                     scratch buffer) is set.
 * @par Comment:
 *     Raw CPU pointers (block/sprite data) are used rather than handles:
 *     kfx_platform cannot depend on kfx_render's data, and the GPU backend
 *     owns its own caches keyed on them (see RendererGpu3D.h).
 */
/******************************************************************************/
#pragma once

#include "bflib_render.h" // struct PolyPoint
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

// Texture-block geometry as engine_textures.c lays it out (block_dimension,
// block_dimension * block_count_per_row) -- kfx_platform cannot read those
// kfx_render globals, so the constants live here; a texture pointer in a
// WorldFramePolyItem addresses the block's top-left texel of an 8-bit
// palette-indexed atlas whose rows are WORLDFRAME_BLOCK_STRIDE bytes apart.
#define WORLDFRAME_BLOCK_SIZE   32
#define WORLDFRAME_BLOCK_STRIDE 256

// gpu-v2 Phase C.5: per-vertex depth. PolyPoint::Z holds the point's view-space
// depth as a fixed-point value 0 (near) .. WORLDFRAME_DEPTH_ONE (far), chosen
// hyperbolic in view z -- (1 - near/z) / (1 - near/far) -- because that is the
// function that stays *linear in screen space* under perspective, so the
// rasterizer's plain interpolation of Z between vertices (CPU or GPU) gives the
// exact per-pixel depth. Also the depth a GPU depth buffer stores directly.
#define WORLDFRAME_DEPTH_ONE    16777216
#define WORLDFRAME_DEPTH_NEAR_Z 32
#define WORLDFRAME_DEPTH_FAR_Z  65536

static inline double worldframe_depth_from_view_z_f(double z)
{
    if (z <= WORLDFRAME_DEPTH_NEAR_Z) return 0.0;
    if (z >= WORLDFRAME_DEPTH_FAR_Z) return 1.0;
    return (1.0 - WORLDFRAME_DEPTH_NEAR_Z / z) / (1.0 - (double)WORLDFRAME_DEPTH_NEAR_Z / WORLDFRAME_DEPTH_FAR_Z);
}

static inline int64_t worldframe_depth_from_view_z(int64_t z)
{
    return (int64_t)(worldframe_depth_from_view_z_f((double)z) * WORLDFRAME_DEPTH_ONE + 0.5);
}

typedef struct WorldFramePolyItem {
    struct PolyPoint v0;
    struct PolyPoint v1;
    struct PolyPoint v2;
    unsigned char *texture; // block texture source; see WORLDFRAME_BLOCK_*
} WorldFramePolyItem;

enum WorldFrameOpKind {
    WF_OP_POLY   = 0,
    WF_OP_SPRITE = 1,
    WF_OP_SHADOW = 2,
};

// How a sprite's texels combine with what is already in the frame --
// mirrors the CPU sprite families in bflib_vidraw_spr_*.c.
enum WorldFrameSpriteMode {
    WFS_SOLID     = 0, // opaque (colour from the op's LUT row)
    WFS_GHOST1    = 1, // Lb_SPRITE_TRANSPAR4: (ref + 2*dest) / 3   (render_ghost_blend)
    WFS_GHOST2    = 2, // Lb_SPRITE_TRANSPAR8: (2*ref + dest) / 3   (render_ghost_blend_2)
    WFS_ALPHA     = 3, // DrawAlphaSprite: dest +/- ramp step        (render_alpha_blend)
    // 4 is reserved: RendererGpu3D draws WFS_ALPHA sprites twice (add pass, subtract pass) and uses 4 internally for the latter.
    WFS_ONECOLOUR = 5, // silhouette in one flat colour (rgba)
    WFS_LIT       = 7, // per-pixel-lit opaque sprite (palette colours, NOT pre-shaded): the op's rgba field holds the base shade x256 (8.8, 0..63 shade units); the GPU applies max(base, dynamic lights) per pixel. Only recorded while the frame carries lighting inputs.
    WFS_SHADOW    = 6, // internal to RendererGpu3D: WF_OP_SHADOW triangles (not a sprite op mode)
};

// A scaled sprite as the CPU rasterizer would draw it. `rle` is the
// sprite's own run-length-encoded row data (signed-byte runs: >0 literal
// texel bytes follow, <0 skip, 0 = end of row); texel 0 is transparent.
// The destination is dst_w x dst_h at (dst_x, dst_y), window-relative like
// polygon vertices; for each destination column/row, lookup[xmap_off + dx] /
// lookup[ymap_off + dy] is the source column/row to sample (0xFFFF = draw
// nothing there) -- the CPU path's scaling step tables, flip and clipping
// already applied. lut_row selects a 256-entry RGBA colour table in
// WorldFrame::lut: row 0 is the active palette, further rows are the
// remap/shade tables the sprite was drawn with.
typedef struct WorldFrameSpriteOp {
    const unsigned char *rle;
    int32_t src_w, src_h;
    int32_t dst_x, dst_y, dst_w, dst_h;
    uint32_t xmap_off, ymap_off;
    uint32_t lut_row;
    uint32_t mode;
    uint32_t rgba; // WFS_ONECOLOUR: r | g<<8 | b<<16 | a<<24
} WorldFrameSpriteOp;

// One triangle of a creature shadow. The mask is a w x h byte rectangle
// (WorldFrame::pixels + pix_off, row stride w; non-zero = inside the shadow)
// cut out of the engine's 256x256 scratch buffer; u/v are the triangle's
// texture coordinates in mask pixels (fractions kept). x/y are window-
// relative screen pixels. Where the mask is set the frame is shaded by
// `shade` (render_shade's 0..63 domain, already clamped).
typedef struct WorldFrameShadowOp {
    int32_t x[3], y[3];
    float u[3], v[3];
    uint32_t pix_off;
    int32_t w, h;
    uint64_t hash; // of the mask bytes, for the GPU-side mask cache
    uint32_t shade;
} WorldFrameShadowOp;

typedef struct WorldFrameOp {
    uint32_t kind; // enum WorldFrameOpKind
    // gpu-v2 Phase C.5 depth infrastructure: 0 = nearest, 1 = farthest
    // (the GPU depth buffer is cleared to 1). Recorded ops carry the depth
    // bucket of the engine's draw list they were queued in (bucket index /
    // bucket count -- a world-distance quantisation), forced non-increasing
    // in submission order by the recorder so a depth test can never reject
    // an op the painter's algorithm would have drawn over an earlier one.
    float depth;
    // Hyperbolic view-space depth (worldframe_depth_from_view_z) of the op's position, used ONLY to
    // rebuild positions for per-pixel lighting -- independent of `depth`, which decides visibility (and
    // in painter-parity mode is a clamped linear bucket value that could not be inverted to a position).
    float view_depth;
    union {
        WorldFramePolyItem poly;
        WorldFrameSpriteOp sprite;
        WorldFrameShadowOp shadow;
    } u;
} WorldFrameOp;

// gpu-v2 Phase C.5 lighting pass (per-pixel lighting): dynamic point lights
// evaluated per terrain pixel on the GPU, in place of the engine baking them into
// its per-subtile lightness grid. Positions are engine map units (256 per subtile);
// intensity is in the engine's shade domain (0..63).
#define WORLDFRAME_MAX_LIGHTS 64

typedef struct WorldFrameLight {
    float x, y, z;    // map position; the falloff distance is horizontal (like classic lighting), z only places the shadow ray's end
    float radius;     // falloff radius in map units: (radius - dist) / radius, linear
    float intensity;  // 0..63 shade units at the light's centre
    float r, g, b;    // colour, 0..1 per channel (1,1,1 = the classic white light)
} WorldFrameLight;

typedef struct WorldFrameLighting {
    // View space -> map x/y: map_x = vx[0]*vx_ + vx[1]*vy_ + vx[2]*vz_ + vx[3] (likewise map_y).
    double map_x[4];
    double map_y[4];
    double map_z[4]; // map height (z up) of the pixel, for the shadow ray
    // The engine's perspective: window pixel = centre + lens * view.xy / view.z.
    double lens, centre_x, centre_y;
    // Distance fade of vertex shade (engine_render.c fade_min/max/scaler/range).
    double fade_min, fade_max, fade_scaler, fade_range;
    int32_t light_count;
    WorldFrameLight lights[WORLDFRAME_MAX_LIGHTS];
    // Per-subtile solid column height in subtiles (0 = open), row-major with stride
    // grid_w, indexed by map subtile (x, y). The per-light shadow term marches a ray
    // from each pixel to each light through this height field, so walls block light.
    int32_t grid_w, grid_h;
    const uint8_t *grid;
} WorldFrameLighting;

// A recorded frame. Ops are in draw order (back to front). Does not own any
// of the arrays it points at -- valid only for the duration of the
// SubmitWorldFrame() call.
typedef struct WorldFrame {
    const WorldFrameOp *ops;
    int64_t op_count;
    // Sprite scaling lookups (see WorldFrameSpriteOp) and colour tables:
    // lut_rows rows of 256 RGBA8 entries (r | g<<8 | b<<16 | a<<24), row 0
    // NOT included here -- backends build it from the active palette.
    const uint16_t *lookup;
    int64_t lookup_count;
    const uint32_t *lut;
    int64_t lut_rows; // rows recorded in `lut`, numbered from 1 in lut_row
    // Shadow mask bytes (see WorldFrameShadowOp).
    const unsigned char *pixels;
    int64_t pixel_count;
    // The framebuffer window the ops are relative to (the CPU rasterizer's
    // vec window -- engine window inside lbDrawSurface): coordinates are
    // offset by (view_x, view_y) and clipped to view_w x view_h.
    // view_w/view_h == 0 means "the whole target, no offset".
    int64_t view_x, view_y, view_w, view_h;
    // The colour the target is cleared to (opaque): whatever was showing in
    // the view window before the world was drawn -- the frame's clear colour,
    // visible wherever no terrain covers -- so empty regions match the CPU path.
    unsigned char clear_r, clear_g, clear_b;
    // True: terrain triangles depth-test with their per-vertex PolyPoint::Z and
    // the recorder did not clamp depth (the GPU depth buffer decides visibility).
    // False (default): every op uses its bucket depth, clamped non-increasing,
    // so output equals the CPU painter's algorithm exactly.
    int32_t true_depth;
    // Overlay frame: drawn on top of the existing world target (no clear), ops at depth 0.
    int32_t overlay;
    // Per-pixel lighting inputs, or NULL for classic (baked) lighting. Implies true_depth.
    const WorldFrameLighting *lighting;
} WorldFrame;

/******************************************************************************/
#ifdef __cplusplus
}
#endif
