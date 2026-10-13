/** @file harness_content.cpp
 *     Fuzz targets for the content a downloaded campaign or map pack brings: configuration files, campaign files, level
 *     scripts, language strings, map files, RNC-packed files (security pass S08, README.md).
 *
 *     Every target runs the game's own loader on a file the input made, the way the game would meet it: as the
 *     campaign's own override of a base file, as a campaign .cfg, as a level's map files.
 */
#include "fuzz_driver.h"
#include "harness_common.h"

#include <string>
#include <vector>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

#include "pre_inc.h"
#include "bflib_dernc.h"
#include "bflib_fileio.h"
#include "config.h"
#include "config_campaigns.h"
#include "config_creature.h"
#include "config_crtrmodel.h"
#include "config_crtrstates.h"
#include "config_cubes.h"
#include "config_effects.h"
#include "config_keeperfx.h"
#include "config_lenses.h"
#include "config_magic.h"
#include "config_objects.h"
#include "config_players.h"
#include "config_powerhands.h"
#include "config_rules.h"
#include "config_slabsets.h"
#include "config_sounds.h"
#include "config_spritecolors.h"
#include "config_terrain.h"
#include "config_textures.h"
#include "config_translation.h"
#include "config_trapdoor.h"
#include "config_compp.h"
#include "kfx_config_state.h"
#include "kfx_game_state.h"
#include "kfx_sim_state.h"
#include "level_script_override.h"
#include "lua_base.h"
#include "lvl_filesdk1.h"
#include "lvl_script.h"
#include "map_data.h"
#include "ariadne_update.h"
#include "compat_report.h"
#include "post_inc.h"

// (defined in lvl_script.c, not in a header: the game calls them from process_level_script())
extern "C" void process_check_new_creature_parties(void);
extern "C" void process_check_new_tunneller_parties(void);
extern "C" void process_values(void);
extern "C" void process_win_and_lose_conditions(void);
extern "C" TbBool load_campaign_strings_data_from_file(const char *fname, int64_t flags, struct GameCampaign *campgn, uint8_t lang_id);

