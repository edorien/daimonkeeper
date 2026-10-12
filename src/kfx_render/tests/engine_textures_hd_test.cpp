// kfx_render: engine_textures_hd.c's pack loader (docs/newFeatures/texture-resolution/02-design.md §3,
// 03-verification-and-phases.md §C): HD versions of the loaded 32x32 blocks, found by their content key in a
// content folder beside the tmap file (textures-hd/) or the user pack (fxdata/asset_packs/<ASSET_PACK>/textures/),
// in FXGraphics' layout -- pack.toml, filelists, PNG sheets, keys/*.idx.
//
// Each test writes tmap files and packs into a temp runtime directory, loads them through the real
// load_texture_map_file(), and reads back where each slot's HD came from (texture_hd_slot_source()) and its
// colour (each HD block here is one solid colour, so the colour names the entry that was chosen).
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <unistd.h>
#include <vector>

#include <spng.h>

#include "config.h"
#include "config_campaigns.h" // campaign.levels_location
#include "config_keeperfx.h"  // keeper_runtime_directory, install_info, keeperfx_ui_config
#include "engine_textures.h"
#include "engine_textures_hd.h"
#include "kfx_config_state.h"
#include "renderer/RendererManager.h"

namespace fs = std::filesystem;

namespace {

constexpr LevelNumber kLevel = 1;
constexpr size_t kSlotA = TEXTURE_BLOCKS_STAT_COUNT_A * 32 * 32;
constexpr size_t kSlotB = TEXTURE_BLOCKS_STAT_COUNT_B * 32 * 32;

using Block = std::array<unsigned char, 32 * 32>;

/** A non-uniform block, different for each seed. */
Block pattern(int seed)
{
    Block b;
    for (int i = 0; i < 32 * 32; i++)
        b[i] = (unsigned char)(1 + ((i * 7 + seed * 13 + (i >> 5) * seed) % 250));
    return b;
}

Block uniform(unsigned char v)
{
    Block b;
    b.fill(v);
    return b;
}

/** The game's content key of a block, as texpack.py keys writes it. */
uint64_t key_of(const Block &b)
{
    std::vector<unsigned char> atlas(32 * 256, 0);
    for (int y = 0; y < 32; y++)
        std::memcpy(&atlas[y * 256], &b[y * 32], 32);
    return texture_hd_block_key(atlas.data());
}

/** A tmap file: 544 blocks in a 256-wide atlas, block i at column i % 8, row i / 8; unset blocks filler 130. */
struct Tmap {
    std::vector<unsigned char> data = std::vector<unsigned char>(kSlotA, 130);
    void set(int index, const Block &b)
    {
        const int x0 = (index % 8) * 32, y0 = (index / 8) * 32;
        for (int y = 0; y < 32; y++)
            std::memcpy(&data[(size_t)(y0 + y) * 256 + x0], &b[y * 32], 32);
    }
    void write(const fs::path &path) const
    {
        fs::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char *>(data.data()), (std::streamsize)data.size());
    }
};

struct Rgba { uint8_t r, g, b, a; };

/** A PNG sheet: w x h, every pixel one colour unless painted. */
struct Sheet {
    int w, h;
    std::vector<uint8_t> px;
    Sheet(int w_, int h_) : w(w_), h(h_), px((size_t)w_ * h_ * 4, 0) {}
    void fill(int x0, int y0, int side, Rgba c)
    {
        for (int y = y0; y < y0 + side; y++)
            for (int x = x0; x < x0 + side; x++)
            {
                uint8_t *p = &px[((size_t)y * w + x) * 4];
                p[0] = c.r; p[1] = c.g; p[2] = c.b; p[3] = c.a;
            }
    }
    void write(const fs::path &path, size_t truncate_to = 0) const
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
        out.write(static_cast<const char *>(png), (std::streamsize)((truncate_to > 0) ? truncate_to : len));
        free(png);
        spng_ctx_free(ctx);
    }
};

/** A pack folder: pack.toml, filelists with their keys, sheets. */
struct Pack {
    fs::path dir;
    int size;
    struct Line { std::string sheet; int x, y, w, h; uint64_t key; };
    std::map<std::string, std::vector<Line>> filelists;

    Pack(fs::path d, int s) : dir(std::move(d)), size(s) {}

