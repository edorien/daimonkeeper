// kfx_render: HD texture packs against real KeeperFX content (docs/newFeatures/texture-resolution/
// 03-verification-and-phases.md §E). Optional: needs an install's data and a pack, so it skips unless
//     KFX_TEXRES_COMPAT_DIR=<an install, e.g. core_files>  KFX_TEXRES_PACK=<a pack made against its data/>
// are set (texpack.py upscale makes such a pack from data/ itself).
//
// Each case copies the files it needs (base data/, the campaign's or map's own tmap files, a mod) into a temp
// runtime directory -- the install is never written to -- loads them through the real lookup, and checks that
// every slot of the case's file got HD exactly when its content key is one of the pack's (read here from
// keys/*.idx, independently of the loader). The per-file counts are printed for the §E table.
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_set>
#include <unistd.h>
#include <vector>

#include "config.h"
#include "config_campaigns.h"
#include "config_keeperfx.h"
#include "config_mods.h"
#include "engine_textures.h"
#include "engine_textures_hd.h"
#include "kfx_config_state.h"

namespace fs = std::filesystem;

namespace {

struct CompatCase {
    const char *name;
    const char *levels_location;  ///< campaign.levels_location, relative to the install
    const char *configs_location; ///< campaign.configs_location
    LevelNumber level;
    int64_t variation;            ///< the slot pair checked: 0 the level's own (tmapidx 0), v >= 1 tmap?{v-1}
    int letter;                   ///< 0 a, 1 b
    std::vector<std::string> copy; ///< files and folders to copy from the install, besides data/
    const char *mod;              ///< a mod to list after_base, or nullptr
};

/** Copies a file or folder of the install into the runtime directory (missing ones are skipped). */
void copy_in(const fs::path &install, const fs::path &root, const std::string &rel)
{
    std::error_code ec;
    const fs::path src = install / rel;
    if (!fs::exists(src, ec))
        return;
    fs::create_directories((root / rel).parent_path(), ec);
    fs::copy(src, root / rel, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
}

std::unordered_set<uint64_t> pack_keys(const fs::path &pack)
{
    std::unordered_set<uint64_t> keys;
    for (const auto &entry : fs::directory_iterator(pack / "keys"))
    {
        std::ifstream in(entry.path(), std::ios::binary);
        unsigned char b[8];
        while (in.read(reinterpret_cast<char *>(b), 8))
        {
            uint64_t k = 0;
            for (int i = 7; i >= 0; i--)
                k = (k << 8) | b[i];
            if (k != 0)
                keys.insert(k);
        }
    }
    return keys;
}

bool uniform(const unsigned char *block)
{
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++)
            if (block[y * 256 + x] != block[0])
                return false;
    return true;
}

void set(char *dst, size_t size, const std::string &value)
{
    std::strncpy(dst, value.c_str(), size - 1);
    dst[size - 1] = '\0';
}

} // namespace

