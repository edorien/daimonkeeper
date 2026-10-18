// kfx_platform: renderer/SpriteAtlasCache.h -- the sprite atlases' rectangle bookkeeping (pages, growth, LRU eviction;
// docs/plans/sprite-atlas-thrash.md Phase 3). Pure CPU, so every policy is testable here; GpuSpriteRenderer only adds
// the textures and uploads.
#include <catch2/catch_test_macros.hpp>

#include "renderer/SpriteAtlasCache.h"

#include <set>
#include <tuple>

namespace {
using Cache = SpriteAtlasCache<uint64_t>;

bool no_grow(int32_t) { return false; }

// Rectangles placed in one atlas never overlap.
bool overlaps(const Cache::Entry& a, const Cache::Entry& b)
{
    return a.layer == b.layer && a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}
}

TEST_CASE("Rectangles fill pages without overlapping and stay inside them", "[kfx_platform][SpriteAtlasCache]") {
    Cache c;
    c.Reset(256, 128, 1);
    c.SetCap(1);
    std::vector<Cache::Entry> placed;
    for (uint64_t k = 1; k <= 16; ++k)
    {
        Cache::Entry* e = c.Insert(k, 60, 50, 1, no_grow);
        REQUIRE(e != nullptr);
        CHECK(e->x / 128 == (e->x + e->w - 1) / 128); // inside one page
        CHECK(e->y / 128 == (e->y + e->h - 1) / 128);
        CHECK(e->x + e->w <= 256);
        CHECK(e->y + e->h <= 256);
        placed.push_back(*e);
    }
    for (size_t i = 0; i < placed.size(); ++i)
        for (size_t j = i + 1; j < placed.size(); ++j)
            CHECK_FALSE(overlaps(placed[i], placed[j]));
    // 4 pages of 2x2 rectangles: the 17th has no room this frame (every page used by frame 1).
    CHECK(c.Insert(17, 60, 50, 1, no_grow) == nullptr);
    CHECK(c.Evictions() == 0);
}

TEST_CASE("Find marks the page used; a full atlas evicts the least recently used page only", "[kfx_platform][SpriteAtlasCache]") {
    Cache c;
    c.Reset(256, 128, 1);
    c.SetCap(1);
    // Frame 1 fills the four pages, one 128x128 rectangle each.
    for (uint64_t k = 1; k <= 4; ++k)
        REQUIRE(c.Insert(k, 128, 128, 1, no_grow) != nullptr);
    // Frame 2 uses 1, 3 and 4; key 2's page is the least recently used.
    CHECK(c.Find(1, 2) != nullptr);
    CHECK(c.Find(3, 2) != nullptr);
    CHECK(c.Find(4, 2) != nullptr);
    Cache::Entry* e = c.Insert(5, 100, 100, 2, no_grow);
    REQUIRE(e != nullptr);
    CHECK(c.Evictions() == 1);
    CHECK(c.Find(2, 2) == nullptr);   // evicted
    CHECK(c.Find(1, 2) != nullptr);   // kept
    CHECK(c.Find(3, 2) != nullptr);
    CHECK(c.Find(4, 2) != nullptr);
    CHECK(c.EntryCount() == 4);
}

TEST_CASE("Nothing used by the current frame is evicted", "[kfx_platform][SpriteAtlasCache]") {
    Cache c;
    c.Reset(256, 128, 1);
    c.SetCap(1);
    for (uint64_t k = 1; k <= 4; ++k)
        REQUIRE(c.Insert(k, 128, 128, 7, no_grow) != nullptr);
    CHECK(c.Insert(5, 10, 10, 7, no_grow) == nullptr);
    for (uint64_t k = 1; k <= 4; ++k)
        CHECK(c.Find(k, 7) != nullptr);
    // The next frame may evict.
    CHECK(c.Insert(5, 10, 10, 8, no_grow) != nullptr);
}

