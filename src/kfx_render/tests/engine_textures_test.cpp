// kfx_render: engine_textures.c's load_texture_map_file(), which loads each
// tmapaNNN.dat/tmapbNNN.dat into its own fixed-size slot of block_mem. A
// map, campaign or mod can supply these files, so one larger than its slot
// must be cut to the slot instead of overrunning the next one.
//
// The mod test at the end loads a real mods/load_order.cfg from the temp
// runtime directory, so mods_conf is set up the same way the game does it.
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

#include "config.h"
#include "config_campaigns.h" // campaign.levels_location
#include "config_keeperfx.h"  // keeper_runtime_directory, install_info
#include "config_mods.h"
#include "engine_textures.h"

namespace fs = std::filesystem;

namespace {

constexpr LevelNumber kLevel = 1;
constexpr uint64_t kSlotA = TEXTURE_BLOCKS_STAT_COUNT_A * 32 * 32;
constexpr uint64_t kSlotB = TEXTURE_BLOCKS_STAT_COUNT_B * 32 * 32;
constexpr unsigned char kFill = 130; // load_texture_map_file()'s memset value

// Points FGrp_CmpgLvls at <temp>/levels, so map00001.tmap*.dat files written
// there are what the lookup finds, and restores the globals afterwards.
struct TmapLevelDir {
    fs::path root;
    std::string prev_runtime_dir;
    std::string prev_inst_path;
    std::string prev_levels_location;

    TmapLevelDir()
    {
        root = fs::temp_directory_path() / ("kfx_tmap_test_" + std::to_string(getpid()));
        fs::remove_all(root);
        fs::create_directories(root / "levels");
        prev_runtime_dir = keeper_runtime_directory;
        prev_inst_path = install_info.inst_path;
        prev_levels_location = campaign.levels_location;
        set(keeper_runtime_directory, sizeof(keeper_runtime_directory), root.string());
        set(install_info.inst_path, sizeof(install_info.inst_path), "");
        set(campaign.levels_location, sizeof(campaign.levels_location), "levels");
    }
    ~TmapLevelDir()
    {
        set(keeper_runtime_directory, sizeof(keeper_runtime_directory), prev_runtime_dir);
        set(install_info.inst_path, sizeof(install_info.inst_path), prev_inst_path);
        set(campaign.levels_location, sizeof(campaign.levels_location), prev_levels_location);
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    static void set(char *dst, size_t size, const std::string &value)
    {
        std::strncpy(dst, value.c_str(), size - 1);
        dst[size - 1] = '\0';
    }

    void write(char letter, int idx, size_t size, unsigned char value) const
    {
        char name[64];
        std::snprintf(name, sizeof(name), "map%05d.tmap%c%03d.dat", (int)kLevel, letter, idx);
        std::vector<unsigned char> data(size, value);
        std::ofstream out(root / "levels" / name, std::ios::binary);
        out.write(reinterpret_cast<const char *>(data.data()), (std::streamsize)data.size());
    }
};

// Adds one mod, listed in the given load_order.cfg block, with one level
// folder (by default levels/, which is FGrp_CmpgLvls here), and empties
// mods_conf again afterwards.
struct TmapModDir : TmapLevelDir {
    static constexpr const char *kModName = "tmap_test_mod";
    std::string mod_sub = "levels";

    void add_mod(const char *block, const char *sub = "levels")
    {
        mod_sub = sub;
        fs::create_directories(mod_levels());
        std::ofstream cfg(root / MODS_DIR_NAME / MODS_LOAD_ORDER_FILE_NAME);
        cfg << "[" << block << "]\n" << kModName << "\n";
        cfg.close();
        REQUIRE(load_mods_order_config_file());
        recheck_all_mod_exist();
    }
    ~TmapModDir()
    {
        std::error_code ec;
        fs::remove(root / MODS_DIR_NAME / MODS_LOAD_ORDER_FILE_NAME, ec);
        load_mods_order_config_file(); // clears mods_conf, then finds no file
    }

    fs::path mod_levels() const { return root / MODS_DIR_NAME / kModName / mod_sub; }