    /** Block `index` of `stem` is a solid `colour` square at (x, y) of `sheet`, made for the block `upgrades`. */
    void add(const std::string &stem, int index, const std::string &sheet, int x, int y, uint64_t key)
    {
        auto &lines = filelists[stem];
        if ((int)lines.size() <= index)
            lines.resize((size_t)index + 1, Line{"pack000/none.png", 0, 0, size, size, 0});
        lines[(size_t)index] = Line{sheet, x, y, size, size, key};
    }
    bool original_at_32 = true;
    void write(bool keys = true, const char *separator = "\t", bool bom = true) const
    {
        fs::create_directories(dir);
        std::ofstream(dir / "pack.toml") << "name = \"test\"\nsize = " << size << "\n"
                                         << "use_original_at_32 = " << (original_at_32 ? "true" : "false") << "\n";
        for (const auto &[stem, lines] : filelists)
        {
            std::ofstream out(dir / ("filelist_" + stem + ".txt"), std::ios::binary);
            if (bom)
                out << "\xEF\xBB\xBF";
            out << "textures_pack_000" << separator << 8 << separator << 68 << separator << size << separator << size << "\r\n";
            for (const auto &l : lines)
                out << l.sheet << separator << l.x << separator << l.y << separator << l.w << separator << l.h << "\r\n";
            if (!keys)
                continue;
            fs::create_directories(dir / "keys");
            std::ofstream idx(dir / "keys" / (stem + ".idx"), std::ios::binary);
            for (const auto &l : lines)
                for (int k = 0; k < 8; k++)
                    idx.put((char)((l.key >> (8 * k)) & 0xFF));
        }
    }
};

/** A temp runtime directory: data/ (base tmap files, palette.dat), levels/ (the campaign's), fxdata/asset_packs/. */
struct HdDir {
    fs::path root;
    std::string prev_runtime_dir, prev_inst_path, prev_levels_location, prev_pack;
    std::vector<int64_t> prev_animation;

    HdDir()
    {
        root = fs::temp_directory_path() / ("kfx_texhd_test_" + std::to_string(getpid()));
        fs::remove_all(root);
        fs::create_directories(root / "data");
        fs::create_directories(root / "levels");
        prev_runtime_dir = keeper_runtime_directory;
        prev_inst_path = install_info.inst_path;
        prev_levels_location = campaign.levels_location;
        prev_pack = keeperfx_ui_config.asset_pack;
        prev_animation.assign(std::begin(kfx_config_state.texture_animation), std::end(kfx_config_state.texture_animation));
        set(keeper_runtime_directory, sizeof(keeper_runtime_directory), root.string());
        set(install_info.inst_path, sizeof(install_info.inst_path), "");
        set(campaign.levels_location, sizeof(campaign.levels_location), "levels");
        set(keeperfx_ui_config.asset_pack, sizeof(keeperfx_ui_config.asset_pack), "NONE");
        // A grey ramp: every entry a different colour, index 0 black.
        std::vector<unsigned char> pal(768);
        for (int i = 0; i < 256; i++)
            pal[3 * i] = pal[3 * i + 1] = pal[3 * i + 2] = (unsigned char)(i / 4);
        std::ofstream(root / "data" / "palette.dat", std::ios::binary).write(reinterpret_cast<const char *>(pal.data()), 768);
        setup_texture_block_mem();
        texture_hd_set_memory_cap(0);
        RendererSetTextureMipmaps(0);
    }
    ~HdDir()
    {
        RendererSetTextureMipmaps(0);
        texture_hd_clear();
        texture_hd_set_memory_cap(0);
        set(keeper_runtime_directory, sizeof(keeper_runtime_directory), prev_runtime_dir);
        set(install_info.inst_path, sizeof(install_info.inst_path), prev_inst_path);
        set(campaign.levels_location, sizeof(campaign.levels_location), prev_levels_location);
        set(keeperfx_ui_config.asset_pack, sizeof(keeperfx_ui_config.asset_pack), prev_pack);
        std::copy(prev_animation.begin(), prev_animation.end(), std::begin(kfx_config_state.texture_animation));
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    static void set(char *dst, size_t size, const std::string &value)
    {
        std::strncpy(dst, value.c_str(), size - 1);
        dst[size - 1] = '\0';
    }
    void use_pack(const char *name) { set(keeperfx_ui_config.asset_pack, sizeof(keeperfx_ui_config.asset_pack), name); }
    fs::path user_pack(const char *name) const { return root / "fxdata" / "asset_packs" / name / "textures"; }
    void load() { REQUIRE(load_texture_map_file(0, kLevel, FGrp_CmpgLvls)); }
};

/** The slot of (variation, a-file block index). Variation 0 is the level's own pack (tmapidx 0 here); variation
 *  v >= 1 is tmapa{v-1}. */
int64_t slot(int64_t variation, int64_t index) { return variation * TEXTURE_BLOCKS_COUNT + index; }

Rgba hd_colour(int64_t s)
{
    const TbPixel *p = block_hd[s].page;
    REQUIRE(p != nullptr);
    return Rgba{p[0].r, p[0].g, p[0].b, p[0].a};
}

bool same(Rgba a, Rgba b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; }

constexpr Rgba kRed{200, 0, 0, 255}, kGreen{0, 200, 0, 255}, kBlue{0, 0, 200, 255}, kYellow{200, 200, 0, 255};

} // namespace