TEST_CASE("HD packs on real content: each slot gets HD exactly when its content has a key", "[engine_textures_hd_compat]")
{
    const char *install_env = std::getenv("KFX_TEXRES_COMPAT_DIR");
    const char *pack_env = std::getenv("KFX_TEXRES_PACK");
    if ((install_env == nullptr) || (pack_env == nullptr))
        SKIP("KFX_TEXRES_COMPAT_DIR and KFX_TEXRES_PACK not set");
    const fs::path install = install_env;
    const fs::path pack = pack_env;
    REQUIRE(fs::exists(install / "data" / "tmapa000.dat"));
    REQUIRE(fs::exists(pack / "pack.toml"));
    const std::unordered_set<uint64_t> keys = pack_keys(pack);

    const std::vector<CompatCase> cases = {
        {"map-specific override (halloween map32432.tmapa002)", "levels/halloween", "levels/halloween_cfgs", 32432, 3, 0,
         {"levels/halloween/map32432.tmapa002.dat"}, nullptr},
        {"campaign tmapb002 over base tmapa002: the a file", "levels/halloween", "levels/halloween_cfgs", 1, 3, 0,
         {"levels/halloween_cfgs"}, nullptr},
        {"campaign tmapb002 over base tmapa002: the b file", "levels/halloween", "levels/halloween_cfgs", 1, 3, 1,
         {"levels/halloween_cfgs"}, nullptr},
        {"campaign pack beyond 013 reusing base art (dk2maps tmapa015)", "multiplayer/dk2maps", "multiplayer/dk2maps_cfgs", 1, 16, 0,
         {"multiplayer/dk2maps_cfgs"}, nullptr},
        {"campaign pack beyond 013, mostly new art (necro tmapa014)", "campgns/necro", "campgns/necro_cfgs", 1, 15, 0,
         {"campgns/necro_cfgs"}, nullptr},
        {"campaign pack beyond 013, mostly new art (Tyraels_realms tmapa014)", "campgns/Tyraels_realms", "campgns/Tyraels_realms_cfg", 1, 15, 0,
         {"campgns/Tyraels_realms_cfg"}, nullptr},
        {"map-specific campaign pack (AbyssGuardians MAP10005.tmapa013)", "campgns/AbyssGuardians", "campgns/AbyssGuardians/CONFIG", 10005, 14, 0,
         {"campgns/AbyssGuardians/MAP10005.tmapa013.dat", "campgns/AbyssGuardians/CONFIG"}, nullptr},
        {"full-size campaign tmapb (AbyssGuardians CONFIG/tmapb002)", "campgns/AbyssGuardians", "campgns/AbyssGuardians/CONFIG", 1, 3, 1,
         {"campgns/AbyssGuardians/CONFIG"}, nullptr},
        {"mod texture pack (Player_texture_pack tmapa016)", "levels", "", 1, 17, 0,
         {"mods/Player_texture_pack"}, "Player_texture_pack"},
    };

    const fs::path root = fs::temp_directory_path() / ("kfx_texhd_compat_" + std::to_string(getpid()));
    const std::string prev_runtime = keeper_runtime_directory, prev_inst = install_info.inst_path;
    const std::string prev_levels = campaign.levels_location, prev_configs = campaign.configs_location;
    const std::string prev_pack = keeperfx_ui_config.asset_pack;
    for (const CompatCase &c : cases)
    {
        CAPTURE(c.name);
        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(root / "levels");
        copy_in(install, root, "data");
        for (const std::string &rel : c.copy)
            copy_in(install, root, rel);
        fs::create_directories(root / "fxdata" / "asset_packs" / "compat");
        fs::create_directory_symlink(fs::absolute(pack), root / "fxdata" / "asset_packs" / "compat" / "textures");
        if (c.mod != nullptr)
            std::ofstream(root / MODS_DIR_NAME / MODS_LOAD_ORDER_FILE_NAME) << "[" << MODS_AFTER_BASE_BLOCK_NAME << "]\n" << c.mod << "\n";
        set(keeper_runtime_directory, sizeof(keeper_runtime_directory), root.string());
        set(install_info.inst_path, sizeof(install_info.inst_path), "");
        set(campaign.levels_location, sizeof(campaign.levels_location), c.levels_location);
        set(campaign.configs_location, sizeof(campaign.configs_location), c.configs_location);
        set(keeperfx_ui_config.asset_pack, sizeof(keeperfx_ui_config.asset_pack), "compat");
        load_mods_order_config_file();
        recheck_all_mod_exist();
        setup_texture_block_mem();

        REQUIRE(load_texture_map_file(0, c.level, FGrp_CmpgLvls));
        const struct TextureMapFile &file = texture_map_files[c.variation][c.letter];
        REQUIRE(file.loaded);
        int blocks = 0, expected = 0, got = 0, wrong = 0;
        const int64_t first = c.variation * TEXTURE_BLOCKS_COUNT + ((c.letter == 0) ? 0 : TEX_B_START_POINT);
        const int64_t count = (c.letter == 0) ? TEXTURE_BLOCKS_STAT_COUNT_A : TEXTURE_BLOCKS_STAT_COUNT_B;
        for (int64_t i = 0; i < count; i++)
        {
            const unsigned char *block = block_ptrs[first + i];
            if (uniform(block))
            {
                wrong += (texture_hd_slot_source(first + i) != TexHdSrc_None) ? 1 : 0;
                continue;
            }
            blocks++;
            const bool should = keys.count(texture_hd_block_key(block)) > 0;
            const bool has = texture_hd_slot_source(first + i) == TexHdSrc_UserPack;
            expected += should ? 1 : 0;
            got += has ? 1 : 0;
            wrong += (should != has) ? 1 : 0;
        }
        std::string where = file.path;
        if (where.rfind(root.string(), 0) == 0)
            where = where.substr(root.string().size() + 1);
        std::printf("%-66s %s (%s): HD %d/%d blocks\n", c.name, file.stem, where.c_str(), got, blocks);
        CHECK(wrong == 0);
        CHECK(got == expected);
    }
    set(keeper_runtime_directory, sizeof(keeper_runtime_directory), prev_runtime);
    set(install_info.inst_path, sizeof(install_info.inst_path), prev_inst);
    set(campaign.levels_location, sizeof(campaign.levels_location), prev_levels);
    set(campaign.configs_location, sizeof(campaign.configs_location), prev_configs);
    set(keeperfx_ui_config.asset_pack, sizeof(keeperfx_ui_config.asset_pack), prev_pack);
    std::error_code ec;
    fs::remove(root / MODS_DIR_NAME / MODS_LOAD_ORDER_FILE_NAME, ec);
    load_mods_order_config_file();
    texture_hd_clear();
    fs::remove_all(root, ec);
}