namespace {

/** The game's configuration files, the ones a campaign or a map can override one by one. */
const ConfigFileData *const kConfigFiles[] = {
    &keeper_terrain_file_data, &keeper_objects_file_data, &keeper_trapdoor_file_data, &keeper_effects_file_data,
    &keeper_lenses_file_data, &keeper_magic_file_data, &keeper_creaturetp_file_data, &creature_states_file_data,
    &keeper_rules_file_data, &keeper_textureanim_file_data, &keeper_powerhands_file_data, &keeper_spritecolors_file_data,
    &keeper_cubes_file_data, &keeper_playerstates_file_data, &keeper_keepcomp_file_data, &keeper_slabset_file_data,
    &keeper_columns_file_data, &keeper_sounds_file_data, &keeper_translation_file_data,
};
constexpr size_t kConfigFilesNum = sizeof(kConfigFiles) / sizeof(kConfigFiles[0]);

bool g_base_loaded = false;

/** The base configuration, loaded as a level load does (once, in the parent: every input starts from a copy). */
int load_base(void)
{
    if (g_base_loaded)
        return 0;
    fuzz_runtime_dir();
    memset(&kfx_config_state, 0, sizeof(kfx_config_state));
    memset(&campaign, 0, sizeof(campaign));
    load_stats_files();
    open_lua_script(0); // a level's script is Lua too; the game opens the state before it loads the level's files
    compat_report_clear();
    g_base_loaded = true;
    return 0;
}

int init_config(void)
{
    return load_base();
}

/** Byte 0 picks the file, the rest is its content, as the campaign's override of the base one. */
int fuzz_config_file(const uint8_t *data, size_t size)
{
    if (size < 1)
        return 0;
    const ConfigFileData *fd = kConfigFiles[data[0] % kConfigFilesNum];
    char rel[96];
    fuzz_scratch_dir(rel, sizeof(rel), "cfg");
    fuzz_write_file(rel, fd->filename, data + 1, size - 1);
    snprintf(campaign.configs_location, sizeof(campaign.configs_location), "%s", rel);
    load_config(fd, CnfLd_ListOnly);
    load_config(fd, CnfLd_Standard | CnfLd_PreListed);
    // the creature models the file may have added are loaded after, as load_stats_files() does
    for (int64_t i = 1; i < kfx_config_state.conf.crtr_conf.model_count && i < 8; i++)
        load_default_creaturemodel_config(i, 0);
    compat_report_clear();
    fuzz_scratch_clean(rel);
    return 0;
}

int fuzz_creature_file(const uint8_t *data, size_t size)
{
    if (size < 1 || kfx_config_state.conf.crtr_conf.model_count < 2)
        return 0;
    const int64_t model = 1 + (int64_t)(data[0] % (kfx_config_state.conf.crtr_conf.model_count - 1));
    char rel[96];
    fuzz_scratch_dir(rel, sizeof(rel), "crtr");
    fuzz_write_file(rel, "model.cfg", data + 1, size - 1);
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s/model.cfg", fuzz_runtime_dir(), rel);
    load_creaturemodel_config_file(model, path, CnfLd_AcceptPartial | CnfLd_IgnoreErrors);
    compat_report_clear();
    fuzz_scratch_clean(rel);
    return 0;
}

int fuzz_campaign_file(const uint8_t *data, size_t size)
{
    // a campaign file is looked for in campgns/ of the runtime directory
    const std::string name = "camp-" + std::to_string((long)getpid()) + ".cfg";
    mkdir((std::string(fuzz_runtime_dir()) + "/campgns").c_str(), 0755);
    fuzz_write_file("campgns", name.c_str(), data, size);
    static struct GameCampaign camp;
    memset(&camp, 0, sizeof(camp));
    const TbBool loaded = load_campaign(name.c_str(), &camp, CnfLd_Standard, FGrp_Campgn);
    if (getenv("KFX_FUZZ_VERBOSE"))
        fprintf(stderr, "campaign_file: loaded=%d name='%s' single=%lld\n", (int)loaded, camp.name, (long long)camp.single_levels_count);
    free_campaign(&camp);
    compat_report_clear();
    unlink((std::string(fuzz_runtime_dir()) + "/campgns/" + name).c_str());
    return 0;
}

int fuzz_level_script(const uint8_t *data, size_t size)
{
    constexpr LevelNumber kLevel = 9876;
    std::string text((const char *)data, size);
    memset(&kfx_game_state, 0, sizeof(kfx_game_state));
    level_script_override_set(kLevel, "", text.c_str());
    if (preload_script(kLevel))
    {
        load_script(kLevel);
        // run what the script set up as if every condition held: the commands' process functions are where the
        // numbers a script wrote are used
        for (int64_t i = 0; i < kfx_game_state.script.conditions_num && i < CONDITIONS_COUNT; i++)
            kfx_game_state.script.conditions[i].status |= 0x01;
        process_check_new_creature_parties();
        process_check_new_tunneller_parties();
        process_values();
        process_win_and_lose_conditions();
    }
    level_script_override_clear();
    compat_report_clear();
    return 0;
}

int fuzz_strings_file(const uint8_t *data, size_t size)
{
    char rel[96];
    fuzz_scratch_dir(rel, sizeof(rel), "strs");
    fuzz_write_file(rel, "strings.dat", data, size);
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s/strings.dat", fuzz_runtime_dir(), rel);
    static struct GameCampaign camp;
    memset(&camp, 0, sizeof(camp));
    const TbBool loaded = load_campaign_strings_data_from_file(path, 0, &camp, Lang_English);
    if (getenv("KFX_FUZZ_VERBOSE"))
        fprintf(stderr, "strings_file: loaded=%d\n", (int)loaded);
    free_campaign(&camp);
    fuzz_scratch_clean(rel);
    return 0;
}

/** An RNC-packed (or plain) file as every loader of the game meets it. */
int fuzz_rnc_file(const uint8_t *data, size_t size)
{
    char rel[96];
    fuzz_scratch_dir(rel, sizeof(rel), "rnc");
    fuzz_write_file(rel, "f.bin", data, size);
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s/f.bin", fuzz_runtime_dir(), rel);
    const int64_t len = LbFileLengthRnc(path);
    if (len > 0 && len < (64 << 20))
    {
        unsigned char *buf = (unsigned char *)calloc((size_t)len + 1, 1);
        LbFileLoadAt(path, buf);
        free(buf);
        unsigned char *small = (unsigned char *)calloc(64, 1);
        LbFileLoadAtMax(path, small, 64);
        free(small);
    }
    calculate_file_checksum(path);
    fuzz_scratch_clean(rel);
    return 0;
}

/* ---- map files ---- */

const char *const kMapExts[] = { "slb", "clm", "dat", "apt", "aptfx", "lgt", "lgtfx", "own", "tng", "tngfx", "wib", "wlb", "flg", "inf", "lif", "txt", "lof", "tsl" };
constexpr size_t kMapExtsNum = sizeof(kMapExts) / sizeof(kMapExts[0]);
std::string g_map_base;

int init_map(void)
{
    if (load_base() != 0)
        return -1;
    const char *dir = getenv("KFX_FUZZ_MAP_DIR");
    g_map_base = dir ? dir : KFX_FUZZ_REPO_ROOT "/core_files/campgns/ancntkpr";
    struct stat st;
    if (stat((g_map_base + "/map00001.slb").c_str(), &st) != 0)
    {
        fprintf(stderr, "map_file: no %s/map00001.slb (set KFX_FUZZ_MAP_DIR to a folder with a level's files)\n", g_map_base.c_str());
        return -1;
    }
    return 0;
}

int fuzz_map_file(const uint8_t *data, size_t size)
{
    if (size < 1)
        return 0;
    const char *ext = kMapExts[data[0] % kMapExtsNum];
    char rel[96];
    fuzz_scratch_dir(rel, sizeof(rel), "lvl");
    // the base level's files, linked in, but for the one the input replaces
    if (DIR *d = opendir(g_map_base.c_str()))
    {
        while (struct dirent *e = readdir(d))
        {
            const std::string name = e->d_name;
            if (name.size() < 10 || name.compare(0, 8, "map00001") != 0)
                continue;
            if (name.compare(9, std::string::npos, ext) == 0)
                continue;
            if (symlink((g_map_base + "/" + name).c_str(), (std::string(fuzz_runtime_dir()) + "/" + rel + "/" + name).c_str()) != 0) {}
        }
        closedir(d);
    }
    fuzz_write_file(rel, (std::string("map00001.") + ext).c_str(), data + 1, size - 1);
    snprintf(campaign.levels_location, sizeof(campaign.levels_location), "%s", rel);
    snprintf(campaign.fname, sizeof(campaign.fname), "fuzz.cfg");
    memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
    set_selected_level_number(1);
    init_map_size(1);
    if (load_map_file(1))
        init_navigation();
    compat_report_clear();
    fuzz_scratch_clean(rel);
    return 0;
}

/** Each base configuration file, with its selector byte in front, and every overlay the campaigns shipped with the game
 *  have for it (KFX_FUZZ_CAMPAIGNS, default core_files/campgns). */
int seeds_config(const char *dir)
{
    const char *camps = getenv("KFX_FUZZ_CAMPAIGNS");
    std::string campdir = camps ? camps : KFX_FUZZ_REPO_ROOT "/core_files/campgns";
    int written = 0;
    for (size_t i = 0; i < kConfigFilesNum; i++)
    {
        std::vector<std::string> sources;
        sources.push_back(std::string(KFX_FUZZ_REPO_ROOT "/config/fxdata/") + kConfigFiles[i]->filename);
        if (DIR *d = opendir(campdir.c_str()))
        {
            while (struct dirent *e = readdir(d))
            {
                if (e->d_name[0] == '.')
                    continue;
                std::string sub = campdir + "/" + e->d_name;
                if (DIR *dd = opendir(sub.c_str()))
                {
                    closedir(dd);
                    sources.push_back(sub + "/" + kConfigFiles[i]->filename);
                }
            }
            closedir(d);
        }
        int n = 0;
        for (const std::string &src : sources)
        {
            FILE *f = fopen(src.c_str(), "rb");
            if (f == NULL)
                continue;
            std::string bytes(1, (char)i);
            char buf[4096];
            size_t got;
            while ((got = fread(buf, 1, sizeof(buf), f)) > 0 && bytes.size() < 16000)
                bytes.append(buf, got);
            fclose(f);
            char path[512];
            snprintf(path, sizeof(path), "%s/%02zu-%d", dir, i, n++);
            if (FILE *o = fopen(path, "wb"))
            {
                fwrite(bytes.data(), 1, bytes.size(), o);
                fclose(o);
                written++;
            }
        }
    }
    fprintf(stderr, "config_file: %d seeds\n", written);
    return written > 0 ? 0 : 1;
}

int seeds_map(const char *dir)
{
    if (init_map() != 0)
        return 1;
    int written = 0;
    for (size_t i = 0; i < kMapExtsNum; i++)
    {
        FILE *f = fopen((g_map_base + "/map00001." + kMapExts[i]).c_str(), "rb");
        if (f == NULL)
            continue;
        std::string bytes(1, (char)i);
        char buf[4096];
        size_t got;
        while ((got = fread(buf, 1, sizeof(buf), f)) > 0 && bytes.size() < 65000)
            bytes.append(buf, got);
        fclose(f);
        char path[512];
        snprintf(path, sizeof(path), "%s/%02zu-%s", dir, i, kMapExts[i]);
        if (FILE *o = fopen(path, "wb"))
        {
            fwrite(bytes.data(), 1, bytes.size(), o);
            fclose(o);
            written++;
        }
    }
    fprintf(stderr, "map_file: %d seeds\n", written);
    return written > 0 ? 0 : 1;
}

} // namespace

