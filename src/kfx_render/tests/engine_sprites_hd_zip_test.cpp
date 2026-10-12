// kfx_render: mods' zip sprites and HD (docs/refactor/sprites/, Phase 6), on the game's own zips: with
// KFX_SPRITES_COMPAT_DIR=<an install> (core_files), a temp runtime directory links its fxdata zips and
// data/png_conv_pal.dat, and init_custom_sprites() loads them as the game does.
//
// - Every custom slot records its zip, PNG and the key of its quantised frame; the same zips loaded in another order
//   (two mods swapped in mods/load_order.cfg) put the frames in other slots with the same keys.
// - SPRITE_TRUE_COLOUR: a slot's true colours are its PNG's, opaque exactly where its quantised frame draws
//   (compress_raw()'s alpha 0x40), and switching the option off and on again reads the same colours back from the
//   zips.
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <unistd.h>
#include <vector>

#include "config.h"
#include "config_campaigns.h"
#include "config_keeperfx.h"
#include "config_mods.h"
#include "custom_sprites.h"
#include "engine_render.h"
#include "engine_sprites_hd.h"
#include "keeper_sprites.h"
#include "mod_config_types.h"

namespace fs = std::filesystem;

namespace {

void set(char *dst, size_t size, const std::string &value)
{
    std::strncpy(dst, value.c_str(), size - 1);
    dst[size - 1] = '\0';
}

/** A runtime directory whose fxdata/ links the install's required zips; mods/ holds `mods` (name -> zips). */
struct ZipRuntime {
    fs::path root, install;
    std::string prev_runtime_dir, prev_inst_path, prev_levels, prev_configs;
    int64_t prev_true_colour;

    explicit ZipRuntime(const char *install_dir, const std::vector<std::pair<std::string, std::vector<std::string>>> &mods = {})
        : install(install_dir)
    {
        root = fs::temp_directory_path() / ("kfx_sprhd_zip_" + std::to_string(getpid()));
        fs::remove_all(root);
        for (const char *d : {"fxdata", "data", "levels", "campgns/test_cfgs", MODS_DIR_NAME})
            fs::create_directories(root / d);
        for (size_t i = 0; i < REQUIRED_SPRITE_ZIP_COUNT; i++)
            fs::create_symlink(install / "fxdata" / required_sprite_zips[i], root / "fxdata" / required_sprite_zips[i]);
        fs::create_symlink(install / "data" / "png_conv_pal.dat", root / "data" / "png_conv_pal.dat");
        prev_runtime_dir = keeper_runtime_directory;
        prev_inst_path = install_info.inst_path;
        prev_levels = campaign.levels_location;
        prev_configs = campaign.configs_location;
        prev_true_colour = keeperfx_ui_config.sprite_true_colour;
        set(keeper_runtime_directory, sizeof(keeper_runtime_directory), root.string());
        set(install_info.inst_path, sizeof(install_info.inst_path), "");
        set(campaign.levels_location, sizeof(campaign.levels_location), "levels");
        set(campaign.configs_location, sizeof(campaign.configs_location), "campgns/test_cfgs");
        std::ofstream cfg(root / MODS_DIR_NAME / MODS_LOAD_ORDER_FILE_NAME);
        cfg << "[" << MODS_AFTER_BASE_BLOCK_NAME << "]\n";
        for (const auto &m : mods)
        {
            fs::create_directories(root / MODS_DIR_NAME / m.first / "fxdata");
            for (const std::string &z : m.second)
                fs::copy_file(install / "fxdata" / z, root / MODS_DIR_NAME / m.first / "fxdata" / z); // listed, not opened by name: no link
            cfg << m.first << "\n";
        }
        cfg.close();
        REQUIRE(load_mods_order_config_file());
        recheck_all_mod_exist();
    }
    ~ZipRuntime()
    {
        keeperfx_ui_config.sprite_true_colour = prev_true_colour;
        sprite_hd_zip_frames_clear();
        set(keeper_runtime_directory, sizeof(keeper_runtime_directory), prev_runtime_dir);
        set(install_info.inst_path, sizeof(install_info.inst_path), prev_inst_path);
        set(campaign.levels_location, sizeof(campaign.levels_location), prev_levels);
        set(campaign.configs_location, sizeof(campaign.configs_location), prev_configs);
        std::error_code ec;
        fs::remove(root / MODS_DIR_NAME / MODS_LOAD_ORDER_FILE_NAME, ec);
        load_mods_order_config_file();
        fs::remove_all(root, ec);
    }
};

struct SlotFrame { int64_t slot; uint64_t key; };

/** (zip file name, PNG) -> slot and key, for every recorded slot whose zip is in a mod folder. */
std::map<std::pair<std::string, std::string>, SlotFrame> mod_frames()
{
    std::map<std::pair<std::string, std::string>, SlotFrame> out;
    for (int64_t i = 0; i < KEEPERSPRITE_ADD_NUM; i++)
    {
        uint64_t key = 0;
        const char *zip = nullptr, *png = nullptr;
        if (!sprite_hd_zip_frame(i, &key, &zip, &png))
            continue;
        const std::string z(zip);
        if (z.find("/" MODS_DIR_NAME "/") == std::string::npos)
            continue;
        out[{fs::path(z).filename().string(), png}] = {i, key};
    }
    return out;
}

/** The slot's quantised frame: true where it draws (inside a run). */
std::vector<bool> drawn_mask(const unsigned char *rle, int64_t w, int64_t h)
{
    std::vector<bool> m((size_t)(w * h), false);
    size_t p = 0;
    for (int64_t y = 0; y < h; y++)
    {
        int64_t x = 0;
        for (;;)
        {
            const int n = (signed char)rle[p++];
            if (n == 0)
                break;
            if (n < 0) { x -= n; continue; }
            for (int k = 0; k < n; k++, x++)
                if ((x >= 0) && (x < w))
                    m[(size_t)(y * w + x)] = true;
            p += (size_t)n;
        }
    }
    return m;
}

} // namespace

