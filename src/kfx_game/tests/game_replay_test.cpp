// kfx_game: game_replay.c's restore_users_from_packet_save(). Moved with the
// code from kfx_net's net_game_drop_test.cpp in refactor pass 2 (S12,
// docs/refactor-pass2/stage-12-net-split.md), with that file's fixture.
#include <catch2/catch_test_macros.hpp>
#include "ports/ui_port.h"
#include "kfx_config/tests/scoped_port_override.h"

#include "game_replay.h"
#include "net_game.h"
#include "packets.h"
#include "player_data.h"
#include "player_utils.h"
#include "dungeon_data.h"
#include "thing_data.h"
#include "kfx_sim_state.h"
#include "kfx_net_state.h"
#include "kfx_config_state.h"

#include "config_keeperfx.h"
#include "game_saves.h"
#include "ports/sim_port.h"
#include "ports/file_path_port.h"
#include "file_path_port_impl.h"
#include "config_campaigns.h"
#include "sim_port_impl.h"
#include "save_catalogue.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <unistd.h>

namespace {
struct DropFixture {
    DropFixture() {
        std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
        std::memset(&kfx_net_state, 0, sizeof(kfx_net_state));
        kfx_config_state.neutral_player_num = PLAYER_NEUTRAL; // zeroed state would make player 0 the neutral one
        std::memset(net_user_info, 0, sizeof(net_user_info));
        local_system_flags |= GSF_NetworkActive;
        net_user_info[SERVER_ID].network_user_active = 1;
        setup_network_player_numbers(); // host -> player 0
        for (int64_t i = 0; i < PLAYERS_COUNT; i++) {
            kfx_sim_state.players[i].id_number = (PlayerNumber)i;
            kfx_sim_state.players[i].user_id = -1;
        }
        kfx_sim_state.players[0].user_id = SERVER_ID;
        make_player(0);
    }
    void make_player(int64_t i) {
        struct PlayerInfo *player = &kfx_sim_state.players[i];
        player->allocflags |= PlaF_Allocated;
        player->victory_state = VicS_Undecided;
        // a living heart: player_cannot_win() is false while the soul container exists
        struct Thing *heart = thing_get(10 + i);
        heart->index = (ThingIndex)(10 + i);
        heart->class_id = TCls_Object;
        heart->alloc_flags |= TAlF_Exists;
        get_dungeon(i)->dnheart_idx = (ThingIndex)(10 + i);
    }
    // A connected, human-driven opponent (user i controls player i).
    void make_human_enemy(int64_t i) {
        make_player(i);
        kfx_sim_state.players[i].user_id = i;
        net_user_info[i].network_user_active = 1;
        setup_network_player_numbers();
    }
};
}

// Upstream #5317 (multiplayer -packetload): a replay header records which player each
// network user controlled; restore_users_from_packet_save() rebuilds that user<->player
// mapping (and each user's name) so the replayed packets reach the right players.
TEST_CASE_METHOD(DropFixture, "restore_users_from_packet_save maps recorded users to their players", "[kfx_game][game_replay]") {
    std::memset(&replay.head, 0, sizeof(replay.head));
    std::memset(replay.head.user_players, -1, sizeof(replay.head.user_players));
    replay.head.players_exist = (1 << 0) | (1 << 2);
    replay.head.user_players[1] = 2; // user 1 drove player 2
    replay.head.user_players[0] = 0; // the host drove player 0
    std::snprintf(replay.head.user_names[1], sizeof(replay.head.user_names[1]), "Guest");
    my_player_number = 0;

    restore_users_from_packet_save();

    CHECK(get_net_user_player_number(1) == 2);
    CHECK(get_net_user_player_number(0) == 0);
    CHECK(kfx_sim_state.players[2].user_id == 1);
    CHECK(std::string(kfx_sim_state.players[2].player_name) == "Guest");
}

