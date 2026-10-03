// kfx_config: golden hashes of the base config reader (refactor pass 4, S04: daimonkeeper.cfg's keys are read
// through the options screen's settings schema; these hashes must not change when they are).
//
// For every key of conf_commands[] and each of a list of values (numbers in and out of every range the reader
// uses, words of every name table it knows, the boolean words, text, nothing, several values), a one-line file
// is read as the base config (load_base_config_file_for_test: no mods, a fresh start) over the same starting
// state, and everything a key can write is hashed: the config globals and structs, the three fields of
// kfx_config_state it writes (the rest is checked unchanged once, at the end), the
// platform globals it sets (atmos volume, display, mouse grab, vsync, ENet port), the screen mode, the
// renderer's type, GPU debug and lighting, the log level, and the matchmaking calls it makes. Warnings aren't
// hashed.
//
// To regenerate (only on a commit meant to change what the file does):
//     KFX_CONFIG_GOLDEN_WRITE=1 kfx_config_utest "[base_config_golden]"
#include <catch2/catch_test_macros.hpp>

#include "bflib_basics.h"
#include "bflib_enet.h"
#include "bflib_inputctrl.h"
#include "bflib_sound.h"
#include "bflib_video.h"
#include "config.h"
#include "config_keeperfx.h"
#include "config_settings.h"
#include "kfx_config_state.h"
#include "ports/net_port.h"
#include "renderer/RendererManager.h"
#include "kfx_config_test_paths.h" // KFX_CONFIG_TEST_FIXTURES_DIR
#include "scoped_port_override.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

extern "C" const struct NamedCommand conf_commands[];

namespace {

const std::string kGoldenFile = std::string(KFX_CONFIG_TEST_FIXTURES_DIR) + "/base_config_golden.txt";

struct Fnv {
    uint64_t v = 1469598103934665603ULL;
    void add(const void *p, size_t n) {
        const unsigned char *b = static_cast<const unsigned char *>(p);
        for (size_t i = 0; i < n; i++) { v ^= b[i]; v *= 1099511628211ULL; }
    }
    template <class T> void add_v(const T &t) { add(&t, sizeof(t)); }
    void add_s(const std::string &s) { add(s.c_str(), s.size() + 1); }
};

// What the matchmaking calls did (net port stubs).
std::string matchmaking_log;
void mm_set_enabled(TbBool enabled) { matchmaking_log += enabled ? "on;" : "off;"; }
void mm_set_server(const char *host) { matchmaking_log += std::string("server=") + (host ? host : "(null)") + ";"; }
const char *mm_ws_url(void) { return "ws://stub"; }

/** Everything a key of the base config can write, saved once and put back before each file. */
struct State {
    uint64_t features_enabled_;
    struct KfxRuntimeSettings runtime;
    struct KeeperFxUiConfig ui;
    struct InstallInfo install;
    struct StartupParameters start;
    int64_t atmos_volume, atmos_start, atmos_end, atmos_repeat, display, api_en, api_p, enet_p;
    TbBool mouse_grab, vsync, autosave, exit_lua, flee, imprison;
    uint64_t vid_scale, packetsave;
    uint64_t replays[ReplTyp_Count];
    // The three fields of kfx_config_state the reader writes (atmos frequency, zoom distances): the whole struct
    // (11 MB) is compared once, at the end, to show nothing else changes.
    int64_t config_fields[3];
    TbScreenMode vidmode;
    RendererType renderer;
    TbBool gpu_debug;
    int lighting;
    int64_t log_level;