static const char *const kScriptDict[] = {
    "IF(", "ENDIF", "ELSE", "WHILE", "REM ", "ADD_CREATURE_TO_POOL(", "WIN_GAME", "LOSE_GAME", "SET_FLAG(", "PLAYER0", "PLAYER_GOOD",
    "ALL_PLAYERS", "CREATURE_AVAILABLE(", "ROOM_AVAILABLE(", "MAGIC_AVAILABLE(", "TRAP_AVAILABLE(", "DOOR_AVAILABLE(", "GAME_TURN",
    "ADD_PARTY_TO_LEVEL(", "CREATE_PARTY(", "ADD_TO_PARTY(", "SET_TIMER(", "DISPLAY_OBJECTIVE(", "QUICK_OBJECTIVE(", "LEVEL_VERSION(1)",
    "SET_CREATURE_CONFIGURATION(", "SET_OBJECT_CONFIGURATION(", "SET_TRAP_CONFIGURATION(", "COMPUTER_PLAYER(", "SET_COMPUTER_PROCESS(",
    "NEXT_COMMAND_REUSABLE", "-1", "99999999999", "\r\n", "/*", "*/", NULL };
static const char *const kCfgDict[] = { "[common]", "[attributes]", "[experience]", "[jobs]", "[sounds]", "[sprites]", "NAME = ", "= ",
    "\n", "\r\n", "; ", "100000", "-1", "0xFFFF", "IMP", "WIZARD", NULL };

