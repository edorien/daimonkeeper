// kfx_game: golden hashes of the packet action and cheat cursor dispatchers (refactor pass 4, S05: their
// switches become tables of handler functions; these hashes must not change when they do).
//
// Every packet action (1..PckA_EditorGoSpectator), with three sets of parameters, goes through
// process_user_packet() (the global actions, the global cheats, then the dungeon-control actions and cheats);
// every cheat cursor mode (player work_state) goes through packets_process_cheats(), with and without a click,
// at two positions. Each case runs in a forked child on a bare level (player 0, a 85x85-slab map, nothing on
// it), which hashes kfx_sim_state, kfx_game_state, kfx_net_state, kfx_render_state, the game settings, the
// render's local_state, the messages the UI port was asked to show and, when it differs from the bare level's, the
// pathfinding state a save holds (kfx_pathfinding_state and Ariadne's mesh), and sends the hash back. Four cases
// need more than a bare level and are left out: PckA_SwitchView (render settings), PSt_MkGoodCreatr/PSt_MkBadCreatr (creature models) and PSt_PlaceTerrain (strings).
//
// To print the hashes (only on a commit meant to change what a packet does):
//     KFX_GAME_GOLDEN_PRINT=1 kfx_game_utest "[packet_golden]"
// A layout change's check: KFX_GAME_GOLDEN_CUT (see sim_state_hash()) on the commit before it.
#include <catch2/catch_test_macros.hpp>

#include "game_commands.h"
#include "packet_data.h"
#include "player_data.h"
#include "config_players.h"
#include "kfx_game_state.h"
#include "kfx_net_state.h"
#include "kfx_sim_state.h"
#include "kfx_config_state.h"
#include "kfx_render_state.h"
#include "ariadne_saved_state.h"
#include "config_settings.h"
#include "local_state.h"
#include "ports/ui_port.h"
#include "kfx_config/tests/scoped_port_override.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {

/** A hash of a large, mostly zero block: each non-zero 8-byte word with its offset (a zero 4 KB page is skipped
 *  fast). Zero words don't count, and neither does the size, so a zero-initialised field added at the end of a
 *  struct leaves the hashes as they were; a field inserted before others moves their offsets and changes them. */
uint64_t block_hash(const void *p, size_t n, uint64_t seed) {
    static const unsigned char zeros[4096] = {0};
    const unsigned char *b = static_cast<const unsigned char *>(p);
    uint64_t r = seed ^ 0x9e3779b97f4a7c15ULL;
    for (size_t off = 0; off < n; off += sizeof(zeros)) {
        const size_t len = (n - off < sizeof(zeros)) ? (n - off) : sizeof(zeros);
        if (std::memcmp(b + off, zeros, len) == 0)
            continue;
        for (size_t i = 0; i < len; i += 8) {
            uint64_t w = 0;
            std::memcpy(&w, b + off + i, (len - i < 8) ? (len - i) : 8);
            if (w != 0)
                r = (r ^ (off + i) ^ (w * 0x9e3779b97f4a7c15ULL)) * 0x100000001b3ULL;
        }
    }
    return r;
}

std::string ui_log;
void ui_targeted(char t, PlayerNumber p, PlayerNumber target, uint64_t timeout, const char *msg) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "T%d,%d,%d,%llu:", (int)t, (int)p, (int)target, (unsigned long long)timeout);
    ui_log += buf; ui_log += msg ? msg : "(null)"; ui_log += ";";
}
void ui_clear(char t, PlayerNumber p) { ui_log += "C" + std::to_string((int)t) + "," + std::to_string((int)p) + ";"; }
void ui_message(char t, int64_t idx, const char *msg) {
    ui_log += "M" + std::to_string((int)t) + "," + std::to_string((long long)idx) + ":" + (msg ? msg : "(null)") + ";";
}
TbBool ui_onscreen(int64_t turns, const char *msg) {
    ui_log += "O" + std::to_string((long long)turns) + ":" + (msg ? msg : "(null)") + ";";
    return true;
}