    void write_mod(LevelNumber lvnum, char letter, int idx, size_t size, unsigned char value) const
    {
        char name[64];
        std::snprintf(name, sizeof(name), "map%05d.tmap%c%03d.dat", (int)lvnum, letter, idx);
        std::vector<unsigned char> data(size, value);
        std::ofstream out(mod_levels() / name, std::ios::binary);
        out.write(reinterpret_cast<const char *>(data.data()), (std::streamsize)data.size());
    }
};

// Index of the first byte in [begin, end) of block_mem that isn't value, or end.
uint64_t first_mismatch(uint64_t begin, uint64_t end, unsigned char value)
{
    for (uint64_t i = begin; i < end; i++)
        if (block_mem[i] != value)
            return i;
    return end;
}

} // namespace

TEST_CASE_METHOD(TmapLevelDir, "an oversized tmapa file loads only its slot and leaves the next slot alone", "[engine_textures]")
{
    write('a', 0, kSlotA + 4096, 0x5A);

    REQUIRE(load_texture_map_file(0, kLevel, FGrp_CmpgLvls));
    CHECK(first_mismatch(0, kSlotA, 0x5A) == kSlotA);
    // No tmapb000 here, so its slot keeps the fill.
    CHECK(first_mismatch(kSlotA, kSlotA + kSlotB, kFill) == kSlotA + kSlotB);
}

// The last slot ends block_mem, so an overrun there would land in whatever
// follows it in memory; this checks the load is cut to exactly that slot (an
// AddressSanitizer build would also catch the overrun itself).
TEST_CASE_METHOD(TmapLevelDir, "an oversized tmapb file for the last variation fills exactly the last slot", "[engine_textures]")
{
    write('a', 0, kSlotA, 0x11);
    // Slot pair 0 is tmapidx's own, pairs 1..31 are variations 0..30.
    write('b', TEXTURE_VARIATIONS_COUNT - 2, kSlotB + 4096, 0x77);

    REQUIRE(load_texture_map_file(0, kLevel, FGrp_CmpgLvls));
    const uint64_t last_b = sizeof(block_mem) - kSlotB;
    CHECK(first_mismatch(last_b, sizeof(block_mem), 0x77) == sizeof(block_mem));
    // Its 'a' slot has no tmapa030 here, so keeps the fill.
    CHECK(first_mismatch(last_b - kSlotA, last_b, kFill) == last_b);
}

TEST_CASE_METHOD(TmapLevelDir, "a tmap file smaller than its slot leaves the rest of the slot filled", "[engine_textures]")
{
    write('a', 0, kSlotA / 2, 0x22);

    REQUIRE(load_texture_map_file(0, kLevel, FGrp_CmpgLvls));
    CHECK(first_mismatch(0, kSlotA / 2, 0x22) == kSlotA / 2);
    CHECK(first_mismatch(kSlotA / 2, kSlotA, kFill) == kSlotA);
}

TEST_CASE_METHOD(TmapLevelDir, "a tmapa file under 1024 bytes fails the load", "[engine_textures]")
{
    write('a', 0, 1000, 0x33);

    CHECK_FALSE(load_texture_map_file(0, kLevel, FGrp_CmpgLvls));
}

// A mod's map-specific mapNNNNN.tmapaNNN.dat is named by level number. The
// mod lookup used to get lvnum and fgroup swapped (also upstream KeeperFX),
// so it looked for map00013 (FGrp_CmpgLvls) instead. The level here must
// not be 13, or the swapped lookup would find the file anyway.
TEST_CASE_METHOD(TmapModDir, "a mod's map-specific tmapa file is found by level number", "[engine_textures]")
{
    constexpr LevelNumber kModLevel = 42;
    static_assert(kModLevel != FGrp_CmpgLvls, "the level must differ from the file group");
    const char *block = GENERATE(as<const char *>{}, MODS_AFTER_MAP_BLOCK_NAME, MODS_AFTER_CAMPAIGN_BLOCK_NAME, MODS_AFTER_BASE_BLOCK_NAME);
    CAPTURE(block);
    add_mod(block);
    write_mod(kModLevel, 'a', 0, kSlotA, 0x4D);

    REQUIRE(load_texture_map_file(0, kModLevel, FGrp_CmpgLvls));
    CHECK(first_mismatch(0, kSlotA, 0x4D) == kSlotA);
}

// The mod lookup follows the level's file group, like the non-mod one: a
// level outside the campaign (here FGrp_MpLevels, multiplayer/) finds the
// mod's file there, though the mod has no campaign levels folder.
TEST_CASE_METHOD(TmapModDir, "a mod's map-specific tmapa file is found in the level's own file group", "[engine_textures]")
{
    constexpr LevelNumber kModLevel = 42;
    add_mod(MODS_AFTER_MAP_BLOCK_NAME, "multiplayer");
    write_mod(kModLevel, 'a', 0, kSlotA, 0x6E);

    REQUIRE(load_texture_map_file(0, kModLevel, FGrp_MpLevels));
    CHECK(first_mismatch(0, kSlotA, 0x6E) == kSlotA);
}