TEST_CASE_METHOD(HdDir, "a block gets HD only when its own content has a key", "[engine_textures_hd]")
{
    Tmap base;
    base.set(0, pattern(1));
    base.set(1, pattern(2));
    base.set(2, uniform(77));
    base.write(root / "data" / "tmapa000.dat");
    Block changed = pattern(2);
    changed[500] ^= 1;
    Pack pack(user_pack("hd"), 64);
    pack.add("tmapa000", 0, "pack000/a.png", 0, 0, key_of(pattern(1)));
    pack.add("tmapa000", 1, "pack000/a.png", 64, 0, key_of(changed)); // made for a block one texel different
    pack.add("tmapa000", 2, "pack000/a.png", 128, 0, key_of(uniform(77)));
    pack.write();
    Sheet sheet(192, 64);
    sheet.fill(0, 0, 64, kRed);
    sheet.fill(64, 0, 64, kGreen);
    sheet.fill(128, 0, 64, kBlue);
    sheet.write(pack.dir / "pack000" / "a.png");
    use_pack("hd");

    load();
    // Variation 0 (tmapidx 0) and variation 1 (tmapa000) are both this file.
    for (int64_t v : {0, 1})
    {
        CAPTURE(v);
        CHECK(texture_hd_slot_source(slot(v, 0)) == TexHdSrc_UserPack);
        CHECK(same(hd_colour(slot(v, 0)), kRed));
        CHECK(block_hd[slot(v, 0)].shift == 1);
        CHECK(texture_hd_slot_source(slot(v, 1)) == TexHdSrc_None); // one texel changed: not the art it was made for
        CHECK(texture_hd_slot_source(slot(v, 2)) == TexHdSrc_None); // uniform blocks never get HD
    }
    // De-duplicated: both slots share one decoded block.
    CHECK(block_hd[slot(0, 0)].page == block_hd[slot(1, 0)].page);
    CHECK(texture_hd_stats()->blocks == 1);
    CHECK(texture_hd_stats()->slots_with_hd == 2);
    CHECK_FALSE(texture_hd_parity()); // packs draw in precision mode
}

TEST_CASE_METHOD(HdDir, "identical HD art shares one block, whichever entries and keys it is for", "[engine_textures_hd]")
{
    Tmap a0;
    a0.set(0, pattern(1));
    a0.set(1, pattern(2));
    a0.write(root / "data" / "tmapa000.dat");
    Pack pack(user_pack("hd"), 64);
    pack.add("tmapa000", 0, "pack000/a.png", 0, 0, key_of(pattern(1)));
    pack.add("tmapa000", 1, "pack000/b.png", 0, 0, key_of(pattern(2))); // another sheet, the same pixels
    pack.write();
    Sheet sheet(64, 64);
    sheet.fill(0, 0, 64, kRed);
    sheet.write(pack.dir / "pack000" / "a.png");
    sheet.write(pack.dir / "pack000" / "b.png");
    use_pack("hd");

    load();
    CHECK(texture_hd_slot_source(slot(1, 0)) == TexHdSrc_UserPack);
    CHECK(texture_hd_slot_source(slot(1, 1)) == TexHdSrc_UserPack);
    CHECK(block_hd[slot(1, 0)].page == block_hd[slot(1, 1)].page);
    CHECK(texture_hd_stats()->blocks == 1);
}

TEST_CASE_METHOD(HdDir, "a key with several HD versions: the slot's own (stem, index), else the first in pack order", "[engine_textures_hd]")
{
    const Block c = pattern(9);
    Tmap a0, a1;
    a0.set(5, c);
    a1.set(7, c);
    a1.set(9, c);
    a0.write(root / "data" / "tmapa000.dat");
    a1.write(root / "data" / "tmapa001.dat");
    Pack pack(user_pack("hd"), 64);
    pack.add("tmapa000", 5, "pack000/a.png", 0, 0, key_of(c));
    pack.add("tmapa001", 7, "pack001/a.png", 0, 0, key_of(c));
    pack.write();
    Sheet s0(64, 64), s1(64, 64);
    s0.fill(0, 0, 64, kRed);
    s1.fill(0, 0, 64, kGreen);
    s0.write(pack.dir / "pack000" / "a.png");
    s1.write(pack.dir / "pack001" / "a.png");
    use_pack("hd");

    load();
    CHECK(same(hd_colour(slot(1, 5)), kRed));   // tmapa000 block 5: its own
    CHECK(same(hd_colour(slot(2, 7)), kGreen)); // tmapa001 block 7: its own
    CHECK(same(hd_colour(slot(2, 9)), kRed));   // tmapa001 block 9: none of its own, the first (tmapa000 before tmapa001)
    CHECK(texture_hd_stats()->blocks == 2);
}