// What the other state held when the program started (the hashes were printed in a process of their own): a test
// that runs before this one may change it (the replay tests' loads change the configuration the actions read).
// Large: copied to the heap once, at start-up.
const struct KfxConfigState *const initial_config_state = new KfxConfigState(kfx_config_state);
const struct GameSettings initial_settings = settings;
const struct LocalState initial_local_state = local_state;
const struct KfxRenderState initial_render_state = kfx_render_state;
// The pathfinding state (what a save holds of it) on the bare level, taken in the parent.
std::vector<unsigned char> bare_pathfinding_state;

void bare_level() {
    std::memcpy(&kfx_config_state, initial_config_state, sizeof(kfx_config_state));
    std::memcpy(&settings, &initial_settings, sizeof(settings));
    std::memcpy(&local_state, &initial_local_state, sizeof(local_state));
    std::memcpy(&kfx_render_state, &initial_render_state, sizeof(kfx_render_state));
    std::memset(&kfx_game_state, 0, sizeof(kfx_game_state));
    std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
    std::memset(&kfx_net_state, 0, sizeof(kfx_net_state));
    kfx_config_state.neutral_player_num = PLAYER_NEUTRAL;
    kfx_sim_state.map_tiles_x = 85; kfx_sim_state.map_tiles_y = 85;
    kfx_sim_state.map_subtiles_x = 255; kfx_sim_state.map_subtiles_y = 255;
    for (PlayerNumber i = 0; i < PLAYERS_COUNT; i++)
        kfx_sim_state.players[i].id_number = i;
    kfx_sim_state.players[0].allocflags |= PlaF_Allocated;
    kfx_sim_state.players[0].view_type = PVT_DungeonTop;
    my_player_number = 0;
    ui_log.clear();
    bare_pathfinding_state.resize(ariadne_saved_state_size());
    ariadne_saved_state_write(bare_pathfinding_state.data());
}

/** KFX_GAME_GOLDEN_CUT=<start>:<len>:<stride>:<count>[,...]: kfx_sim_state hashed as if those byte ranges weren't
 *  in it (a layout change's check, as ftest_golden.h's KFX_FTEST_GOLDEN_CUT: the commit before it, with the removed
 *  fields cut, must print the hashes the new layout prints). */
uint64_t sim_state_hash(uint64_t seed) {
    const char *spec = std::getenv("KFX_GAME_GOLDEN_CUT");
    if (spec == nullptr)
        return block_hash(&kfx_sim_state, sizeof(kfx_sim_state), seed);
    std::vector<unsigned char> keep(sizeof(kfx_sim_state), 1);
    unsigned long long start, len, stride, count;
    int used = 0;
    while (std::sscanf(spec, "%llu:%llu:%llu:%llu%n", &start, &len, &stride, &count, &used) == 4) {
        for (unsigned long long n = 0; n < count; n++)
            std::memset(&keep[start + n * stride], 0, len);
        spec += used;
        if (*spec != ',')
            break;
        spec++;
    }
    const unsigned char *b = reinterpret_cast<const unsigned char *>(&kfx_sim_state);
    std::vector<unsigned char> compact;
    compact.reserve(sizeof(kfx_sim_state));
    for (size_t k = 0; k < sizeof(kfx_sim_state); k++)
        if (keep[k])
            compact.push_back(b[k]);
    return block_hash(compact.data(), compact.size(), seed);
}

uint64_t state_hash() {
    uint64_t h = sim_state_hash(1);
    h = block_hash(&kfx_game_state, sizeof(kfx_game_state), h);
    h = block_hash(&kfx_net_state, sizeof(kfx_net_state), h);
    h = block_hash(&kfx_render_state, sizeof(kfx_render_state), h);
    h = block_hash(&settings, sizeof(settings), h);
    h = block_hash(&local_state, sizeof(local_state), h);
    h = block_hash(ui_log.data(), ui_log.size(), h);
    // Only when a case changed it, so the cases that don't keep the hashes they had before it was hashed.
    std::vector<unsigned char> pathfinding(ariadne_saved_state_size());
    ariadne_saved_state_write(pathfinding.data());
    if (pathfinding != bare_pathfinding_state)
        h = block_hash(pathfinding.data(), pathfinding.size(), h);
    return h;
}

struct Case {
    std::string name;
    bool cursor;
    int64_t value, par1, par2;
    MapCoord x, y;
    uint32_t control_flags;
};