    void save() {
        features_enabled_ = features_enabled; runtime = kfx_runtime_settings; ui = keeperfx_ui_config;
        install = install_info; start = start_params; atmos_volume = atmos_sound_volume;
        atmos_start = AtmosStart; atmos_end = AtmosEnd; atmos_repeat = AtmosRepeat; display = display_id;
        api_en = api_enabled; api_p = api_port; enet_p = enet_port; mouse_grab = lbMouseGrab; vsync = vsync_enabled;
        autosave = automatic_replays; exit_lua = exit_on_lua_error; flee = FLEE_BUTTON_DEFAULT;
        imprison = IMPRISON_BUTTON_DEFAULT; vid_scale = vid_scale_flags; packetsave = packetsave_max_kb;
        std::memcpy(replays, max_replays, sizeof(replays));
        config_fields[0] = kfx_config_state.atmos_sound_frequency;
        config_fields[1] = kfx_config_state.zoom_distance_setting;
        config_fields[2] = kfx_config_state.frontview_zoom_distance_setting;
        vidmode = get_screen_vidmode(); renderer = RendererGetDesiredType(); gpu_debug = RendererGetGpuDebug();
        lighting = RendererGetLightingMode(); log_level = get_log_level();
    }
    void restore() const {
        features_enabled = features_enabled_; kfx_runtime_settings = runtime; keeperfx_ui_config = ui;
        install_info = install; start_params = start; atmos_sound_volume = atmos_volume;
        AtmosStart = atmos_start; AtmosEnd = atmos_end; AtmosRepeat = atmos_repeat; display_id = display;
        api_enabled = api_en; api_port = api_p; enet_port = enet_p; lbMouseGrab = mouse_grab; vsync_enabled = vsync;
        automatic_replays = autosave; exit_on_lua_error = exit_lua; FLEE_BUTTON_DEFAULT = flee;
        IMPRISON_BUTTON_DEFAULT = imprison; vid_scale_flags = vid_scale; packetsave_max_kb = packetsave;
        std::memcpy(max_replays, replays, sizeof(replays));
        kfx_config_state.atmos_sound_frequency = config_fields[0];
        kfx_config_state.zoom_distance_setting = config_fields[1];
        kfx_config_state.frontview_zoom_distance_setting = config_fields[2];
        set_screen_vidmode(vidmode); RendererSetDesiredType(renderer); RendererSetGpuDebug(gpu_debug);
        RendererSetLightingMode(lighting); set_log_level_from_config(log_level);
        matchmaking_log.clear();
    }
};

uint64_t state_hash() {
    Fnv h;
    h.add_v(features_enabled); h.add_v(kfx_runtime_settings); h.add_v(keeperfx_ui_config); h.add_v(install_info);
    h.add_v(start_params); h.add_v(atmos_sound_volume); h.add_v(AtmosStart); h.add_v(AtmosEnd); h.add_v(AtmosRepeat);
    h.add_v(display_id); h.add_v(api_enabled); h.add_v(api_port); h.add_v(enet_port);
    const TbBool grab = lbMouseGrab;
    h.add_v(grab); h.add_v(vsync_enabled); h.add_v(automatic_replays); h.add_v(exit_on_lua_error);
    h.add_v(FLEE_BUTTON_DEFAULT); h.add_v(IMPRISON_BUTTON_DEFAULT); h.add_v(vid_scale_flags); h.add_v(packetsave_max_kb);
    h.add(max_replays, sizeof(max_replays)); h.add_v(kfx_config_state.atmos_sound_frequency); h.add_v(kfx_config_state.zoom_distance_setting);
    h.add_v(kfx_config_state.frontview_zoom_distance_setting);
    h.add_v(get_screen_vidmode()); h.add_v(RendererGetDesiredType()); h.add_v(RendererGetGpuDebug());
    h.add_v(RendererGetLightingMode()); h.add_v(get_log_level()); h.add_s(matchmaking_log);
    return h.v;
}

std::vector<std::string> values() {
    std::vector<std::string> v = {"", "0", "1", "-1", "2", "5", "7", "8", "9", "10", "35", "50", "64", "65", "100", "101",
        "160", "161", "200", "201", "500", "501", "10000", "10001", "32768", "32769", "65535", "65536",
        "2147483647", "abc", "12abc", "1 2 3", "4 0 9", "-5 6", "LEGAL FX INTRO", "EA BULLFROG NOPE",
        "640x480", "1920x1080x32", "C:/games/dk", "./", "wss://example.org:1234", std::string(200, 'x')};
    // the boolean words, and every name table of the reader
    for (const char *w : {"ENABLED", "DISABLED", "ON", "OFF", "TRUE", "FALSE", "YES", "NO", "ALWAYS", "NEVER"})
        v.push_back(w);
    std::ifstream words(std::string(KFX_CONFIG_TEST_FIXTURES_DIR) + "/base_config_words.txt");
    std::string w;
    while (std::getline(words, w))
        if (!w.empty())
            v.push_back(w);
    return v;
}

std::string scratch_file() {
    static const std::string path = (std::filesystem::temp_directory_path() /
        ("kfx_base_config_" + std::to_string(getpid()) + ".cfg")).string();
    return path;
}

} // namespace