TEST_CASE_METHOD(HdDir, "a content folder beside the tmap file beats the user pack, for that file's blocks only", "[engine_textures_hd]")
{
    const Block c = pattern(3);
    Tmap base, level;
    base.set(0, c);
    level.set(0, c);
    level.set(1, pattern(4)); // the level's own art: only its content folder has HD for it
    base.write(root / "data" / "tmapa000.dat");
    // The level's own tmapa001 (variation 2); variations 0 and 1 are the base tmapa000.
    level.write(root / "levels" / "map00001.tmapa001.dat");
    Pack user(user_pack("hd"), 64);
    user.add("tmapa000", 0, "pack000/a.png", 0, 0, key_of(c));
    user.write();
    Sheet us(64, 64);
    us.fill(0, 0, 64, kRed);
    us.write(user.dir / "pack000" / "a.png");
    Pack content(root / "levels" / "textures-hd", 128);
    content.add("tmapa001", 0, "pack001/a.png", 0, 0, key_of(c));
    content.add("tmapa001", 1, "pack001/a.png", 128, 0, key_of(pattern(4)));
    content.write();
    Sheet cs(256, 128);
    cs.fill(0, 0, 128, kBlue);
    cs.fill(128, 0, 128, kYellow);
    cs.write(content.dir / "pack001" / "a.png");
    use_pack("hd");

    load();
    CHECK(texture_hd_slot_source(slot(2, 0)) == TexHdSrc_ContentFolder);
    CHECK(same(hd_colour(slot(2, 0)), kBlue));
    CHECK(block_hd[slot(2, 0)].shift == 2);
    CHECK(same(hd_colour(slot(2, 1)), kYellow));
    // The same content in the base file (data/, no content folder there): the user pack's.
    CHECK(texture_hd_slot_source(slot(1, 0)) == TexHdSrc_UserPack);
    CHECK(same(hd_colour(slot(1, 0)), kRed));
    CHECK(block_hd[slot(1, 0)].shift == 1);

    // Without a user pack, the content folder still applies.
    use_pack("NONE");
    load();
    CHECK(texture_hd_slot_source(slot(2, 0)) == TexHdSrc_ContentFolder);
    CHECK(texture_hd_slot_source(slot(1, 0)) == TexHdSrc_None);
}

TEST_CASE_METHOD(HdDir, "a campaign file that reuses base blocks gets HD on exactly those", "[engine_textures_hd]")
{
    Tmap base, campaign_file;
    Pack pack(user_pack("hd"), 64);
    Sheet sheet(8 * 64, 64);
    for (int i = 0; i < 8; i++)
    {
        base.set(i, pattern(20 + i));
        pack.add("tmapa000", i, "pack000/a.png", 64 * i, 0, key_of(pattern(20 + i)));
        sheet.fill(64 * i, 0, 64, Rgba{(uint8_t)(20 * i), 50, 50, 255});
        // The campaign's file: the base art in its even blocks, its own in the odd ones.
        campaign_file.set(i, (i % 2 == 0) ? pattern(20 + i) : pattern(40 + i));
    }
    base.write(root / "data" / "tmapa000.dat");
    campaign_file.write(root / "levels" / "map00001.tmapa001.dat"); // variation 2
    pack.write();
    sheet.write(pack.dir / "pack000" / "a.png");
    use_pack("hd");

    load();
    for (int i = 0; i < 8; i++)
    {
        CAPTURE(i);
        CHECK(texture_hd_slot_source(slot(2, i)) == ((i % 2 == 0) ? TexHdSrc_UserPack : TexHdSrc_None));
        CHECK(texture_hd_slot_source(slot(1, i)) == TexHdSrc_UserPack);
    }
    // A reused block shares the base slot's decoded block.
    CHECK(block_hd[slot(2, 0)].page == block_hd[slot(1, 0)].page);
}