FUZZ_TARGET(target_config_file, "config_file", "byte 0 picks a config file (terrain, objects, ..., slabset.toml...), the rest is the campaign's override of it",
    init_config, fuzz_config_file, 16384, kCfgDict, seeds_config)
FUZZ_TARGET(target_creature_file, "creature_file", "byte 0 picks a creature model, the rest is its creatrs/<name>.cfg",
    init_config, fuzz_creature_file, 16384, kCfgDict, NULL)
FUZZ_TARGET(target_campaign_file, "campaign_file", "a campaign .cfg (levels, strings, speech, map blocks)",
    init_config, fuzz_campaign_file, 16384, kCfgDict, NULL)
FUZZ_TARGET(target_level_script, "level_script", "a level script (map*.txt): preload_script + load_script",
    init_config, fuzz_level_script, 16384, kScriptDict, NULL)
FUZZ_TARGET(target_strings_file, "strings_file", "a campaign's language strings file (.dat)",
    init_config, fuzz_strings_file, 16384, NULL, NULL)
FUZZ_TARGET(target_rnc_file, "rnc_file", "any file as LbFileLoadAt()/LbFileLoadAtMax() read it: plain or RNC-packed",
    init_config, fuzz_rnc_file, 8192, NULL, NULL)
FUZZ_TARGET(target_map_file, "map_file", "byte 0 picks one of a level's files (slb clm dat apt lgt own tng wib wlb flg ...), the rest replaces it; load + navigation",
    init_map, fuzz_map_file, 65536, NULL, seeds_map)
