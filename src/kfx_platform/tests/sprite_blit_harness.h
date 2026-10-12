// kfx_platform tests: the scaled-sprite blit harness shared by sprite_blit_golden_test.cpp (the blitters' golden
// hashes) and sprite_blit_hd_test.cpp (HD sprites in parity mode draw what the blitters draw): a screen bigger than
// the graphics window, hand-made RLE sprites, the flips and the scales. Test-only; each including file gets its own
// copy (anonymous namespace).
#pragma once

#include "bflib_video.h"
#include "bflib_vidraw.h"
#include "renderer/RendererManager.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

const int kScreenW = 160, kScreenH = 120;
const int kWinX = 8, kWinY = 6, kWinW = 144, kWinH = 108;

struct Fnv {
    uint64_t v = 1469598103934665603ULL;
    void add(const void *p, size_t n) {
        const unsigned char *b = static_cast<const unsigned char *>(p);
        for (size_t i = 0; i < n; i++) { v ^= b[i]; v *= 1099511628211ULL; }
    }
};

/** RLE as the blitters read it: per row, n>0 then n pixel bytes, n<0 skips -n, 0 ends the row. */
std::vector<unsigned char> encode(const std::vector<std::vector<int>> &rows) {
    std::vector<unsigned char> out;
    for (const auto &row : rows) {
        size_t i = 0;
        while (i < row.size()) {
            size_t j = i;
            if (row[i] == 0) {
                while (j < row.size() && row[j] == 0 && j - i < 127) j++;
                if (j < row.size()) out.push_back((unsigned char)(signed char)-(int)(j - i));
            } else {
                while (j < row.size() && row[j] != 0 && j - i < 127) j++;
                out.push_back((unsigned char)(j - i));
                for (size_t k = i; k < j; k++) out.push_back((unsigned char)row[k]);
            }
            i = j;
        }
        out.push_back(0);
    }
    return out;
}

struct TestSprite {
    std::string name;
    int w, h;
    std::vector<unsigned char> rle;
};

std::vector<TestSprite> make_sprites() {
    std::vector<TestSprite> s;
    auto add = [&](const std::string &name, const std::vector<std::vector<int>> &rows) {
        s.push_back({name, (int)rows[0].size(), (int)rows.size(), encode(rows)});
    };
    {   // mixed runs and gaps, 16x12
        std::vector<std::vector<int>> r(12, std::vector<int>(16, 0));
        for (int y = 0; y < 12; y++)
            for (int x = 0; x < 16; x++)
                r[y][x] = ((x + y) % 5 == 0) ? 0 : 1 + ((x * 7 + y * 13) % 254);
        add("mixed", r);
    }
    {   // fully solid 30x20
        std::vector<std::vector<int>> r(20, std::vector<int>(30, 0));
        for (int y = 0; y < 20; y++)
            for (int x = 0; x < 30; x++)
                r[y][x] = 1 + ((x * 3 + y * 5) % 250);
        add("solid", r);
    }
    {   // tall thin 3x40 with a gap column
        std::vector<std::vector<int>> r(40, std::vector<int>(3, 0));
        for (int y = 0; y < 40; y++) { r[y][0] = 10 + y; r[y][2] = 200 - y; }
        add("tall", r);
    }
    {   // single pixel
        add("dot", {{77}});
    }
    {   // leading/trailing transparency and an empty row, 9x6
        add("gaps", {{0, 0, 3, 4, 5, 0, 0, 0, 9},
                     {0, 0, 0, 0, 0, 0, 0, 0, 0},
                     {1, 0, 1, 0, 1, 0, 1, 0, 1},
                     {0, 250, 251, 252, 253, 254, 255, 0, 0},
                     {0, 0, 0, 0, 7, 0, 0, 0, 0},
                     {2, 2, 2, 2, 2, 2, 2, 2, 2}});
    }
    return s;
}

struct Harness {
    std::vector<TbPixel> screen = std::vector<TbPixel>((size_t)kScreenW * kScreenH);
    unsigned char palette[768];
    TbPixel remap[256];
    TbPixel *saved_wscreen, *saved_window;
    int64_t saved[6];
    unsigned char *saved_palette;
    Harness() {
        for (int i = 0; i < 768; i++) palette[i] = (unsigned char)((i * 37 + 11) % 64);
        for (int i = 0; i < 256; i++) remap[i] = TbPixel_RGBA((uint8_t)(255 - i), (uint8_t)(i * 3), (uint8_t)(i ^ 0x5a), (uint8_t)(i | 0x80));
        saved_wscreen = lbDisplay.WScreen;
        saved_window = lbDisplay.GraphicsWindowPtr;
        saved[0] = lbDisplay.GraphicsScreenWidth; saved[1] = lbDisplay.GraphicsScreenHeight;
        saved[2] = lbDisplay.GraphicsWindowX; saved[3] = lbDisplay.GraphicsWindowY;
        saved[4] = lbDisplay.GraphicsWindowWidth; saved[5] = lbDisplay.GraphicsWindowHeight;
        saved_palette = lbDisplay.Palette;
        lbDisplay.WScreen = screen.data();
        lbDisplay.GraphicsWindowPtr = screen.data() + kWinY * kScreenW + kWinX;
        lbDisplay.GraphicsScreenWidth = kScreenW; lbDisplay.GraphicsScreenHeight = kScreenH;
        lbDisplay.GraphicsWindowX = kWinX; lbDisplay.GraphicsWindowY = kWinY;
        lbDisplay.GraphicsWindowWidth = kWinW; lbDisplay.GraphicsWindowHeight = kWinH;
        lbDisplay.Palette = palette;
    }
    ~Harness() {
        lbDisplay.WScreen = saved_wscreen;
        lbDisplay.GraphicsWindowPtr = saved_window;
        lbDisplay.GraphicsScreenWidth = saved[0]; lbDisplay.GraphicsScreenHeight = saved[1];
        lbDisplay.GraphicsWindowX = saved[2]; lbDisplay.GraphicsWindowY = saved[3];
        lbDisplay.GraphicsWindowWidth = saved[4]; lbDisplay.GraphicsWindowHeight = saved[5];
        lbDisplay.Palette = saved_palette;
        RendererSetDrawFlags(0);
    }
    void fill() {
        for (size_t i = 0; i < screen.size(); i++)
            screen[i] = TbPixel_RGBA((uint8_t)(i * 7), (uint8_t)(i * 13 + 5), (uint8_t)(i * 3 + 100),
                (uint8_t)(i % 3 == 0 ? 255 : 200 + i % 50)); // opaque and translucent: the blends differ
    }
};

const struct { const char *name; int64_t flags; } kFlips[] = {
    {"lr", 0}, {"rl", Lb_SPRITE_FLIP_HORIZ}, {"vflip", Lb_SPRITE_FLIP_VERTIC}, {"rl-vflip", Lb_SPRITE_FLIP_HORIZ | Lb_SPRITE_FLIP_VERTIC},
};
// Destination size as a fraction of the source: {num_w, den_w, num_h, den_h}.
const struct { const char *name; int nw, dw, nh, dh; } kScales[] = {
    {"x1", 1, 1, 1, 1}, {"down-half", 1, 2, 1, 2}, {"down-2of3", 2, 3, 2, 3}, {"up-x2", 2, 1, 2, 1},
    {"up-x3", 3, 1, 3, 1}, {"up-1.5", 3, 2, 3, 2}, {"mixed-wide", 2, 1, 1, 2}, {"mixed-tall", 1, 2, 5, 2},
};

} // namespace