TEST_CASE_METHOD(HdDir, "pack layout: separators, missing keys, missing sheets, wrong sizes, a truncated PNG", "[engine_textures_hd]")
{
    Tmap a0, a1, a2;
    for (int i = 0; i < 6; i++)
    {
        a0.set(i, pattern(60 + i));
        a1.set(i, pattern(70 + i));
        a2.set(i, pattern(80 + i));
    }
    a0.write(root / "data" / "tmapa000.dat");
    a1.write(root / "data" / "tmapa001.dat");
    a2.write(root / "data" / "tmapa002.dat");
    Pack pack(user_pack("hd"), 64);
    pack.add("tmapa000", 0, "pack000/a.png", 0, 0, key_of(pattern(60)));
    pack.add("tmapa000", 1, "pack000/missing.png", 0, 0, key_of(pattern(61)));   // no such sheet: skipped
    pack.add("tmapa000", 2, "pack000/a.png", 64, 0, key_of(pattern(62)));
    pack.filelists["tmapa000"][2].w = 32;                                           // wrong crop size: skipped
    pack.add("tmapa000", 3, "pack000/broken.png", 0, 0, key_of(pattern(63)));    // truncated PNG: skipped
    pack.add("tmapa000", 4, "pack000/a.png", 128, 0, key_of(pattern(64)));       // off the sheet: skipped
    pack.write(true, " ", false); // spaces, no BOM
    Pack no_keys(user_pack("hd"), 64);
    no_keys.add("tmapa001", 0, "pack001/a.png", 0, 0, key_of(pattern(70)));
    no_keys.write(false);         // a filelist without keys/: ignored
    Sheet sheet(128, 64);
    sheet.fill(0, 0, 64, kRed);
    sheet.write(pack.dir / "pack000" / "a.png");
    sheet.write(pack.dir / "pack000" / "broken.png", 60);
    Sheet s1(64, 64);
    s1.fill(0, 0, 64, kGreen);
    s1.write(pack.dir / "pack001" / "a.png");
    use_pack("hd");

    load();
    CHECK(texture_hd_slot_source(slot(1, 0)) == TexHdSrc_UserPack);
    for (int i = 1; i <= 4; i++)
    {
        CAPTURE(i);
        CHECK(texture_hd_slot_source(slot(1, i)) == TexHdSrc_None);
    }
    CHECK(texture_hd_slot_source(slot(2, 0)) == TexHdSrc_None); // tmapa001: no keys
    CHECK(texture_hd_stats()->blocks == 1);
}

TEST_CASE_METHOD(HdDir, "PNG alpha below 128 is texture index 0's black", "[engine_textures_hd]")
{
    Tmap a0;
    a0.set(0, pattern(5));
    a0.write(root / "data" / "tmapa000.dat");
    Pack pack(user_pack("hd"), 64);
    pack.add("tmapa000", 0, "pack000/a.png", 0, 0, key_of(pattern(5)));
    pack.write();
    Sheet sheet(64, 64);
    sheet.fill(0, 0, 64, Rgba{90, 100, 110, 127});
    sheet.fill(0, 0, 32, Rgba{90, 100, 110, 128});
    sheet.write(pack.dir / "pack000" / "a.png");
    use_pack("hd");

    load();
    const TbPixel *p = block_hd[slot(1, 0)].page;
    REQUIRE(p != nullptr);
    CHECK((p[0].r == 90 && p[0].g == 100 && p[0].b == 110 && p[0].a == 255));      // alpha 128: opaque, its colour
    const TbPixel &transparent = p[40 * 256 + 40];
    CHECK((transparent.r == 0 && transparent.g == 0 && transparent.b == 0));       // alpha 127: black
}

TEST_CASE_METHOD(HdDir, "animated ids show the HD version of the frame they show", "[engine_textures_hd]")
{
    Tmap a0;
    a0.set(0, pattern(1));
    a0.set(1, pattern(2));
    a0.write(root / "data" / "tmapa000.dat");
    Pack pack(user_pack("hd"), 64);
    pack.add("tmapa000", 0, "pack000/a.png", 0, 0, key_of(pattern(1)));
    pack.add("tmapa000", 1, "pack000/a.png", 64, 0, key_of(pattern(2)));
    pack.write();
    Sheet sheet(128, 64);
    sheet.fill(0, 0, 64, kRed);
    sheet.fill(64, 0, 64, kGreen);
    sheet.write(pack.dir / "pack000" / "a.png");
    use_pack("hd");
    // Every animated id alternates between blocks 0 and 1.
    for (int i = 0; i < TEXTURE_BLOCKS_ANIM_COUNT; i++)
        for (int k = 0; k < TEXTURE_BLOCKS_ANIM_FRAMES; k++)
            kfx_config_state.texture_animation[TEXTURE_BLOCKS_ANIM_FRAMES * i + k] = k % 2;

    load();
    for (int turn = 0; turn < 3; turn++)
    {
        CAPTURE(turn);
        update_animating_texture_maps();
        for (int64_t v : {0, 1, 5})
        {
            const int64_t anim = slot(v, TEXTURE_BLOCKS_STAT_COUNT_A + 3);
            const int64_t shown = (block_ptrs[anim] == block_ptrs[slot(v, 0)]) ? slot(v, 0) : slot(v, 1);
            REQUIRE(block_ptrs[anim] == block_ptrs[shown]);
            CHECK(block_hd[anim].page == block_hd[shown].page);
        }
    }
}