TEST_CASE("golden: what every base config key does with each value", "[kfx_config][base_config_golden]")
{
    ScopedPortOverride<NetPort> net(net_port, set_net_port);
    net->matchmaking_set_enabled = mm_set_enabled;
    net->matchmaking_set_server = mm_set_server;
    net->matchmaking_get_ws_url = mm_ws_url;
    State base;
    base.save();
    auto config_before = std::make_unique<struct KfxConfigState>(); // too big for the stack
    std::memcpy(config_before.get(), &kfx_config_state, sizeof(kfx_config_state));
    std::map<std::string, uint64_t> actual;
    const std::vector<std::string> vals = values();
    for (const struct NamedCommand *cmd = conf_commands; cmd->name != nullptr; cmd++) {
        for (const std::string &value : vals) {
            base.restore();
            {
                std::ofstream out(scratch_file(), std::ios::binary | std::ios::trunc);
                out << "; base config golden test\n" << cmd->name << "=" << value << "\n";
            }
            load_base_config_file_for_test(scratch_file().c_str());
            const std::string shown = value.size() > 40 ? value.substr(0, 12) + "...(" + std::to_string(value.size()) + ")" : value;
            actual[std::string(cmd->name) + "=" + shown] = state_hash();
        }
    }
    base.restore();
    std::remove(scratch_file().c_str());
    REQUIRE(actual.size() > 3000);
    // No key wrote any other part of kfx_config_state (only those three fields are put back between files).
    CHECK(std::memcmp(config_before.get(), &kfx_config_state, sizeof(kfx_config_state)) == 0);

    if (std::getenv("KFX_CONFIG_GOLDEN_WRITE") != nullptr) {
        std::ofstream out(kGoldenFile, std::ios::trunc);
        out << "# Golden hashes for base_config_golden_test.cpp (refactor pass 4, S04). Regenerate only on a commit\n"
               "# meant to change what the base config does: KFX_CONFIG_GOLDEN_WRITE=1.\n";
        for (const auto &kv : actual) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)kv.second);
            out << kv.first << "\t" << buf << "\n";
        }
        WARN("rewrote " << actual.size() << " base config golden hashes");
        return;
    }
    std::map<std::string, uint64_t> expected;
    std::ifstream in(kGoldenFile);
    std::string line;
    while (std::getline(in, line)) {
        const size_t tab = line.rfind('\t');
        if (line.empty() || line[0] == '#' || tab == std::string::npos) continue;
        expected[line.substr(0, tab)] = std::strtoull(line.c_str() + tab + 1, nullptr, 16);
    }
    REQUIRE_FALSE(expected.empty());
    std::ostringstream diff;
    int64_t bad = 0;
    for (const auto &kv : actual) {
        auto it = expected.find(kv.first);
        if ((it == expected.end() || it->second != kv.second) && bad++ < 40)
            diff << (it == expected.end() ? "new: " : "changed: ") << kv.first << "\n";
    }
    for (const auto &kv : expected)
        if (actual.find(kv.first) == actual.end() && bad++ < 40)
            diff << "missing: " << kv.first << "\n";
    INFO(bad << " of " << expected.size() << " golden hashes differ:\n" << diff.str());
    CHECK(bad == 0);
}
