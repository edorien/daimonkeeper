#include "pre_inc.h"
#include "renderer/WorldFrameRecorder.h"
#include "post_inc.h"

#include <cmath>
#include <cstring>

void WorldFrameRecorder::Reset()
{
    m_ops.clear();
    m_last_depth = 1.0f;
    m_has_lighting = false;
    m_lookup.clear();
    m_lut.clear();
    m_pixels.clear();
}

void WorldFrameRecorder::push(WorldFrameOp& op)
{
    float d = m_depth < 0.0f ? 0.0f : (m_depth > 1.0f ? 1.0f : m_depth);
    if (m_monotone && d > m_last_depth)
        d = m_last_depth;
    m_last_depth = d;
    op.depth = d;
    op.view_depth = m_view_depth < 0.0f ? 0.0f : (m_view_depth > 1.0f ? 1.0f : m_view_depth);
    m_ops.push_back(op);
}

void WorldFrameRecorder::AddPoly(const struct PolyPoint* a, const struct PolyPoint* b, const struct PolyPoint* c, unsigned char* texture,
                                 uint8_t block_shift, uint8_t parity, const TbPixel* texture_rgba, uint8_t original_at_32)
{
    WorldFrameOp op;
    op.kind = WF_OP_POLY;
    op.u.poly.v0 = *a;
    op.u.poly.v1 = *b;
    op.u.poly.v2 = *c;
    op.u.poly.texture = texture;
    op.u.poly.block_shift = block_shift;
    op.u.poly.parity = parity;
    op.u.poly.texture_rgba = texture_rgba;
    op.u.poly.original_at_32 = original_at_32;
    push(op);
}

void WorldFrameRecorder::AddSprite(const unsigned char* rle, int32_t src_w, int32_t src_h,
                                   int32_t dst_x, int32_t dst_y, int32_t dst_w, int32_t dst_h,
                                   const uint16_t* xmap, const uint16_t* ymap,
                                   const uint32_t* cmap, uint32_t mode, uint32_t rgba)
{
    if (rle == nullptr || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0)
        return;

    WorldFrameOp op;
    op.kind = WF_OP_SPRITE;
    WorldFrameSpriteOp& s = op.u.sprite;
    s.rle = rle;
    s.src_w = src_w;
    s.src_h = src_h;
    s.dst_x = dst_x;
    s.dst_y = dst_y;
    s.dst_w = dst_w;
    s.dst_h = dst_h;
    s.mode = mode;
    s.rgba = rgba;
    s.hd_texels = nullptr;
    s.hd_id = 0;
    s.hd_w = s.hd_h = s.hd_stride = 0;
    s.hd_table = 0;
    s.hd_scale = 0;
    s.index_lut_row = 0;

    s.xmap_off = static_cast<uint32_t>(m_lookup.size());
    m_lookup.insert(m_lookup.end(), xmap, xmap + dst_w);
    s.ymap_off = static_cast<uint32_t>(m_lookup.size());
    m_lookup.insert(m_lookup.end(), ymap, ymap + dst_h);

    s.lut_row = lut_row_for(cmap);
    push(op);
}

uint32_t WorldFrameRecorder::lut_row_for(const uint32_t* cmap)
{
    if (cmap == nullptr)
        return 0;
    {
        const size_t rows = m_lut.size() / 256;
        size_t row = 0;
        for (; row < rows; ++row)
            if (std::memcmp(&m_lut[row * 256], cmap, 256 * sizeof(uint32_t)) == 0)
                break;
        if (row == rows)
            m_lut.insert(m_lut.end(), cmap, cmap + 256);
        return static_cast<uint32_t>(row + 1);
    }
}

void WorldFrameRecorder::AddSpriteHd(const void* texels, const void* sub_texels, uint64_t id, int32_t hd_w, int32_t hd_h, int32_t hd_stride, int32_t hd_scale,
                                     const unsigned char* rle, int32_t src_w, int32_t src_h,
                                     int32_t dst_x, int32_t dst_y, int32_t dst_w, int32_t dst_h,
                                     const uint16_t* xmap, const uint16_t* ymap,
                                     const uint32_t* curve_row, uint32_t table, const uint32_t* index_cmap, uint32_t mode, uint32_t rgba)
{
    if (texels == nullptr || curve_row == nullptr || hd_w <= 0 || hd_h <= 0 || hd_scale < 1 || dst_w <= 0 || dst_h <= 0)
        return;
    WorldFrameOp op;
    op.kind = WF_OP_SPRITE;
    WorldFrameSpriteOp& s = op.u.sprite;
    s.rle = rle;
    s.src_w = src_w;
    s.src_h = src_h;
    s.dst_x = dst_x;
    s.dst_y = dst_y;
    s.dst_w = dst_w;
    s.dst_h = dst_h;
    s.mode = mode;
    s.rgba = rgba;
    s.hd_texels = texels;
    s.hd_sub_texels = sub_texels;
    s.hd_id = id;
    s.hd_w = hd_w;
    s.hd_h = hd_h;
    s.hd_stride = hd_stride;
    s.hd_table = table;
    s.hd_scale = static_cast<uint32_t>(hd_scale);
    s.xmap_off = static_cast<uint32_t>(m_lookup.size());
    m_lookup.insert(m_lookup.end(), xmap, xmap + dst_w);
    s.ymap_off = static_cast<uint32_t>(m_lookup.size());
    m_lookup.insert(m_lookup.end(), ymap, ymap + dst_h);
    s.lut_row = lut_row_for(curve_row);
    s.index_lut_row = lut_row_for(index_cmap);
    push(op);
}