TEST_CASE_METHOD(HdDir, "over the memory cap, the variations in use keep HD at the first frame", "[engine_textures_hd]")
{
    Tmap a0, a1;
    for (int i = 0; i < 4; i++)
    {
        a0.set(i, pattern(100 + i));
        a1.set(i, pattern(200 + i));
    }
    a0.write(root / "data" / "tmapa000.dat");
    a1.write(root / "data" / "tmapa001.dat");
    Pack pack(user_pack("hd"), 64);
    Sheet sheet(8 * 64, 64);
    for (int i = 0; i < 4; i++)
    {
        pack.add("tmapa000", i, "pack000/a.png", 64 * i, 0, key_of(pattern(100 + i)));
        pack.add("tmapa001", i, "pack000/a.png", 64 * (4 + i), 0, key_of(pattern(200 + i)));
    }
    for (int i = 0; i < 8; i++)
        sheet.fill(64 * i, 0, 64, Rgba{(uint8_t)(30 * i), 99, 0, 255}); // 8 different blocks
    pack.write();
    sheet.write(pack.dir / "pack000" / "a.png");
    use_pack("hd");

    SECTION("under the cap: every slot, at load")
    {
        load();
        CHECK(texture_hd_stats()->blocks == 8);
        CHECK(texture_hd_slot_source(slot(2, 0)) == TexHdSrc_UserPack);
    }
    SECTION("over it: nothing until the first frame, then the level's variations (here only the default, 0)")
    {
        texture_hd_set_memory_cap((uint64_t)64 * 64 * 4 * 6); // room for 6 of the 8 blocks
        load();
        CHECK(texture_hd_stats()->blocks == 0);
        texture_hd_frame_begin();
        CHECK(texture_hd_stats()->blocks == 4);              // tmapa000's 4 (variations 0 and 1 share them)
        CHECK(texture_hd_slot_source(slot(0, 0)) == TexHdSrc_UserPack);
        CHECK(texture_hd_slot_source(slot(1, 0)) == TexHdSrc_None); // variation 1 isn't in use
        CHECK(texture_hd_slot_source(slot(2, 0)) == TexHdSrc_None); // nor is 2
        CHECK(texture_hd_stats()->slots_capped > 0);
    }
}

TEST_CASE_METHOD(HdDir, "changing ASSET_PACK reloads at the next frame", "[engine_textures_hd]")
{
    Tmap a0;
    a0.set(0, pattern(1));
    a0.write(root / "data" / "tmapa000.dat");
    Pack pack(user_pack("hd"), 64);
    pack.add("tmapa000", 0, "pack000/a.png", 0, 0, key_of(pattern(1)));
    pack.write();
    Sheet sheet(64, 64);
    sheet.fill(0, 0, 64, kRed);
    sheet.write(pack.dir / "pack000" / "a.png");

    load();
    CHECK(texture_hd_slot_source(slot(1, 0)) == TexHdSrc_None);
    use_pack("hd");
    texture_hd_frame_begin();
    CHECK(texture_hd_slot_source(slot(1, 0)) == TexHdSrc_UserPack);
    use_pack("NONE");
    texture_hd_frame_begin();
    CHECK(texture_hd_slot_source(slot(1, 0)) == TexHdSrc_None);
}

TEST_CASE("filelists parse with their BOM, tabs or spaces, CRLF", "[engine_textures_hd]")
{
    const std::string text = "\xEF\xBB\xBFtextures_pack_005\t8\t68\t128\t128\r\n"
                             "pack005/a.png\t0\t0\t128\t128\r\n"
                             "\r\n"
                             "pack005/b.png 128 256 128 128\n";
    std::vector<TextureHdFilelistLine> lines(4);
    int64_t header_size = 0;
    REQUIRE(texture_hd_parse_filelist(text.data(), text.size(), lines.data(), 4, &header_size) == 2);
    CHECK(header_size == 128);
    CHECK(std::string(lines[0].sheet) == "pack005/a.png");
    CHECK(std::string(lines[1].sheet) == "pack005/b.png");
    CHECK(lines[1].x == 128);
    CHECK(lines[1].y == 256);
    CHECK(lines[1].w == 128);
}

TEST_CASE("the level of detail is the one whose texel is nearest a pixel", "[engine_textures_hd]")
{
    // Without TEXTURE_MIPMAPS loaded, nothing below 32.
    texture_hd_clear();
    CHECK(texture_hd_lod_for_texel(0.0) == 0);
    CHECK(texture_hd_lod_for_texel(0.1) == 0);
    CHECK(texture_hd_lod_for_texel(1.0) == 0);
    CHECK(texture_hd_lod_for_texel(1.41) == 0);  // 32-res texel 1.41 px: nearer 1 than 64's 0.71
    CHECK(texture_hd_lod_for_texel(1.42) == 1);
    CHECK(texture_hd_lod_for_texel(2.82) == 1);
    CHECK(texture_hd_lod_for_texel(2.84) == 2);
    CHECK(texture_hd_lod_for_texel(5.7) == 3);
    CHECK(texture_hd_lod_for_texel(100.0) == 3);
}

