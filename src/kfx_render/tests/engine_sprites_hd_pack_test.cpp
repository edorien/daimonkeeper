// kfx_render: engine_sprites_hd_pack.c, HD sprite packs (docs/refactor/sprites/, Phase 5): a pack folder in
// FXGraphics' sprites-* layout with keys -- pack.toml, animlist.txt, filelists of canvas PNGs, keys/animlist.idx --
// indexed by content key, its art decoded on worker threads (the original's crop cut out at the pack's scale, and its
// levels), packs earlier in priority keeping a key, the least recently drawn freed over a cap.
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <unistd.h>

#include <spng.h>

#include "engine_sprites_hd_pack.h"

namespace fs = std::filesystem;

namespace {

/** An RGBA canvas PNG: w x h, transparent unless painted. */
struct Canvas {
    int w, h;
    std::vector<uint8_t> px;
    Canvas(int w_, int h_) : w(w_), h(h_), px((size_t)w_ * h_ * 4, 0) {}
    void fill(int x0, int y0, int fw, int fh, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
    {
        for (int y = y0; y < y0 + fh; y++)
            for (int x = x0; x < x0 + fw; x++)
            {
                uint8_t *p = &px[((size_t)y * w + x) * 4];
                p[0] = r; p[1] = g; p[2] = b; p[3] = a;
            }
    }
    void write(const fs::path &path) const
    {
        fs::create_directories(path.parent_path());
        spng_ctx *ctx = spng_ctx_new(SPNG_CTX_ENCODER);
        spng_set_option(ctx, SPNG_ENCODE_TO_BUFFER, 1);
        struct spng_ihdr ihdr = {};
        ihdr.width = (uint32_t)w;
        ihdr.height = (uint32_t)h;
        ihdr.bit_depth = 8;
        ihdr.color_type = SPNG_COLOR_TYPE_TRUECOLOR_ALPHA;
        spng_set_ihdr(ctx, &ihdr);
        REQUIRE(spng_encode_image(ctx, px.data(), px.size(), SPNG_FMT_PNG, SPNG_ENCODE_FINALIZE) == 0);
        size_t len = 0;
        int err = 0;
        void *png = spng_get_png_buffer(ctx, &len, &err);
        REQUIRE(png != nullptr);
        std::ofstream out(path, std::ios::binary);
        out.write(static_cast<const char *>(png), (std::streamsize)len);
        free(png);
        spng_ctx_free(ctx);
    }
};

struct Frame { std::string png; uint64_t key; int x, y, w, h; };

/** A pack: one filelist ("anim/filelist_rot1.txt") of `frames`, keys written unless told otherwise. */
void write_pack(const fs::path &dir, int scale, const std::vector<Frame> &frames, bool keys = true, int key_records = -1,
    bool original_at_1x = true)
{
    fs::create_directories(dir / "anim");
    std::ofstream(dir / "pack.toml") << "name = \"test pack\"\nscale = " << scale << "\nuse_original_at_1x = "
                                     << (original_at_1x ? "true" : "false") << "\n";
    std::ofstream(dir / "animlist.txt", std::ios::binary) << "\xEF\xBB\xBF" << "anim/filelist_rot1.txt\r\n";
    std::ofstream fl(dir / "anim" / "filelist_rot1.txt", std::ios::binary);
    fl << "animation_878 2 " << frames.size() << " -10 -20\r\n";
    for (const Frame &f : frames)
        fl << f.png << "\r\n";
    if (!keys)
        return;
    fs::create_directories(dir / "keys");
    std::ofstream idx(dir / "keys" / "animlist.idx", std::ios::binary);
    const int n = (key_records < 0) ? (int)frames.size() : key_records;
    for (int i = 0; i < n; i++)
    {
        const Frame f = (i < (int)frames.size()) ? frames[(size_t)i] : Frame{"", 0, 0, 0, 0, 0};
        for (int k = 0; k < 8; k++)
            idx.put((char)((f.key >> (8 * k)) & 0xFF));
        const uint16_t v[4] = { (uint16_t)f.x, (uint16_t)f.y, (uint16_t)f.w, (uint16_t)f.h };
        for (uint16_t u : v)
        {
            idx.put((char)(u & 0xFF));
            idx.put((char)(u >> 8));
        }
    }
}

struct TempDir {
    fs::path root;
    TempDir()
    {
        root = fs::temp_directory_path() / ("kfx_sprhd_test_" + std::to_string(getpid()));
        fs::remove_all(root);
        fs::create_directories(root);
        sprite_pack_close_all();
    }
    ~TempDir()
    {
        sprite_pack_close_all();
        fs::remove_all(root);
    }
};

/** A 10x8 canvas at x4 (40x32), the original's crop at (2,1) 5x4 (8..28, 4..20): red, with one blue texel. */
void write_frame(const fs::path &path, int scale = 4)
{
    Canvas c(10 * scale, 8 * scale);
    c.fill(2 * scale, 1 * scale, 5 * scale, 4 * scale, 200, 10, 10);
    c.fill(3 * scale, 2 * scale, scale, scale, 10, 10, 200);
    c.write(path);
}

} // namespace

TEST_CASE("sprite_pack_open indexes a pack's keyed frame lines", "[kfx_render][sprite_hd_pack]")
{
    TempDir t;
    const fs::path pack = t.root / "p";
    write_frame(pack / "anim" / "a.png");
    write_frame(pack / "anim" / "b.png");
    write_pack(pack, 4, { {"a.png", 0x1111, 2, 1, 5, 4}, {"none.png", 0, 0, 0, 0, 0}, {"b.png", 0x2222, 2, 1, 5, 4} });
    CHECK(sprite_pack_open(pack.string().c_str(), false) == 2);
    CHECK(sprite_pack_entry_count() == 2);
    REQUIRE(sprite_pack_source_count() == 1);
    CHECK(sprite_pack_source(0)->scale == 4);
    CHECK(sprite_pack_source(0)->frames == 3);
    CHECK(std::string(sprite_pack_source(0)->name) == "test pack");
    const int32_t a = sprite_pack_find(0x1111);
    REQUIRE(a >= 0);
    CHECK(sprite_pack_entry(a)->crop_x == 2);
    CHECK(sprite_pack_entry(a)->h == 4);
    CHECK(sprite_pack_find(0x2222) >= 0);
    CHECK(sprite_pack_find(0x3333) == -1);
    CHECK(sprite_pack_find(0) == -1);
}

TEST_CASE("A pack's art is decoded: the crop at the pack's scale, and its levels", "[kfx_render][sprite_hd_pack]")
{
    TempDir t;
    const fs::path pack = t.root / "p";
    write_frame(pack / "anim" / "a.png");
    write_pack(pack, 4, { {"a.png", 0x1111, 2, 1, 5, 4} }, true, -1, false);
    REQUIRE(sprite_pack_open(pack.string().c_str(), false) == 1);
    sprite_pack_request_all();
    CHECK(sprite_pack_wait_idle() == 1);
    struct SpritePackEntry *e = sprite_pack_entry(sprite_pack_find(0x1111));
    REQUIRE(e->state == SpE_Built);
    CHECK(e->uid != 0);
    // x4, x2 and (use_original_at_1x = false) x1.
    REQUIRE(e->lv.levels == 3);
    CHECK(e->lv.level_scale[0] == 4);
    CHECK(e->lv.level_scale[1] == 2);
    CHECK(e->lv.level_scale[2] == 1);
    const TbPixel *top = e->lv.level[0];
    CHECK(top[0].r == 200);                 // the crop's first texel: red
    CHECK(top[0].a == 255);
    CHECK(top[(4 + 1) * 20 + 4 + 1].b == 200); // the blue texel, crop column 1 row 1 at x4
    CHECK(e->lv.level[2][1 * 5 + 1].b == 200);  // and at x1
    CHECK(e->lv.bytes == (uint64_t)(20 * 16 + 10 * 8 + 5 * 4) * 4);
    CHECK(sprite_pack_bytes() == e->lv.bytes);
}

TEST_CASE("A pack with a bad scale, no keys or stale keys is refused, and adds nothing", "[kfx_render][sprite_hd_pack]")
{
    TempDir t;
    write_frame(t.root / "s" / "anim" / "a.png");
    write_pack(t.root / "s", 5, { {"a.png", 0x1111, 2, 1, 5, 4} });
    CHECK(sprite_pack_open((t.root / "s").string().c_str(), false) == -1);
    write_frame(t.root / "k" / "anim" / "a.png");
    write_pack(t.root / "k", 4, { {"a.png", 0x1111, 2, 1, 5, 4} }, false);
    CHECK(sprite_pack_open((t.root / "k").string().c_str(), false) == -1);
    write_frame(t.root / "n" / "anim" / "a.png");
    write_pack(t.root / "n", 4, { {"a.png", 0x1111, 2, 1, 5, 4}, {"a.png", 0x2222, 2, 1, 5, 4} }, true, 1);
    CHECK(sprite_pack_open((t.root / "n").string().c_str(), false) == -1);
    CHECK(sprite_pack_open((t.root / "none").string().c_str(), true) == -1);
    CHECK(sprite_pack_entry_count() == 0);
    CHECK(sprite_pack_source_count() == 0);
    CHECK(sprite_pack_find(0x1111) == -1);
}

TEST_CASE("A key two packs have keeps the first pack's art", "[kfx_render][sprite_hd_pack]")
{
    TempDir t;
    write_frame(t.root / "first" / "anim" / "a.png");
    write_pack(t.root / "first", 4, { {"a.png", 0x1111, 2, 1, 5, 4} });
    Canvas other(20, 16);
    other.fill(4, 2, 10, 8, 10, 200, 10);
    other.write(t.root / "second" / "anim" / "a.png");
    write_pack(t.root / "second", 2, { {"a.png", 0x1111, 2, 1, 5, 4}, {"a.png", 0x2222, 2, 1, 5, 4} });
    CHECK(sprite_pack_open((t.root / "first").string().c_str(), true) == 1);
    CHECK(sprite_pack_open((t.root / "second").string().c_str(), false) == 1); // only 0x2222 is new
    CHECK(sprite_pack_entry(sprite_pack_find(0x1111))->source == 0);
    CHECK(sprite_pack_entry(sprite_pack_find(0x2222))->source == 1);
    sprite_pack_request_all();
    sprite_pack_wait_idle();
    CHECK(sprite_pack_entry(sprite_pack_find(0x1111))->lv.level[0][0].r == 200); // the first pack's red
    CHECK(sprite_pack_entry(sprite_pack_find(0x2222))->lv.level[0][0].g == 200); // the second's green, at x2
    CHECK(sprite_pack_entry(sprite_pack_find(0x2222))->lv.level_scale[0] == 2);
}

TEST_CASE("Art smaller than the original's crop at the pack's scale fails, and the original stays", "[kfx_render][sprite_hd_pack]")
{
    TempDir t;
    Canvas small(16, 8); // the crop needs (2+5)*4 = 28 wide
    small.fill(0, 0, 16, 8, 200, 0, 0);
    small.write(t.root / "p" / "anim" / "a.png");
    write_pack(t.root / "p", 4, { {"a.png", 0x1111, 2, 1, 5, 4}, {"missing.png", 0x2222, 2, 1, 5, 4} });
    REQUIRE(sprite_pack_open((t.root / "p").string().c_str(), false) == 2);
    sprite_pack_request_all();
    CHECK(sprite_pack_wait_idle() == 0);
    CHECK(sprite_pack_entry(sprite_pack_find(0x1111))->state == SpE_Failed);
    CHECK(sprite_pack_entry(sprite_pack_find(0x2222))->state == SpE_Failed);
    CHECK(sprite_pack_failed() == 2);
    CHECK(sprite_pack_bytes() == 0);
}

TEST_CASE("Over the cap the least recently drawn frames are freed, and can be decoded again", "[kfx_render][sprite_hd_pack]")
{
    TempDir t;
    std::vector<Frame> frames;
    for (int i = 0; i < 4; i++)
    {
        write_frame(t.root / "p" / "anim" / ("f" + std::to_string(i) + ".png"));
        frames.push_back({"f" + std::to_string(i) + ".png", 0x1000u + (uint64_t)i, 2, 1, 5, 4});
    }
    write_pack(t.root / "p", 4, frames);
    REQUIRE(sprite_pack_open((t.root / "p").string().c_str(), false) == 4);
    sprite_pack_request_all();
    REQUIRE(sprite_pack_wait_idle() == 4);
    const uint64_t one = sprite_pack_entry(sprite_pack_find(0x1000))->lv.bytes;
    for (int i = 0; i < 4; i++)
        sprite_pack_entry(sprite_pack_find(0x1000u + (uint64_t)i))->last_used = (uint64_t)(10 - i); // 0x1003 oldest
    CHECK(sprite_pack_evict(2 * one) == 2);
    CHECK(sprite_pack_bytes() == 2 * one);
    CHECK(sprite_pack_entry(sprite_pack_find(0x1003))->state == SpE_Idle);
    CHECK(sprite_pack_entry(sprite_pack_find(0x1002))->state == SpE_Idle);
    CHECK(sprite_pack_entry(sprite_pack_find(0x1000))->state == SpE_Built);
    CHECK(sprite_pack_evict(2 * one) == 0);
    // Drawn again: decoded again, with a new id (the GPU's copy of the old texels is not reused).
    const uint64_t old_uid = sprite_pack_entry(sprite_pack_find(0x1003))->uid;
    sprite_pack_request(sprite_pack_find(0x1003), true);
    CHECK(sprite_pack_wait_idle() == 1);
    CHECK(sprite_pack_entry(sprite_pack_find(0x1003))->state == SpE_Built);
    CHECK(sprite_pack_entry(sprite_pack_find(0x1003))->uid != old_uid);
}

TEST_CASE("An urgent request is decoded before the background load", "[kfx_render][sprite_hd_pack]")
{
    TempDir t;
    std::vector<Frame> frames;
    for (int i = 0; i < 40; i++)
    {
        write_frame(t.root / "p" / "anim" / ("f" + std::to_string(i) + ".png"));
        frames.push_back({"f" + std::to_string(i) + ".png", 0x2000u + (uint64_t)i, 2, 1, 5, 4});
    }
    write_pack(t.root / "p", 4, frames);
    REQUIRE(sprite_pack_open((t.root / "p").string().c_str(), false) == 40);
    // One worker, so decodes finish in the order they are taken (uids follow that order).
    sprite_pack_set_worker_limit(1);
    // The last in pack order asked for first (a frame being drawn), then the whole pack in the background.
    sprite_pack_request(sprite_pack_find(0x2027), true);
    sprite_pack_request_all();
    // And one more urgent request while the background load runs: it goes ahead of what is still queued.
    sprite_pack_request(sprite_pack_find(0x2026), true);
    sprite_pack_wait_idle();
    sprite_pack_set_worker_limit(0);
    uint64_t newer = 0, after_second = 0;
    const uint64_t urgent_uid = sprite_pack_entry(sprite_pack_find(0x2027))->uid;
    const uint64_t second_uid = sprite_pack_entry(sprite_pack_find(0x2026))->uid;
    for (int i = 0; i < 40; i++)
    {
        const uint64_t uid = sprite_pack_entry(sprite_pack_find(0x2000u + (uint64_t)i))->uid;
        newer += (uid > urgent_uid);
        after_second += (uid > second_uid);
    }
    // The first urgent one goes before every background one (the second urgent one, a stack's top, may go before it).
    CHECK(newer >= 38);
    // The second urgent request waits only for the decode under way when it came (a few, under a slow scheduler).
    CHECK(after_second >= 30);
}

TEST_CASE("Reopening with a content folder: its art wins its keys, and art decoded from unchanged packs is kept",
    "[kfx_render][sprite_hd_pack]")
{
    TempDir t;
    write_frame(t.root / "user" / "anim" / "a.png");
    write_frame(t.root / "user" / "anim" / "b.png");
    write_pack(t.root / "user", 4, { {"a.png", 0x1111, 2, 1, 5, 4}, {"b.png", 0x2222, 2, 1, 5, 4} });
    Canvas green(20, 16);
    green.fill(4, 2, 10, 8, 10, 200, 10);
    green.write(t.root / "level" / "sprites-hd" / "anim" / "b.png");
    write_pack(t.root / "level" / "sprites-hd", 2, { {"b.png", 0x2222, 2, 1, 5, 4} });
    const std::string user = (t.root / "user").string(), content = (t.root / "level" / "sprites-hd").string(),
        missing = (t.root / "elsewhere" / "sprites-hd").string();

    const char *only_user[] = { user.c_str() };
    const TbBool user_flags[] = { false };
    REQUIRE(sprite_pack_reopen(only_user, user_flags, 1) == 2);
    sprite_pack_request_all();
    CHECK(sprite_pack_wait_idle() == 2);
    const uint64_t uid_a = sprite_pack_entry(sprite_pack_find(0x1111))->uid;
    const uint64_t uid_b = sprite_pack_entry(sprite_pack_find(0x2222))->uid;
    const uint64_t user_bytes = sprite_pack_bytes();

    // A level with a content folder (and one that has no pack: nothing, no warning): it comes first.
    const char *level[] = { missing.c_str(), content.c_str(), user.c_str() };
    const TbBool level_flags[] = { true, true, false };
    CHECK(sprite_pack_reopen(level, level_flags, 3) == 2);
    CHECK(sprite_pack_source_count() == 2);
    const SpritePackEntry *a = sprite_pack_entry(sprite_pack_find(0x1111));
    CHECK(a->state == SpE_Built); // the same art: not decoded again
    CHECK(a->uid == uid_a);
    CHECK(a->lv.level[0][0].r == 200);
    const SpritePackEntry *b = sprite_pack_entry(sprite_pack_find(0x2222));
    CHECK(sprite_pack_source(b->source)->content);
    CHECK(b->state == SpE_Idle);
    CHECK(sprite_pack_bytes() < user_bytes);
    sprite_pack_request_all();
    CHECK(sprite_pack_wait_idle() == 1);
    b = sprite_pack_entry(sprite_pack_find(0x2222));
    CHECK(b->lv.level[0][0].g == 200); // the content folder's green
    CHECK(b->lv.level_scale[0] == 2);
    CHECK(b->uid != uid_b);
    const uint64_t uid_b_content = b->uid;

    // Back to a level without it: the user pack's art again, decoded again (a new uid: the GPU's copy is stale).
    REQUIRE(sprite_pack_reopen(only_user, user_flags, 1) == 2);
    CHECK(sprite_pack_entry(sprite_pack_find(0x1111))->uid == uid_a);
    CHECK(sprite_pack_entry(sprite_pack_find(0x2222))->state == SpE_Idle);
    sprite_pack_request_all();
    CHECK(sprite_pack_wait_idle() == 1);
    b = sprite_pack_entry(sprite_pack_find(0x2222));
    CHECK(b->lv.level[0][0].r == 200);
    CHECK(b->uid != uid_b);
    CHECK(b->uid != uid_b_content);
    CHECK(sprite_pack_bytes() == user_bytes);

    // Nothing: everything freed.
    CHECK(sprite_pack_reopen(nullptr, nullptr, 0) == 0);
    CHECK(sprite_pack_entry_count() == 0);
    CHECK(sprite_pack_bytes() == 0);
}

TEST_CASE("Reopening while the background load runs keeps what finished and loses nothing else", "[kfx_render][sprite_hd_pack]")
{
    TempDir t;
    std::vector<Frame> frames;
    for (int i = 0; i < 200; i++)
    {
        const std::string png = "f" + std::to_string(i) + ".png";
        write_frame(t.root / "user" / "anim" / png);
        frames.push_back({png, 0x1000u + (uint64_t)i, 2, 1, 5, 4});
    }
    write_pack(t.root / "user", 4, frames);
    const std::string user = (t.root / "user").string();
    const char *dirs[] = { user.c_str() };
    const TbBool flags[] = { false };
    REQUIRE(sprite_pack_reopen(dirs, flags, 1) == 200);
    sprite_pack_request_all();
    // Mid-load: whatever is built or decoding now carries over, the rest is Idle and queued again.
    REQUIRE(sprite_pack_reopen(dirs, flags, 1) == 200);
    int64_t built = 0, idle = 0;
    for (int32_t e = 0; e < 200; e++)
    {
        const int32_t st = sprite_pack_entry(e)->state;
        built += (st == SpE_Built);
        idle += (st == SpE_Idle);
    }
    CHECK(built + idle == 200);
    sprite_pack_request_all();
    CHECK(sprite_pack_wait_idle() == idle);
    for (int32_t e = 0; e < 200; e++)
        CHECK(sprite_pack_entry(e)->state == SpE_Built);
    CHECK(sprite_pack_failed() == 0);
}

TEST_CASE("Art past the original's crop widens the frame by whole original texels, up to the canvas and the limit",
    "[kfx_render][sprite_hd_pack]")
{
    TempDir t;
    // The crop at (2,1) 5x4 on a 10x8 canvas at x4; art reaching 1 pixel left of it (one original texel), 5 pixels
    // below (two), and to the canvas's right edge (three).
    Canvas c(40, 32);
    c.fill(8, 4, 20, 16, 200, 10, 10);
    c.fill(7, 10, 1, 1, 10, 200, 10);
    c.fill(12, 20, 2, 5, 10, 200, 10);
    c.fill(28, 6, 12, 1, 10, 200, 10);
    c.write(t.root / "p" / "anim" / "spill.png");
    write_frame(t.root / "p" / "anim" / "inside.png");
    write_pack(t.root / "p", 4, { {"spill.png", 0x1111, 2, 1, 5, 4}, {"inside.png", 0x2222, 2, 1, 5, 4} });
    REQUIRE(sprite_pack_open((t.root / "p").string().c_str(), false) == 2);
    sprite_pack_request_all();
    CHECK(sprite_pack_wait_idle() == 2);
    const SpritePackEntry *e = sprite_pack_entry(sprite_pack_find(0x1111));
    REQUIRE(e->state == SpE_Built);
    CHECK(e->lv.spill[0] == 1);
    CHECK(e->lv.spill[1] == 0);
    CHECK(e->lv.spill[2] == 3);
    CHECK(e->lv.spill[3] == 2);
    const int64_t stride = (5 + 1 + 3) * 4;
    // The crop's first texel is red, one original texel in; the art left of it is there, at (3, 6) of the widened texels.
    CHECK(e->lv.level[0][0 * stride + 1 * 4].r == 200);
    CHECK(e->lv.level[0][6 * stride + 3].g == 200);
    CHECK(e->lv.level[0][6 * stride + 2].a == 0);
    // The x2 level is the widened rectangle too.
    CHECK(e->lv.level_scale[1] == 2);
    CHECK(e->lv.bytes == (uint64_t)((9 * 4) * (6 * 4) + (9 * 2) * (6 * 2)) * sizeof(TbPixel));
    // Art inside the crop: nothing past it.
    const SpritePackEntry *in = sprite_pack_entry(sprite_pack_find(0x2222));
    CHECK((in->lv.spill[0] | in->lv.spill[1] | in->lv.spill[2] | in->lv.spill[3]) == 0);
}
