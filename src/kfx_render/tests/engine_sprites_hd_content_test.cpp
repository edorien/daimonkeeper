// kfx_render: HD sprite content folders (docs/refactor/sprites/, Phase 5): init_custom_sprites() notes a sprites-hd/
// folder in each place it reads sprite zips from -- mods after_base, the campaign's config folder, mods
// after_campaign, the map's folder, mods after_map -- in that load order, and the HD store opens them most specific
// first, then the user pack (ASSET_PACK's sprites/): a key in several takes the most specific one's art.
//
// The runtime directory is a temp folder with a real mods/load_order.cfg, as engine_textures_test.cpp's TmapModDir.
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

#include "config.h"
#include "config_campaigns.h"
#include "config_keeperfx.h"
#include "config_mods.h"
#include "custom_sprites.h"
#include "engine_sprites_hd.h"
#include "engine_sprites_hd_pack.h"
#include "mod_config_types.h"

namespace fs = std::filesystem;

namespace {

void set(char *dst, size_t size, const std::string &value)
{
    std::strncpy(dst, value.c_str(), size - 1);
    dst[size - 1] = '\0';
}

/** A pack of frame lines with these keys (the art isn't read by these tests). */
void write_pack(const fs::path &dir, const std::vector<uint64_t> &keys)
{
    fs::create_directories(dir / "anim");
    std::ofstream(dir / "pack.toml") << "scale = 2\n";
    std::ofstream(dir / "animlist.txt") << "anim/filelist.txt\n";
    std::ofstream fl(dir / "anim" / "filelist.txt");
    fl << "animation_1 2 " << keys.size() << " 0 0\n";
    for (size_t i = 0; i < keys.size(); i++)
        fl << "f" << i << ".png\n";
    fs::create_directories(dir / "keys");
    std::ofstream idx(dir / "keys" / "animlist.idx", std::ios::binary);
    for (uint64_t key : keys)
    {
        for (int k = 0; k < 8; k++)
            idx.put((char)((key >> (8 * k)) & 0xFF));
        const uint16_t v[4] = { 0, 0, 4, 4 };
        for (uint16_t u : v)
        {
            idx.put((char)(u & 0xFF));
            idx.put((char)(u >> 8));
        }
    }
}

struct ContentRuntime {
    fs::path root;
    std::string prev_runtime_dir, prev_inst_path, prev_levels, prev_configs, prev_sprites;