TEST_CASE_METHOD(HdDir, "levels: HD above 32, the original at 32, mips below with TEXTURE_MIPMAPS", "[engine_textures_hd]")
{
    Tmap a0;
    a0.set(0, pattern(1));
    a0.set(1, uniform(77));
    a0.write(root / "data" / "tmapa000.dat");
    Pack pack(user_pack("hd"), 128);
    pack.add("tmapa000", 0, "pack000/a.png", 0, 0, key_of(pattern(1)));
    pack.write();
    Sheet sheet(128, 128);
    // 2 x 2 squares: red, green / blue, transparent (black): the 64 level averages them over 2 x 2 texels only
    // inside each square, so it keeps the four colours.
    sheet.fill(0, 0, 64, kRed);
    sheet.fill(64, 0, 64, kGreen);
    sheet.fill(0, 64, 64, kBlue);
    sheet.fill(64, 64, 64, Rgba{0, 0, 0, 0});
    sheet.write(pack.dir / "pack000" / "a.png");
    use_pack("hd");
    RendererSetTextureMipmaps(1);

    load();
    texture_hd_frame_begin();
    REQUIRE(texture_hd_low_levels());
    const int64_t s0 = slot(1, 0);
    TextureHdDraw d;
    texture_hd_select(s0, 3, &d);
    CHECK(d.shift == 2);
    CHECK(d.rgba == block_hd[s0].page);
    texture_hd_select(s0, 1, &d);
    REQUIRE(d.rgba != nullptr);
    CHECK(d.shift == 1);
    CHECK((d.rgba[0].r == 200 && d.rgba[0].a == 255));                  // red quarter
    CHECK((d.rgba[40 * 256 + 40].a == 0 && d.rgba[40 * 256 + 40].r == 0)); // the transparent quarter stays black, alpha 0
    // At 32 the original indexed block (use_original_at_32, the default).
    texture_hd_select(s0, 0, &d);
    CHECK(d.rgba == nullptr);
    CHECK(d.indexed == block_ptrs[s0]);
    CHECK(d.shift == 0);
    // Below 32: the original's indexed mips. A uniform block's are its index.
    texture_hd_select(s0, -1, &d);
    CHECK(d.rgba == nullptr);
    CHECK(d.shift == -1);
    REQUIRE(d.indexed != nullptr);
    CHECK(d.indexed != block_ptrs[s0]);
    texture_hd_select(slot(1, 1), -5, &d);
    REQUIRE(d.indexed != nullptr);
    CHECK(d.shift == -5);
    CHECK(d.indexed[0] == 77);
    // Clamped: nothing below 1x1.
    texture_hd_select(slot(1, 1), -9, &d);
    CHECK(d.shift == -5);

    // The index mips follow block_ptrs: an animated id's are its frame's.
    for (int i = 0; i < TEXTURE_BLOCKS_ANIM_COUNT; i++)
        for (int k = 0; k < TEXTURE_BLOCKS_ANIM_FRAMES; k++)
            kfx_config_state.texture_animation[TEXTURE_BLOCKS_ANIM_FRAMES * i + k] = 1;
    update_animating_texture_maps();
    TextureHdDraw anim, frame;
    texture_hd_select(slot(1, TEXTURE_BLOCKS_STAT_COUNT_A + 2), -2, &anim);
    texture_hd_select(slot(1, 1), -2, &frame);
    CHECK(anim.indexed == frame.indexed);
}

TEST_CASE_METHOD(HdDir, "a pack that replaces the original at 32 has RGBA levels down to 32 and below", "[engine_textures_hd]")
{
    Tmap a0;
    a0.set(0, pattern(1));
    a0.write(root / "data" / "tmapa000.dat");
    Pack pack(user_pack("hd"), 64);
    pack.original_at_32 = false;
    pack.add("tmapa000", 0, "pack000/a.png", 0, 0, key_of(pattern(1)));
    pack.write();
    Sheet sheet(64, 64);
    sheet.fill(0, 0, 64, kYellow);
    sheet.write(pack.dir / "pack000" / "a.png");
    use_pack("hd");

    SECTION("TEXTURE_MIPMAPS off: down to 32")
    {
        load();
        texture_hd_frame_begin();
        TextureHdDraw d;
        texture_hd_select(slot(1, 0), 0, &d);
        REQUIRE(d.rgba != nullptr);
        CHECK(d.shift == 0);
        CHECK((d.rgba[0].r == 200 && d.rgba[0].g == 200 && d.rgba[0].b == 0));
        texture_hd_select(slot(1, 0), -3, &d); // clamped to 32
        CHECK(d.shift == 0);
        CHECK(d.rgba != nullptr);
    }
    SECTION("on: RGBA below 32 too")
    {
        RendererSetTextureMipmaps(1);
        load();
        texture_hd_frame_begin();
        TextureHdDraw d;
        texture_hd_select(slot(1, 0), -5, &d);
        REQUIRE(d.rgba != nullptr);
        CHECK(d.shift == -5);
        CHECK((d.rgba[0].r == 200 && d.rgba[0].g == 200));
    }
}