TEST_CASE("Zip frames keep their keys when the zips load in another order", "[kfx_render][sprite_hd_zip_compat]")
{
    const char *dir = std::getenv("KFX_SPRITES_COMPAT_DIR");
    if (dir == nullptr)
        SKIP("KFX_SPRITES_COMPAT_DIR not set");
    std::map<std::pair<std::string, std::string>, SlotFrame> first, second;
    {
        ZipRuntime rt(dir, {{"mod_a", {"druid.zip"}}, {"mod_b", {"time_mage.zip"}}});
        init_custom_sprites(1);
        first = mod_frames();
    }
    {
        ZipRuntime rt(dir, {{"mod_b", {"time_mage.zip"}}, {"mod_a", {"druid.zip"}}});
        init_custom_sprites(1);
        second = mod_frames();
    }
    REQUIRE(first.size() > 500);
    REQUIRE(first.size() == second.size());
    int64_t moved = 0, keyed = 0;
    for (const auto &kv : first)
    {
        const auto it = second.find(kv.first);
        REQUIRE(it != second.end());
        CHECK(it->second.key == kv.second.key);
        moved += (it->second.slot != kv.second.slot);
        keyed += (kv.second.key != 0);
    }
    CHECK(moved == (int64_t)first.size()); // every frame in another slot
    CHECK(keyed == (int64_t)first.size());
}

TEST_CASE("True colours are the PNG's, drawn exactly where the quantised frame draws, and read back the same",
    "[kfx_render][sprite_hd_zip_compat]")
{
    const char *dir = std::getenv("KFX_SPRITES_COMPAT_DIR");
    if (dir == nullptr)
        SKIP("KFX_SPRITES_COMPAT_DIR not set");
    ZipRuntime rt(dir);
    keeperfx_ui_config.sprite_true_colour = 1;
    init_custom_sprites(1);
    int64_t slots = 0, mismatched = 0;
    std::vector<std::vector<TbPixel>> loaded(KEEPERSPRITE_ADD_NUM);
    for (int64_t i = 0; i < KEEPERSPRITE_ADD_NUM; i++)
    {
        if (!sprite_hd_zip_frame(i, nullptr, nullptr, nullptr))
            continue;
        const TbPixel *tc = sprite_hd_zip_true_colour(i);
        REQUIRE(tc != nullptr);
        const int64_t w = creature_table_add[i].SWidth, h = creature_table_add[i].SHeight;
        const std::vector<bool> mask = drawn_mask(keepersprite_add[i], w, h);
        for (int64_t p = 0; p < w * h; p++)
            mismatched += ((tc[p].a >= 128) != mask[(size_t)p]);
        loaded[(size_t)i].assign(tc, tc + w * h);
        slots++;
    }
    CHECK(slots == 3420);
    CHECK(mismatched == 0);
    CHECK(sprite_hd_stats()->zip_frames == 3420);

    // Off: freed. On again: read back from the zips, the same colours.
    keeperfx_ui_config.sprite_true_colour = 0;
    sprite_hd_frame_begin();
    CHECK(sprite_hd_zip_true_colour(0) == nullptr);
    CHECK(sprite_hd_stats()->zip_frames == 0);
    keeperfx_ui_config.sprite_true_colour = 1;
    sprite_hd_frame_begin();
    int64_t same = 0;
    for (int64_t i = 0; i < KEEPERSPRITE_ADD_NUM; i++)
    {
        if (loaded[(size_t)i].empty())
            continue;
        const TbPixel *tc = sprite_hd_zip_true_colour(i);
        REQUIRE(tc != nullptr);
        same += (std::memcmp(tc, loaded[(size_t)i].data(), loaded[(size_t)i].size() * sizeof(TbPixel)) == 0);
    }
    CHECK(same == slots);
}