    ContentRuntime()
    {
        root = fs::temp_directory_path() / ("kfx_sprhd_content_" + std::to_string(getpid()));
        fs::remove_all(root);
        for (const char *d : {"fxdata", "levels", "campgns/test_cfgs", "mods/base_mod/fxdata", "mods/camp_mod/fxdata",
                 "mods/camp_mod/levels", "mods/map_mod/levels"})
            fs::create_directories(root / d);
        prev_runtime_dir = keeper_runtime_directory;
        prev_inst_path = install_info.inst_path;
        prev_levels = campaign.levels_location;
        prev_configs = campaign.configs_location;
        prev_sprites = keeperfx_ui_config.asset_pack;
        set(keeper_runtime_directory, sizeof(keeper_runtime_directory), root.string());
        set(install_info.inst_path, sizeof(install_info.inst_path), "");
        set(campaign.levels_location, sizeof(campaign.levels_location), "levels");
        set(campaign.configs_location, sizeof(campaign.configs_location), "campgns/test_cfgs");
        std::ofstream cfg(root / MODS_DIR_NAME / MODS_LOAD_ORDER_FILE_NAME);
        cfg << "[" << MODS_AFTER_BASE_BLOCK_NAME << "]\nbase_mod\n[" << MODS_AFTER_CAMPAIGN_BLOCK_NAME << "]\ncamp_mod\n["
            << MODS_AFTER_MAP_BLOCK_NAME << "]\nmap_mod\n";
        cfg.close();
        REQUIRE(load_mods_order_config_file());
        recheck_all_mod_exist();
    }
    ~ContentRuntime()
    {
        sprite_hd_set_content_folders(nullptr, 0);
        sprite_pack_close_all();
        set(keeperfx_ui_config.asset_pack, sizeof(keeperfx_ui_config.asset_pack), prev_sprites);
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

bool same_folder(const char *a, const fs::path &b)
{
    std::error_code ec;
    return (a != nullptr) && fs::exists(a, ec) && fs::equivalent(a, b, ec);
}

} // namespace

TEST_CASE("init_custom_sprites() notes a sprites-hd folder in each place it reads sprite zips from, in load order",
    "[kfx_render][sprite_hd_content]")
{
    ContentRuntime rt;
    const std::vector<fs::path> want = {
        rt.root / "mods/base_mod/fxdata/sprites-hd",
        rt.root / "campgns/test_cfgs/sprites-hd",
        rt.root / "mods/camp_mod/fxdata/sprites-hd",
        rt.root / "mods/camp_mod/levels/sprites-hd",
        rt.root / "levels/sprites-hd",
        rt.root / "mods/map_mod/levels/sprites-hd",
    };
    for (const fs::path &p : want)
        fs::create_directories(p);
    init_custom_sprites(7);
    REQUIRE(sprite_hd_content_folder_count() == (int64_t)want.size());
    for (size_t i = 0; i < want.size(); i++)
    {
        INFO(i << ": " << sprite_hd_content_folder((int64_t)i) << ", want " << want[i]);
        CHECK(same_folder(sprite_hd_content_folder((int64_t)i), want[i]));
    }
}

TEST_CASE("A key in several content folders and the user pack takes the most specific folder's art",
    "[kfx_render][sprite_hd_content]")
{
    ContentRuntime rt;
    write_pack(rt.root / "fxdata/asset_packs/user/sprites", {0xA1, 0xA2, 0xA3});
    write_pack(rt.root / "mods/base_mod/fxdata/sprites-hd", {0xA1, 0xA2});
    write_pack(rt.root / "levels/sprites-hd", {0xA1});
    set(keeperfx_ui_config.asset_pack, sizeof(keeperfx_ui_config.asset_pack), "user");
    init_custom_sprites(7);
    sprite_hd_frame_begin(); // opens the packs
    auto source_of = [](uint64_t key) -> std::string {
        const int32_t e = sprite_pack_find(key);
        return (e < 0) ? std::string() : std::string(sprite_pack_source(sprite_pack_entry(e)->source)->dir);
    };
    CHECK(sprite_pack_entry_count() == 3);
    CHECK(same_folder(source_of(0xA1).c_str(), rt.root / "levels/sprites-hd"));
    CHECK(same_folder(source_of(0xA2).c_str(), rt.root / "mods/base_mod/fxdata/sprites-hd"));
    CHECK(same_folder(source_of(0xA3).c_str(), rt.root / "fxdata/asset_packs/user/sprites"));
    for (int64_t s = 0; s < sprite_pack_source_count(); s++)
        CHECK(sprite_pack_source(s)->content == !same_folder(sprite_pack_source(s)->dir, rt.root / "fxdata/asset_packs/user/sprites"));

    // Another level, whose only content folder is the base mod's: the map folder's key falls back to it.
    const std::string base = (rt.root / "mods/base_mod/fxdata/sprites-hd").string();
    const char *level2[] = { base.c_str() };
    sprite_hd_set_content_folders(level2, 1);
    sprite_hd_frame_begin();
    CHECK(same_folder(source_of(0xA1).c_str(), rt.root / "mods/base_mod/fxdata/sprites-hd"));
    CHECK(same_folder(source_of(0xA3).c_str(), rt.root / "fxdata/asset_packs/user/sprites"));
    sprite_hd_set_content_folders(nullptr, 0);
    sprite_hd_frame_begin(); // no content folders: the user pack alone
    CHECK(same_folder(source_of(0xA1).c_str(), rt.root / "fxdata/asset_packs/user/sprites"));
    CHECK(same_folder(source_of(0xA2).c_str(), rt.root / "fxdata/asset_packs/user/sprites"));
    CHECK(sprite_pack_source_count() == 1);
}