TEST_CASE_METHOD(HdDir, "turning TEXTURE_MIPMAPS on reloads at the next frame", "[engine_textures_hd]")
{
    Tmap a0;
    a0.set(0, pattern(1));
    a0.write(root / "data" / "tmapa000.dat");
    load();
    texture_hd_frame_begin();
    CHECK_FALSE(texture_hd_low_levels());
    RendererSetTextureMipmaps(1);
    texture_hd_frame_begin();
    CHECK(texture_hd_low_levels());
    TextureHdDraw d;
    texture_hd_select(slot(1, 0), -1, &d);
    CHECK(d.shift == -1);
}

TEST_CASE_METHOD(HdDir, "UV precision: the finest HD texel's while a pack draws in precision mode, none in parity mode", "[engine_textures_hd]")
{
    Tmap a0;
    a0.set(0, pattern(1));
    a0.write(root / "data" / "tmapa000.dat");
    load();
    texture_hd_frame_begin();
    CHECK(texture_hd_uv_shift() == 0); // no HD
    Pack pack(user_pack("hd"), 128);
    pack.add("tmapa000", 0, "pack000/a.png", 0, 0, key_of(pattern(1)));
    pack.write();
    Sheet sheet(128, 128);
    sheet.fill(0, 0, 128, kRed);
    sheet.write(pack.dir / "pack000" / "a.png");
    use_pack("hd");
    texture_hd_frame_begin();
    CHECK(texture_hd_uv_shift() == 2);
    setenv("KFX_TEXTURE_HD_PARITY", "1", 1);
    load();
    texture_hd_frame_begin();
    CHECK(texture_hd_uv_shift() == 0);
    unsetenv("KFX_TEXTURE_HD_PARITY");
}

TEST_CASE_METHOD(HdDir, "ASSET_PACK_FILL: blocks no pack covers get generated art, and a picture's blocks run on into each other", "[engine_textures_hd]")
{
    const int64_t prev_fill = keeperfx_ui_config.asset_pack_fill;
    // Blocks 0 and 1 side by side: one ramp, dark to light, across both (a picture over two blocks); block 2 a pack's;
    // block 3 one colour (never HD).
    Block left, right;
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++)
        {
            left[y * 32 + x] = (unsigned char)(4 + 2 * x);
            right[y * 32 + x] = (unsigned char)(4 + 2 * (32 + x));
        }
    Tmap base;
    base.set(0, left);
    base.set(1, right);
    base.set(2, pattern(3));
    base.set(3, uniform(77));
    base.write(root / "data" / "tmapa000.dat");
    Pack pack(user_pack("hd"), 64);
    pack.add("tmapa000", 2, "pack000/a.png", 0, 0, key_of(pattern(3)));
    pack.write();
    Sheet sheet(64, 64);
    sheet.fill(0, 0, 64, kRed);
    sheet.write(pack.dir / "pack000" / "a.png");
    use_pack("hd");
    keeperfx_ui_config.asset_pack_fill = 1;
    load();
    CHECK(texture_hd_slot_source(slot(0, 0)) == TexHdSrc_Generated);
    CHECK(texture_hd_slot_source(slot(0, 1)) == TexHdSrc_Generated);
    CHECK(texture_hd_slot_source(slot(0, 2)) == TexHdSrc_UserPack); // the pack's art wins
    CHECK(texture_hd_slot_source(slot(0, 3)) == TexHdSrc_None);
    CHECK(block_hd[slot(0, 0)].shift == 2);
    // The ramp runs on across the blocks' edge: block 0's last HD column next to block 1's first steps no more than
    // neighbouring columns inside a block do (filtered against its own opposite edge instead, the dark start of the
    // ramp, it would jump by about 60).
    const TbPixel *a = block_hd[slot(0, 0)].page, *b = block_hd[slot(0, 1)].page;
    int across = 0, inside = 0;
    for (int y = 0; y < 128; y++)
    {
        across = std::max(across, std::abs((int)a[y * 256 + 127].r - (int)b[y * 256].r));
        for (int x = 8; x < 120; x++)
            inside = std::max(inside, std::abs((int)a[y * 256 + x + 1].r - (int)a[y * 256 + x].r));
    }
    CAPTURE(across, inside);
    CHECK(across <= inside + 2);
    // Its colours are the ramp's (grey), sharpened, not tinted.
    CHECK(a[64 * 256 + 64].r == a[64 * 256 + 64].g);
    // Off: only the pack's.
    keeperfx_ui_config.asset_pack_fill = 0;
    load();
    CHECK(texture_hd_slot_source(slot(0, 0)) == TexHdSrc_None);
    CHECK(texture_hd_slot_source(slot(0, 2)) == TexHdSrc_UserPack);
    keeperfx_ui_config.asset_pack_fill = prev_fill;
}