bool WorldFrameRecorder::AddShadowTri(const struct PolyPoint* a, const struct PolyPoint* b, const struct PolyPoint* c,
                                       const unsigned char* tex, int64_t shade, int64_t scale, bool coverage)
{
    if (tex == nullptr || scale < 1 || scale > 8)
        return false;
    const int k = static_cast<int>(scale);
    const struct PolyPoint* p[3] = { a, b, c };
    // U/V are 16.16 fixed point whose integer part wraps at 256 (the
    // 256x256 mask); work in float texels.
    float u[3], v[3];
    float umin = 1e30f, umax = -1e30f, vmin = 1e30f, vmax = -1e30f;
    for (int i = 0; i < 3; ++i)
    {
        u[i] = static_cast<float>(p[i]->U & 0xFFFFFF) / 65536.0f;
        v[i] = static_cast<float>(p[i]->V & 0xFFFFFF) / 65536.0f;
        if (u[i] < umin) umin = u[i];
        if (u[i] > umax) umax = u[i];
        if (v[i] < vmin) vmin = v[i];
        if (v[i] > vmax) vmax = v[i];
    }
    const int u0 = static_cast<int>(std::floor(umin)), v0 = static_cast<int>(std::floor(vmin));
    const int u1 = static_cast<int>(std::ceil(umax)), v1 = static_cast<int>(std::ceil(vmax));
    if (u1 - u0 >= 256 || v1 - v0 >= 256 || u0 < 0 || v0 < 0 || u1 > 255 || v1 > 255)
        return false; // wraps the mask: not representable as a plain rectangle
    // The mask's rectangle, at its scale (a soft shadow's coverage is scale texels a 256x256 texel).
    const int w = (u1 - u0 + 1) * k, h = (v1 - v0 + 1) * k;

    WorldFrameOp op;
    op.kind = WF_OP_SHADOW;
    WorldFrameShadowOp& s = op.u.shadow;
    for (int i = 0; i < 3; ++i)
    {
        s.x[i] = static_cast<int32_t>(p[i]->X);
        s.y[i] = static_cast<int32_t>(p[i]->Y);
        s.u[i] = (u[i] - static_cast<float>(u0)) * static_cast<float>(k);
        s.v[i] = (v[i] - static_cast<float>(v0)) * static_cast<float>(k);
    }
    s.w = w;
    s.h = h;
    s.pix_off = static_cast<uint32_t>(m_pixels.size());
    s.shade = static_cast<uint32_t>(shade < 0 ? 0 : (shade > 63 ? 63 : shade));
    uint64_t hash = 1469598103934665603ull;
    m_pixels.resize(m_pixels.size() + static_cast<size_t>(w) * h);
    unsigned char* dst = m_pixels.data() + s.pix_off;
    for (int row = 0; row < h; ++row)
        for (int col = 0; col < w; ++col)
        {
            const unsigned char m = tex[static_cast<size_t>(v0 * k + row) * 256 * k + (u0 * k + col)];
            const unsigned char t = coverage ? m : (m ? 1 : 0);
            dst[row * w + col] = t;
            hash = (hash ^ t) * 1099511628211ull;
        }
    s.coverage = coverage ? 1u : 0u;
    s.hash = hash ^ (static_cast<uint64_t>(w) << 32) ^ static_cast<uint64_t>(h) ^ (coverage ? (1ull << 63) : 0ull);
    push(op);
    return true;
}

void WorldFrameRecorder::SetLighting(const WorldFrameLighting& lighting, const uint8_t* grid, int64_t grid_w, int64_t grid_h)
{
    m_lighting = lighting;
    if (m_lighting.light_count > WORLDFRAME_MAX_LIGHTS) m_lighting.light_count = WORLDFRAME_MAX_LIGHTS;
    if (m_lighting.light_count < 0) m_lighting.light_count = 0;
    m_grid.clear();
    if (grid != nullptr && grid_w > 0 && grid_h > 0)
        m_grid.assign(grid, grid + grid_w * grid_h);
    m_lighting.grid_w = static_cast<int32_t>(m_grid.empty() ? 0 : grid_w);
    m_lighting.grid_h = static_cast<int32_t>(m_grid.empty() ? 0 : grid_h);
    m_lighting.grid = m_grid.empty() ? nullptr : m_grid.data();
    m_has_lighting = true;
}

WorldFrame WorldFrameRecorder::Build() const
{
    WorldFrame f = {};
    f.lighting = m_has_lighting ? &m_lighting : nullptr;
    f.ops = m_ops.data();
    f.op_count = static_cast<int64_t>(m_ops.size());
    f.lookup = m_lookup.data();
    f.lookup_count = static_cast<int64_t>(m_lookup.size());
    f.lut = m_lut.data();
    f.lut_rows = static_cast<int64_t>(m_lut.size() / 256);
    f.pixels = m_pixels.data();
    f.pixel_count = static_cast<int64_t>(m_pixels.size());
    return f;
}
