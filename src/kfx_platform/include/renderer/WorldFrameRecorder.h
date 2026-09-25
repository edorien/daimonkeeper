#ifndef RENDERER_WORLDFRAMERECORDER_H
#define RENDERER_WORLDFRAMERECORDER_H

#include "renderer/WorldFrame.h"

#include <vector>

// gpu-v2 Phase C.2: accumulates a WorldFrame's ops in draw order. Pure data
// (no SDL, no GPU, no global state) so it is unit-testable; the process-wide
// instance and the eligibility/clear logic around it live in
// RendererManager.cpp (RendererWorldFrame* functions).
class WorldFrameRecorder {
public:
    void Reset();

    void AddPoly(const struct PolyPoint* a, const struct PolyPoint* b, const struct PolyPoint* c, unsigned char* texture);

    // xmap/ymap: per destination column/row, the source column/row (see
    // WorldFrameSpriteOp); copied. cmap: the 256-entry RGBA colour table the
    // sprite was drawn with (nullptr = the active palette, LUT row 0);
    // copied and de-duplicated against tables already recorded this frame.
    void AddSprite(const unsigned char* rle, int32_t src_w, int32_t src_h,
                   int32_t dst_x, int32_t dst_y, int32_t dst_w, int32_t dst_h,
                   const uint16_t* xmap, const uint16_t* ymap,
                   const uint32_t* cmap, uint32_t mode, uint32_t rgba);

    // One triangle of a creature shadow (trig() in VM_SpriteTranslucent):
    // copies the used rectangle of the 256x256 mask `tex` (the engine's
    // scratch buffer, overwritten by the next shadow) at record time.
    // Vertices are PolyPoints as the CPU path receives them (X/Y pixels,
    // U/V 16.16 fixed point). Returns false if the triangle was ignored.
    bool AddShadowTri(const struct PolyPoint* a, const struct PolyPoint* b, const struct PolyPoint* c,
                      const unsigned char* tex, int64_t shade);

    // The depth stamped on ops recorded from now on (0 near .. 1 far, see
    // WorldFrameOp::depth). With monotone depth on (the default) each op's
    // depth is clamped to be <= the previous op's, which keeps the depth
    // test equivalent to plain painter's order; turning it off (tests, a
    // future true-depth producer) lets the GPU depth buffer decide.
    void SetDepth(float depth) { m_depth = depth; m_view_depth = depth; }
    // Depth for visibility and, separately, the hyperbolic view depth lighting rebuilds positions from.
    void SetDepth(float depth, float view_depth) { m_depth = depth; m_view_depth = view_depth; }
    void SetMonotoneDepth(bool on) { m_monotone = on; }

    // Per-pixel lighting inputs for this frame (copied). Cleared by Reset().
    void SetLighting(const WorldFrameLighting& lighting, const uint8_t* grid, int64_t grid_w, int64_t grid_h);

    bool HasLighting() const { return m_has_lighting; }

    int64_t OpCount() const { return static_cast<int64_t>(m_ops.size()); }

    // Fills a WorldFrame pointing into this recorder's storage; the caller
    // sets the view window and clear colour.
    WorldFrame Build() const;

private:
    void push(WorldFrameOp& op);
    bool m_has_lighting = false;
    WorldFrameLighting m_lighting = {};
    std::vector<uint8_t> m_grid;
    float m_depth = 0.5f;
    float m_view_depth = 0.5f;
    float m_last_depth = 1.0f;
    bool m_monotone = true;
    std::vector<WorldFrameOp> m_ops;
    std::vector<uint16_t> m_lookup;
    std::vector<unsigned char> m_pixels;
    std::vector<uint32_t> m_lut; // 256 entries per row; recorded row r is lut_row r+1
};

#endif // RENDERER_WORLDFRAMERECORDER_H
