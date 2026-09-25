/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_mappack.cpp
 *     See editor_mappack.h.
 */
#include "pre_inc.h"
#include "editor_mappack.h"
#include "config.h"
#include "config_campaigns.h"
#include "bflib_fileio.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <system_error>
#include "post_inc.h"

namespace {
const char *const kPackDirName = "editormaps";
const char *const kPackCfgName = "editormaps.cfg";

std::string strip_slashes(std::string s)
{
    while (s.size() > 1 && (s.back() == '/' || s.back() == '\\'))
        s.pop_back();
    return s;
}
}

std::string editor_maps_dir(void)
{
    return prepare_file_path(FGrp_VarLevels, kPackDirName);
}

LevelNumber editor_maps_next_free_number(const char *dir)
{
    for (LevelNumber n = 1; n < 100000; n++)
    {
        char path[600];
        snprintf(path, sizeof(path), "%s/map%05" PRIu64 ".slb", dir, (uint64_t)n);
        if (!LbFileExists(path))
            return n;
    }
    return 1;
}

bool editor_maps_ensure_dir(void)
{
    std::error_code ec;
    std::filesystem::create_directories(editor_maps_dir(), ec);
    return std::filesystem::is_directory(editor_maps_dir(), ec);
}

bool editor_maps_is_dir(const char *dir)
{
    if (dir == nullptr)
        return false;
    return strip_slashes(dir) == strip_slashes(editor_maps_dir());
}

void editor_maps_register(void)
{
    const std::string cfg = std::string(prepare_file_path(FGrp_VarLevels, kPackCfgName));
    if (std::filesystem::exists(cfg))
        return;
    {
        std::ofstream out(cfg);
        if (!out)
            return;
        out << "; KeeperFX Mappack file -- maps made in the level editor\n"
               "\n"
               "[common]\n"
               "NAME = Editor Maps\n"
               "LEVELS_LOCATION = levels/" << kPackDirName << "\n"
               "HIGH_SCORES = 9999 scr_edmaps.dat\n"
               "HUMAN_PLAYER = RED\n";
    }
    // The pack list is read at start-up; scan again so the new pack can be picked now.
    load_campaigns_list(&mappacks_list, FGrp_VarLevels, "mappacks", "mappck_order.txt");
}