TEST_CASE_METHOD(DropFixture, "restore_users_from_packet_save ignores a user mapped to a player the file says did not exist", "[kfx_game][game_replay]") {
    std::memset(&replay.head, 0, sizeof(replay.head));
    std::memset(replay.head.user_players, -1, sizeof(replay.head.user_players));
    replay.head.players_exist = (1 << 0);
    replay.head.user_players[0] = 0;
    replay.head.user_players[3] = 4; // player 4 not in players_exist
    my_player_number = 0;

    restore_users_from_packet_save();

    CHECK(get_net_user_player_number(0) == 0);
    CHECK(get_net_user_player_number(3) == -1);
}

// Coverage-first for upstream #5359 (autosaved, compressed replays): whatever the
// .pck encoding is, recording turns with save_packets() and playing them back with
// load_packets_for_turn() must give back the exact packets, turn for turn, and a
// long turn's chat message must reach the right player again.
namespace {
// Loading a .pck header also loads the campaign it names (load_game_chunks()'s
// INFO chunk), so the runtime directory points at a scratch tree holding the
// original campaign file minus NAME_TEXT_ID (resolving that needs the language
// strings, which this binary never loads). There is no fxdata/ in it: the stats
// reload that follows then finds nothing to (re)load.
struct ReplayFile {
    std::string path;
    std::filesystem::path root;
    std::string prev_runtime_dir;
    ReplayFile() {
        namespace fs = std::filesystem;
        const fs::path repo = fs::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
        // per process: ctest runs each case as its own process, in parallel
        root = fs::temp_directory_path() / ("kfx_game_replay_test_" + std::to_string(getpid()));
        fs::remove_all(root);
        fs::create_directories(root);
        fs::create_directories(root / "campgns");
        std::ifstream orig(repo / "campgns" / "keeporig.cfg");
        std::ofstream copy(root / "campgns" / "keeporig.cfg");
        for (std::string line; std::getline(orig, line); )
            if (line.rfind("NAME_TEXT_ID", 0) != 0)
                copy << line << "\n";
        set_sim_port(&kfx_sim_port); // the stats reload clears kfx_sim's slab tables through it
        prev_runtime_dir = keeper_runtime_directory;
        REQUIRE(root.string().size() < sizeof(keeper_runtime_directory));
        std::snprintf(keeper_runtime_directory, sizeof(keeper_runtime_directory), "%s", root.string().c_str());
        path = (root / "roundtrip.pck").string();
        std::remove(path.c_str());
        REQUIRE(path.size() < sizeof(replay.fname));
        std::snprintf(replay.fname, sizeof(replay.fname), "%s", path.c_str());
    }
    ~ReplayFile() {
        close_packet_file();
        set_sim_port(nullptr);
        std::snprintf(keeper_runtime_directory, sizeof(keeper_runtime_directory), "%s", prev_runtime_dir.c_str());
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

// Mostly-zero packets with a few live fields, the way real input looks, plus the
// occasional fully random packet so every byte value goes through the encoding.
void fill_packet(struct Packet *pckt, uint32_t &rng, GameTurn turn) {
    auto next = [&rng]() { rng = rng * 1664525u + 1013904223u; return rng >> 8; };
    std::memset(pckt, 0, sizeof(*pckt));
    if (next() % 7 == 0) {
        unsigned char *b = reinterpret_cast<unsigned char *>(pckt);
        for (size_t i = 0; i < sizeof(*pckt); i++)
            b[i] = (unsigned char)next();
        pckt->action = PckA_None;
        return;
    }
    pckt->turn = turn;
    pckt->pos_x = (int32_t)(next() % 0x10000);
    pckt->pos_y = (int32_t)(next() % 0x10000);
    if (next() % 3 == 0) {
        pckt->action = PckA_TogglePause;
        pckt->actn_par1 = (int32_t)(next() % 50);
    }
    pckt->control_flags = next() % 4;
}
}

TEST_CASE_METHOD(DropFixture, "save_packets then load_packets_for_turn gives back every recorded turn", "[kfx_game][game_replay]") {
    make_human_enemy(1);
    ReplayFile file;
    replay.checksum_verify = false;
    REQUIRE(open_new_packet_file_for_save());

    const GameTurn turns = 300;
    std::vector<struct Packet> recorded;
    uint32_t rng = 12345;
    for (GameTurn t = 0; t < turns; t++) {
        for (NetUserId user = 0; user < 2; user++) {
            fill_packet(&sim_packets[user], rng, t);
            recorded.push_back(sim_packets[user]);
        }
        REQUIRE(save_packets());
    }
    close_packet_file();

    std::memset(sim_packets, 0, sizeof(sim_packets));
    struct CatalogueEntry centry;
    REQUIRE(open_packet_file_for_load(replay.fname, &centry));
    REQUIRE(replay.turns_stored == turns);
    // upstream #5359: recordings are compressed; mostly-zero input must come out well below raw size
    CHECK((replay.head.flags & PSHF_Compressed) != 0);
    const uint64_t raw_turn_data = turns * (2 * sizeof(struct Packet) + sizeof(TbBigChecksum));
    CHECK(std::filesystem::file_size(file.path) - replay.file_pos < raw_turn_data / 2);
    for (GameTurn t = 0; t < turns; t++) {
        load_packets_for_turn(t);
        for (NetUserId user = 0; user < 2; user++) {
            INFO("turn " << t << " user " << user);
            CHECK(std::memcmp(&sim_packets[user], &recorded[t * 2 + user], sizeof(struct Packet)) == 0);
        }
    }
}

// Upstream #5370: chat is recorded as it is processed (replay_record_chat_message(), a long
// turn record) rather than as a PckA_PlyrMsgEnd packet, and played back by showing it again.
namespace {
std::vector<std::string> shown_chat;
void record_shown_chat(char, int64_t, const char *msg) { shown_chat.push_back(msg); }
}

TEST_CASE_METHOD(DropFixture, "a recorded chat message is replayed on its own turn", "[kfx_game][game_replay]") {
    ScopedPortOverride<UiPort> ui{ui_port, set_ui_port};
    ui->message_add = record_shown_chat;
    shown_chat.clear();
    ReplayFile file;
    replay.checksum_verify = false;
    REQUIRE(open_new_packet_file_for_save());
    replay.save_enable = true; // records are only written while recording
    local_system_flags &= ~GSF_NetworkActive; // chat is only recorded outside network games

    const char *msg = "hello from turn 2";
    for (GameTurn t = 0; t < 4; t++) {
        std::memset(&sim_packets[0], 0, sizeof(struct Packet));
        sim_packets[0].turn = t;
        if (t == 2)
            replay_record_chat_message(0, msg, -1, -1);
        REQUIRE(save_packets());
    }
    close_packet_file();
    local_system_flags |= GSF_NetworkActive; // the user->player map is read back as recorded

    struct CatalogueEntry centry;
    REQUIRE(open_packet_file_for_load(replay.fname, &centry));
    REQUIRE(replay.turns_stored == 4);
    for (GameTurn t = 0; t < 4; t++) {
        load_packets_for_turn(t);
        CHECK(sim_packets[0].turn == t);
        CHECK(shown_chat.size() == ((t >= 2) ? 1u : 0u));
    }
    REQUIRE(shown_chat.size() == 1);
    CHECK(shown_chat[0] == msg);
}

// Upstream #5376: the header says whether a multiplayer game was recorded and holds every user's start
// settings, so playback gives each player its own (cheats, zoom, ...) rather than the viewer's.
TEST_CASE_METHOD(DropFixture, "the replay header records the game kind and each user's start settings", "[kfx_game][game_replay]") {
    ReplayFile file;
    replay.checksum_verify = false;
    kfx_sim_state.game_kind = GKind_MultiGame;
    kfx_sim_state.easter_eggs_enabled = true;
    REQUIRE(open_new_packet_file_for_save());
    REQUIRE(save_packets());
    close_packet_file();
    std::memset(&replay.head, 0, sizeof(replay.head));

    struct CatalogueEntry centry;
    REQUIRE(open_packet_file_for_load(replay.fname, &centry));
    CHECK((replay.head.flags & PSHF_MultiGame) != 0);
    CHECK((replay.head.user_start[SERVER_ID].flags & USF_CheatsEnabled) != 0);
    CHECK(replay.head.user_players[SERVER_ID] == 0);
}

// Upstream #5385: the header records the level files' checksums; playing a replay against a level that
// changed on disk is reported (the replay still plays).
TEST_CASE_METHOD(DropFixture, "a replay notices when the level on disk is not the one it was recorded on", "[kfx_game][game_replay]") {
    ReplayFile file;
    replay.checksum_verify = false;
    REQUIRE(open_new_packet_file_for_save());
    REQUIRE(save_packets());
    close_packet_file();
    struct CatalogueEntry centry;
    REQUIRE(open_packet_file_for_load(replay.fname, &centry));
    CHECK(verify_replay_map_checksums());
    replay.head.map_checksums[0] ^= 1;
    CHECK_FALSE(verify_replay_map_checksums());
}

// Autosaved replays (upstream #5359) are opt-in in this fork: AUTOSAVE_REPLAYS, off by default.
namespace {
struct AutosaveFixture : DropFixture {
    ReplayFile file; // scratch runtime dir, so replays/ lands in it
    TbBool prev_autosave = autosave_replays;
    uint64_t prev_max[ReplTyp_Count];
    AutosaveFixture() {
        std::memcpy(prev_max, max_replays, sizeof(prev_max));
        set_file_path_port(&kfx_config_file_path_port);
        std::memset(&campaign, 0, sizeof(campaign));
        replay.save_enable = false;
        replay.fname[0] = '\0';
    }
    ~AutosaveFixture() {
        autosave_replays = prev_autosave;
        std::memcpy(max_replays, prev_max, sizeof(prev_max));
        set_file_path_port(nullptr);
    }
};
}

TEST_CASE_METHOD(AutosaveFixture, "no replay is recorded while AUTOSAVE_REPLAYS is off", "[kfx_game][game_replay]") {
    autosave_replays = false;
    CHECK_FALSE(setup_auto_replay_save());
    CHECK_FALSE(replay.save_enable);
    CHECK(replay.fname[0] == '\0');
}

TEST_CASE_METHOD(AutosaveFixture, "with AUTOSAVE_REPLAYS on, a campaign level records to replays/campaign/", "[kfx_game][game_replay]") {
    autosave_replays = true;
    REQUIRE(setup_auto_replay_save());
    CHECK(replay.save_enable);
    const std::string fname = replay.fname;
    const std::string dir = (file.root / "replays" / "campaign").string() + "/";
    CHECK(fname.rfind(dir, 0) == 0);
    CHECK(fname.substr(dir.size() + 15, 3) == "_c1"); // timestamp, then kind and human count
    CHECK(fname.size() > 4);
    CHECK(fname.substr(fname.size() - 4) == ".pck");
}

TEST_CASE_METHOD(AutosaveFixture, "MAX_REPLAYS 0 for the kind records nothing", "[kfx_game][game_replay]") {
    autosave_replays = true;
    max_replays[ReplTyp_Campaign] = 0;
    CHECK_FALSE(setup_auto_replay_save());
    CHECK_FALSE(replay.save_enable);
}

TEST_CASE_METHOD(AutosaveFixture, "a new recording evicts the oldest ones of its kind beyond MAX_REPLAYS", "[kfx_game][game_replay]") {
    namespace fs = std::filesystem;
    autosave_replays = true;
    max_replays[ReplTyp_Campaign] = 2; // room for one old one plus the new one
    const fs::path dir = file.root / "replays" / "campaign";
    fs::create_directories(dir);
    for (const char *name : {"20200101T000000_c1_map00001_v100.pck", "20200102T000000_c1_map00001_v100.pck",
                             "20200103T000000_c1_map00001_v100.pck", "notes.pck"})
        std::ofstream(dir / name) << "x";

    REQUIRE(setup_auto_replay_save());

    CHECK_FALSE(fs::exists(dir / "20200101T000000_c1_map00001_v100.pck"));
    CHECK_FALSE(fs::exists(dir / "20200102T000000_c1_map00001_v100.pck"));
    CHECK(fs::exists(dir / "20200103T000000_c1_map00001_v100.pck"));
    CHECK(fs::exists(dir / "notes.pck")); // not a replay name: never touched
}