TEST_CASE("The atlas grows by doubling up to its cap before evicting; a refused grow caps it", "[kfx_platform][SpriteAtlasCache]") {
    Cache c;
    c.Reset(128, 64, 1);
    c.SetCap(4);
    std::vector<int32_t> asked;
    auto grow = [&](int32_t layers) { asked.push_back(layers); return true; };
    // 4 pages a layer: 16 whole-page rectangles fit 4 layers.
    for (uint64_t k = 1; k <= 16; ++k)
        REQUIRE(c.Insert(k, 64, 64, 1, grow) != nullptr);
    CHECK(c.Layers() == 4);
    CHECK(asked == std::vector<int32_t>{ 2, 4 });
    CHECK(c.Evictions() == 0);
    CHECK(c.Insert(17, 64, 64, 2, grow) != nullptr); // at the cap: evicts
    CHECK(c.Evictions() == 1);
    CHECK(c.Layers() == 4);

    Cache r;
    r.Reset(128, 64, 1);
    r.SetCap(8);
    for (uint64_t k = 1; k <= 4; ++k)
        REQUIRE(r.Insert(k, 64, 64, 1, grow) != nullptr);
    CHECK(r.Insert(5, 64, 64, 2, no_grow) != nullptr); // refused: evicts instead
    CHECK(r.Layers() == 1);
    CHECK(r.Cap() == 1);
    CHECK(r.Evictions() == 1);
}

TEST_CASE("A rectangle bigger than a page takes a whole layer; one bigger than a layer is refused", "[kfx_platform][SpriteAtlasCache]") {
    Cache c;
    c.Reset(256, 128, 1);
    c.SetCap(2);
    auto grow = [](int32_t) { return true; };
    CHECK(c.Insert(1, 257, 10, 1, grow) == nullptr);
    CHECK(c.Insert(2, 0, 10, 1, grow) == nullptr);
    REQUIRE(c.Insert(3, 10, 10, 1, grow) != nullptr);  // layer 0
    Cache::Entry* big = c.Insert(4, 200, 129, 1, grow); // no free layer: grows to 2, takes layer 1
    REQUIRE(big != nullptr);
    CHECK(big->layer == 1);
    CHECK(big->x == 0);
    CHECK(big->y == 0);
    CHECK(c.Layers() == 2);
    // Layer 0 still has free pages for small ones; none of layer 1's.
    for (uint64_t k = 10; k < 13; ++k)
    {
        Cache::Entry* e = c.Insert(k, 128, 128, 1, grow);
        REQUIRE(e != nullptr);
        CHECK(e->layer == 0);
    }
    CHECK(c.Insert(13, 10, 10, 1, grow) == nullptr); // full this frame
    // Next frame: the big one used, so a small newcomer evicts a layer-0 page, never part of layer 1.
    REQUIRE(c.Find(4, 2) != nullptr);
    Cache::Entry* e = c.Insert(14, 10, 10, 2, grow);
    REQUIRE(e != nullptr);
    CHECK(e->layer == 0);
    CHECK(c.Find(4, 2) != nullptr);
    // A frame later the big one is least recently used: another big one replaces it, as a whole layer.
    for (uint64_t k : { 3ull, 10ull, 11ull, 12ull, 14ull })
        c.Find(k, 3);
    Cache::Entry* b2 = c.Insert(15, 256, 256, 3, grow);
    REQUIRE(b2 != nullptr);
    CHECK(b2->layer == 1);
    CHECK(c.Find(4, 3) == nullptr);
    // Small ones can use the layer again once the big one is evicted (its pages all freed).
    c.Find(15, 4);
    CHECK(c.Insert(16, 300, 300, 4, grow) == nullptr);
}

TEST_CASE("A long run of new frames keeps a bounded atlas and never overlaps live rectangles", "[kfx_platform][SpriteAtlasCache]") {
    Cache c;
    c.Reset(512, 128, 1);
    c.SetCap(2);
    auto grow = [](int32_t) { return true; };
    uint64_t key = 1;
    for (uint64_t frame = 1; frame <= 200; ++frame)
    {
        std::vector<Cache::Entry> live;
        // Each frame: 20 frames seen before (some evicted since) and 10 new ones, of varied sizes.
        for (int i = 0; i < 30; ++i)
        {
            const uint64_t k = (i < 20 && key > 40) ? key - 40 + static_cast<uint64_t>(i) : key++;
            Cache::Entry* e = c.Find(k, frame);
            if (e == nullptr)
                e = c.Insert(k, 20 + static_cast<int32_t>(k % 90), 16 + static_cast<int32_t>(k % 70), frame, grow);
            REQUIRE(e != nullptr);
            live.push_back(*e);
        }
        for (size_t i = 0; i < live.size(); ++i)
            for (size_t j = i + 1; j < live.size(); ++j)
                if (live[i].x != live[j].x || live[i].y != live[j].y || live[i].layer != live[j].layer)
                    REQUIRE_FALSE(overlaps(live[i], live[j]));
    }
    CHECK(c.Layers() == 2);
    CHECK(c.Evictions() > 0);
}