// In the child: the bare level is the parent's (set once), so the child copies only the pages it writes.
void run_case(const Case &c) {
    struct Packet *pckt = get_packet(0);
    pckt->control_flags = c.control_flags;
    pckt->pos_x = c.x; pckt->pos_y = c.y;
    if (c.cursor) {
        kfx_sim_state.players[0].work_state = c.value;
        const MapSubtlCoord stl_x = coord_subtile(c.x), stl_y = coord_subtile(c.y);
        packets_process_cheats(0, 0, c.x, c.y, pckt, stl_x, stl_y, subtile_slab(stl_x), subtile_slab(stl_y));
    } else {
        pckt->action = (unsigned char)c.value;
        pckt->actn_par1 = c.par1; pckt->actn_par2 = c.par2;
        process_user_packet(0);
    }
}

/** Runs one case in a child process; its hash, or 0 if the child died. */
uint64_t hash_in_child(const Case &c) {
    int fds[2];
    REQUIRE(pipe(fds) == 0);
    std::fflush(nullptr);
    const pid_t pid = fork();
    if (pid == 0) {
        close(fds[0]);
        alarm(20);
        run_case(c);
        const uint64_t h = state_hash();
        (void)!write(fds[1], &h, sizeof(h));
        _exit(0);
    }
    close(fds[1]);
    uint64_t h = 0;
    if (read(fds[0], &h, sizeof(h)) != (ssize_t)sizeof(h)) h = 0;
    close(fds[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    return h;
}

std::vector<Case> cases() {
    std::vector<Case> v;
    struct Params { int64_t p1, p2; MapCoord x, y; } params[] = {{0, 0, 0, 0}, {1, 1, 3000, 3000}, {5, 300, 12000, 9000}};
    for (int64_t a = 1; a <= PckA_EditorGoSpectator; a++) {
        if (a == PckA_SwitchView) continue;
        for (int pi = 0; pi < 3; pi++) {
            const Params &p = params[pi];
            v.push_back({"action" + std::to_string((long long)a) + "/" + std::to_string(pi), false, a, p.p1, p.p2, p.x, p.y, PCtr_MapCoordsValid});
        }
    }
    for (int64_t ws = 1; ws < PSt_ListEnd; ws++) {
        if ((ws == PSt_MkGoodCreatr) || (ws == PSt_MkBadCreatr) || (ws == PSt_PlaceTerrain)) continue;
        for (int click = 0; click < 2; click++)
            for (int at = 0; at < 2; at++) {
                const MapCoord x = at ? 12000 : 3000, y = at ? 9000 : 3000;
                const uint32_t flags = PCtr_MapCoordsValid | (click ? PCtr_LBtnRelease : 0);
                v.push_back({"cursor" + std::to_string((long long)ws) + (click ? "/click" : "/hover") + (at ? "/far" : "/near"),
                    true, ws, 0, 0, x, y, flags});
            }
    }
    return v;
}

} // namespace

TEST_CASE("golden: what each packet action and cheat cursor does to a bare level", "[kfx_game][packet_golden]")
{
    ScopedPortOverride<UiPort> ui{ui_port, set_ui_port};
    ui->targeted_message_add = ui_targeted;
    ui->clear_messages_from_player = ui_clear;
    ui->message_add = ui_message;
    ui->show_onscreen_msg = ui_onscreen;
    const std::map<std::string, uint64_t> expected = {
#include "packet_actions_golden.inc"
    };
    const bool print = std::getenv("KFX_GAME_GOLDEN_PRINT") != nullptr;
    int64_t checked = 0, died = 0;
    bare_level();
    for (const Case &c : cases()) {
        const uint64_t h = hash_in_child(c);
        died += (h == 0);
        if (print) {
            std::printf("    {\"%s\", 0x%016llxULL},\n", c.name.c_str(), (unsigned long long)h);
            continue;
        }
        auto it = expected.find(c.name);
        INFO(c.name);
        REQUIRE(it != expected.end());
        CHECK(it->second == h);
        checked++;
    }
    bare_level();
    CHECK(died == 0);
    if (!print)
        CHECK(checked == (int64_t)expected.size());
}