TEST_CASE("Mixed small and layer-sized rectangles over many frames never overlap live ones", "[kfx_platform][SpriteAtlasCache]") {
    Cache c;
    c.Reset(512, 128, 1);
    c.SetCap(4);
    auto grow = [](int32_t) { return true; };
    uint64_t key = 1;
    for (uint64_t frame = 1; frame <= 300; ++frame)
    {
        std::vector<std::pair<uint64_t, Cache::Entry>> live;
        for (int i = 0; i < 12; ++i)
        {
            const uint64_t k = (i < 8 && key > 20) ? key - 20 + static_cast<uint64_t>(i) : key++;
            const bool big = (k % 11) == 0;
            Cache::Entry* e = c.Find(k, frame);
            if (e == nullptr)
                e = c.Insert(k, big ? 300 : 30 + static_cast<int32_t>(k % 90), big ? 200 : 20 + static_cast<int32_t>(k % 100), frame, grow);
            if (e == nullptr)
                continue; // no room this frame (every layer in use): the caller draws the original
            bool seen = false;
            for (auto& l : live)
                seen |= (l.first == k);
            if (!seen)
                live.emplace_back(k, *e);
        }
        for (size_t i = 0; i < live.size(); ++i)
            for (size_t j = i + 1; j < live.size(); ++j)
                REQUIRE_FALSE(overlaps(live[i].second, live[j].second));
        // Everything placed this frame is still where it was put.
        for (auto& l : live)
        {
            Cache::Entry* e = c.Find(l.first, frame);
            REQUIRE(e != nullptr);
            CHECK(e->x == l.second.x);
            CHECK(e->y == l.second.y);
            CHECK(e->layer == l.second.layer);
        }
    }
    CHECK(c.Layers() == 4);
}

TEST_CASE("A full atlas evicts pages unused for the stale age before it grows", "[kfx_platform][SpriteAtlasCache]") {
    Cache c;
    c.Reset(128, 64, 1);
    c.SetCap(8);
    c.SetStaleAge(10);
    int grows = 0;
    auto grow = [&](int32_t) { grows++; return true; };
    for (uint64_t k = 1; k <= 4; ++k)
        REQUIRE(c.Insert(k, 64, 64, 1, grow) != nullptr);
    // Frame 5: pages last used 4 frames ago aren't stale: grows.
    REQUIRE(c.Insert(5, 64, 64, 5, grow) != nullptr);
    CHECK(grows == 1);
    CHECK(c.Layers() == 2);
    for (uint64_t k = 6; k <= 8; ++k)
        REQUIRE(c.Insert(k, 64, 64, 5, grow) != nullptr);
    // Frame 20: keys 1-4 (frame 1) are stale: a newcomer takes one of their pages, no growth.
    for (uint64_t k = 5; k <= 8; ++k)
        c.Find(k, 20);
    REQUIRE(c.Insert(9, 64, 64, 20, grow) != nullptr);
    CHECK(grows == 1);
    CHECK(c.Layers() == 2);
    CHECK(c.Evictions() == 1);
    // A layer-sized one empties a stale layer too: at frame 40 layer 1 (keys 5-8, last used at 20) is the stalest.
    c.Find(9, 21);
    Cache::Entry* big = c.Insert(10, 100, 100, 40, grow);
    REQUIRE(big != nullptr);
    CHECK(big->layer == 1);
    CHECK(grows == 1);
    CHECK(c.Find(5, 40) == nullptr);
    CHECK(c.Find(9, 40) != nullptr);
}
