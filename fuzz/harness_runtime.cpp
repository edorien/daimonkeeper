/** @file harness_runtime.cpp
 *     Fuzz targets for what reaches a running game from outside: saved Lua data, level scripts' Lua, the Lua API's
 *     arguments, the remote API's JSON, packets, replay files, sound files, sprite zips (security pass S08, README.md).
 */
#include "fuzz_driver.h"
#include "harness_common.h"

#include <string>
#include <vector>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "pre_inc.h"
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
#include "api.h"
#include "bflib_basics.h"
#include "bflib_fileio.h"
#include "config.h"
#include "config_campaigns.h"
#include "config_creature.h"
#include "config_keeperfx.h"
#include "custom_sprites.h"
#include "game_replay.h"
#include "game_saves.h"
#include "kfx_config_state.h"
#include "kfx_game_state.h"
#include "kfx_net_state.h"
#include "kfx_sim_state.h"
#include "config_creature.h"
#include "config_effects.h"
#include "config_magic.h"
#include "config_objects.h"
#include "config_players.h"
#include "config_powerhands.h"
#include "config_rules.h"
#include "config_terrain.h"
#include "config_trapdoor.h"
#include "lvl_script_commands.h"
#include "lvl_script_conditions.h"
#include "lvl_script_lib.h"
#include "map_locations.h"
#include "thing_list.h"
#include "lua_base.h"
#include "lua_sandbox.h"
#include "lvl_filesdk1.h"
#include "map_data.h"
#include "ariadne_update.h"
#include "packet_data.h"
#include "packet_validate.h"
#include "net_main.h"
#include "bflib_datetm.h"
#include "net_exchange_common.h"
#include "net_game.h"
#include "player_data.h"
#include "save_catalogue.h"
#include "compat_report.h"
#include "post_inc.h"

extern "C" TbBool custom_sound_load_wav_mem(const unsigned char *data, size_t size, const char *logical_name, int64_t sample_id);

namespace {

bool g_ready = false;

/** The base configuration and a Lua state, as a level load leaves them (once, in the parent). */
int init_runtime(void)
{
    if (g_ready)
        return 0;
    fuzz_runtime_dir();
    if (getenv("KFX_FUZZ_VERBOSE"))
        LbErrorLogSetup(fuzz_runtime_dir(), "fuzz.log", 1); // what the game logs, in <runtime>/fuzz.log
    memset(&kfx_config_state, 0, sizeof(kfx_config_state));
    memset(&campaign, 0, sizeof(campaign));
    load_stats_files();
    compat_report_clear();
    if (!open_lua_script(0))
    {
        fprintf(stderr, "cannot open the Lua environment (is KFX_FUZZ_DATA or the repo's config/ complete?)\n");
        return -1;
    }
    g_ready = true;
    return 0;
}

/* ---- Lua ---- */

int fuzz_lua_serialised(const uint8_t *data, size_t size)
{
    lua_set_serialised_data((const char *)data, size);
    size_t len = 0;
    lua_get_serialised_data(&len); // whatever loaded must save again
    return 0;
}

int seeds_lua_serialised(const char *dir)
{
    if (init_runtime() != 0)
        return 1;
    static const char *const kGames[] = {
        "Game = {}",
        "Game = {a = 1, b = 2.5, c = 'text', d = true, e = false}",
        "Game = {list = {1, 2, 3, 'four', {5, 6}}, [10] = 'sparse', nested = {x = {y = {z = 1}}}}",
        "Game = {big = 2^53, neg = -1, huge = 1e300, tiny = 1e-300, str = ('x'):rep(200)}",
        "function SeedHelper(x) return x end Game = {f = SeedHelper, g = print}",
        "Game = {players = {{score = 1, flags = {a = true}}, {score = 2, flags = {}}}, turn = 12345}",
    };
    int written = 0;
    for (size_t i = 0; i < sizeof(kGames) / sizeof(kGames[0]); i++)
    {
        if (luaL_dostring(Lvl_script, kGames[i]) != 0)
        {
            lua_pop(Lvl_script, 1);
            continue;
        }
        size_t len = 0;
        const char *blob = lua_get_serialised_data(&len);
        if (blob == NULL || len == 0)
            continue;
        char path[512];
        snprintf(path, sizeof(path), "%s/game%zu", dir, i);
        if (FILE *f = fopen(path, "wb"))
        {
            fwrite(blob, 1, len, f);
            fclose(f);
            written++;
        }
    }
    fprintf(stderr, "lua_serialised: %d seeds\n", written);
    return written > 0 ? 0 : 1;
}

int fuzz_lua_source(const uint8_t *data, size_t size)
{
    std::string code((const char *)data, size);
    lua_sandbox_new_turn(Lvl_script);
    lua_sandbox_dostring_script(Lvl_script, code.c_str());
    lua_sandbox_new_turn(Lvl_script);
    return 0;
}

std::vector<std::string> g_api_names;

/** The names the API accepts for creatures, rooms, slabs, powers, players...: arguments made of these get past the
 *  argument checks, so the code behind them is reached. */
std::vector<const struct NamedCommand *> g_vocab;
std::vector<std::string> g_vocab_words;

void collect_vocab(void)
{
    const struct NamedCommand *const tables[] = {
        creature_desc, room_desc, slab_desc, power_desc, spell_desc, shot_desc, trap_desc, door_desc, object_desc, special_desc,
        effect_desc, effectelem_desc, instance_desc, creaturejob_desc, player_desc, timer_desc, flag_desc, campaign_flag_desc,
        hero_objective_desc, tendency_desc, fill_desc, head_for_desc, class_commands, variable_desc, controls_variable_desc,
        msgtype_desc, spawn_type_desc, locked_desc, is_free_desc, hand_rule_desc, rule_action_desc, rule_slot_desc, research_desc,
        creature_select_criteria_desc, hit_type_desc, lenses_desc, comparison_desc, orientation_desc, script_boolean_desc,
        creature_deathkind_desc, powerhand_desc,
    };
    for (const struct NamedCommand *t : tables)
    {
        if (t == NULL)
            continue;
        g_vocab.push_back(t);
        for (const struct NamedCommand *c = t; c->name != NULL && g_vocab_words.size() < 20000; c++)
            g_vocab_words.push_back(c->name);
    }
    // places and the few things the location arguments take
    for (const char *w : { "PLAYER0", "PLAYER1", "PLAYER_GOOD", "PLAYER_NEUTRAL", "ALL_PLAYERS", "ACTION_POINT", "HERO_GATE", "LAST_EVENT" })
        g_vocab_words.push_back(w);
}

/** A real level loaded, if one is at hand (KFX_FUZZ_MAP_DIR; the Lua API works on the map, things and players). */
void load_level_for_api(void)
{
    const char *dir = getenv("KFX_FUZZ_MAP_DIR");
    std::string base = dir ? dir : KFX_FUZZ_REPO_ROOT "/core_files/campgns/ancntkpr";
    struct stat st;
    if (stat((base + "/map00001.slb").c_str(), &st) != 0)
        return;
    char rel[96];
    snprintf(rel, sizeof(rel), "lvlapi");
    std::string d = std::string(fuzz_runtime_dir()) + "/" + rel;
    mkdir(d.c_str(), 0755);
    static const char *const kExts[] = { "slb", "clm", "dat", "apt", "lgt", "own", "tng", "wib", "wlb", "flg", "inf", "txt" };
    for (const char *e : kExts)
    {
        std::string from = base + "/map00001." + e;
        std::string to = d + "/map00001." + e;
        if (access(from.c_str(), R_OK) == 0 && access(to.c_str(), F_OK) != 0)
            if (symlink(from.c_str(), to.c_str()) != 0) {}
    }
    snprintf(campaign.levels_location, sizeof(campaign.levels_location), "%s", rel);
    snprintf(campaign.fname, sizeof(campaign.fname), "fuzz.cfg");
    set_selected_level_number(1);
    init_map_size(1);
    if (load_map_file(1))
        init_navigation();
}

int init_lua_api(void)
{
    if (init_runtime() != 0)
        return -1;
    load_level_for_api();
    lua_State *L = Lvl_script;
    lua_pushnil(L);
    while (lua_next(L, LUA_GLOBALSINDEX) != 0)
    {
        if (lua_type(L, -2) == LUA_TSTRING && lua_iscfunction(L, -1))
            g_api_names.push_back(lua_tostring(L, -2));
        lua_pop(L, 1);
    }
    // not part of the script's API: LuaJIT's own base functions are also C functions
    static const char *const kBase[] = { "print", "pcall", "xpcall", "error", "assert", "select", "type", "tostring", "tonumber", "next", "pairs", "ipairs",
        "rawget", "rawset", "rawequal", "setmetatable", "getmetatable", "unpack", "collectgarbage", "load", "loadstring", "require", "dofile", "loadfile",
        "getfenv", "setfenv", "gcinfo", "newproxy", "module", "tostring" };
    std::vector<std::string> kept;
    for (const std::string &n : g_api_names)
    {
        bool base = false;
        for (const char *b : kBase)
            if (n == b)
                base = true;
        if (!base)
            kept.push_back(n);
    }
    g_api_names.swap(kept);
    if (getenv("KFX_FUZZ_VERBOSE"))
        for (const std::string &n : g_api_names)
            fprintf(stderr, "lua api: %s\n", n.c_str());
    collect_vocab();
    fprintf(stderr, "lua_api: %zu functions, %zu words\n", g_api_names.size(), g_vocab_words.size());
    return g_api_names.empty() ? -1 : 0;
}

/** The input is a list of calls: function, argument count, then each argument as a type byte and its value. */
int fuzz_lua_api(const uint8_t *data, size_t size)
{
    lua_State *L = Lvl_script;
    size_t pos = 0;
    auto next = [&](void) -> uint8_t { return pos < size ? data[pos++] : 0; };
    lua_newtable(L);
    const int pool_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    for (int call = 0; call < 24 && pos < size; call++)
    {
        const std::string &name = g_api_names[next() % g_api_names.size()];
        const int nargs = next() % 7;
        lua_getglobal(L, name.c_str());
        if (!lua_isfunction(L, -1))
        {
            lua_pop(L, 1);
            continue;
        }
        for (int a = 0; a < nargs; a++)
        {
            switch (next() % 14)
            {
            case 0: lua_pushnil(L); break;
            case 1: lua_pushboolean(L, 1); break;
            case 2: lua_pushboolean(L, 0); break;
            case 3: { int32_t v = (int32_t)(next() | next() << 8 | next() << 16 | (uint32_t)next() << 24); lua_pushinteger(L, v); break; }
            case 4: { int64_t v = 0; for (int i = 0; i < 8; i++) v |= (int64_t)next() << (8 * i); lua_pushnumber(L, (lua_Number)v); break; }
            case 5: { uint64_t v = 0; for (int i = 0; i < 8; i++) v |= (uint64_t)next() << (8 * i); double d; memcpy(&d, &v, sizeof(d)); lua_pushnumber(L, d); break; }
            case 6: { int n = next() % 48; std::string s; for (int i = 0; i < n; i++) s.push_back((char)next()); lua_pushlstring(L, s.data(), s.size()); break; }
            case 7: lua_newtable(L); break;
            case 8: lua_newtable(L); lua_pushinteger(L, 1); lua_rawseti(L, -2, 1); lua_pushinteger(L, next()); lua_rawseti(L, -2, 2); lua_pushstring(L, "x"); lua_rawseti(L, -2, 3); break;
            case 9: case 10: case 11: // a name the API knows (two thirds of the string-ish arguments)
            {
                const size_t w = (size_t)(next() | next() << 8) % g_vocab_words.size();
                lua_pushstring(L, g_vocab_words[w].c_str());
                break;
            }
            case 12: // a number one of the name tables knows
            {
                const struct NamedCommand *t = g_vocab[next() % g_vocab.size()];
                size_t n = 0;
                while (t[n].name != NULL) n++;
                lua_pushinteger(L, n ? t[next() % n].num : 0);
                break;
            }
            case 13: // something an earlier call returned: a thing, a room, a slab, a player
            {
                lua_rawgeti(L, LUA_REGISTRYINDEX, pool_ref);
                const int n = (int)lua_objlen(L, -1);
                if (n > 0)
                    lua_rawgeti(L, -1, 1 + next() % n);
                else
                    lua_pushnil(L);
                lua_remove(L, -2);
                break;
            }
            default: lua_pushinteger(L, (lua_Integer)(int8_t)next()); break; // a small signed number: indexes
            }
        }
        lua_sandbox_new_turn(L);
        const int base = lua_gettop(L) - nargs - 1;
        if (lua_pcall(L, nargs, LUA_MULTRET, 0) == 0)
        {
            // keep what came back for later calls
            lua_rawgeti(L, LUA_REGISTRYINDEX, pool_ref);
            int pool_n = (int)lua_objlen(L, -1);
            const int results = lua_gettop(L) - 1 - base;
            for (int r = 1; r <= results && r <= 3; r++)
            {
                if (lua_istable(L, base + r))
                {
                    lua_pushvalue(L, base + r);
                    lua_rawseti(L, -2, (pool_n < 32) ? ++pool_n : 1 + (int)(next() % 32));
                }
            }
        }
        lua_settop(L, 0);
    }
    return 0;
}

/* ---- the remote API ---- */

int init_api(void)
{
    return init_runtime();
}

int fuzz_api_json(const uint8_t *data, size_t size)
{
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0)
        return 0;
    fcntl(sv[0], F_SETFL, O_NONBLOCK);
    fcntl(sv[1], F_SETFL, O_NONBLOCK);
    kfx_sim_state.game_kind = GKind_LocalGame;
    api_process_message_for_test((const char *)data, size, sv[0]);
    close(sv[0]);
    close(sv[1]);
    return 0;
}

int seeds_api(const char *dir)
{
    static const char *const kMessages[] = {
        "{\"action\":\"hello\"}", "{\"action\":\"get_kfx_info\"}", "{\"action\":\"get_seats\",\"ack\":1}",
        "{\"action\":\"subscribe_event\",\"event\":\"GAME_STARTED\",\"ack\":\"a\"}",
        "{\"action\":\"console_command\",\"command\":\"say hello\",\"ack\":2}",
        "{\"action\":\"map_command\",\"command\":\"ADD_CREATURE_TO_POOL(IMP,5)\"}",
        "{\"action\":\"set_game_speed\",\"speed\":20}", "{\"action\":\"claim_seat\",\"player\":0}",
        "{\"action\":\"set_pause\",\"paused\":true}", "{\"action\":\"get_player_view\",\"player\":\"PLAYER0\"}",
        "{\"action\":\"get_log_tail\",\"lines\":50}", "{\"action\":\"subscribe_var\",\"var\":\"MONEY\",\"player\":1}",
        "{\"action\":\"unsubscribe_all\"}", "{\"action\":\"nonsense\"}", "[1,2,3]", "{}", "{\"action\":5}",
    };
    int written = 0;
    for (size_t i = 0; i < sizeof(kMessages) / sizeof(kMessages[0]); i++)
    {
        char path[512];
        snprintf(path, sizeof(path), "%s/m%02zu", dir, i);
        if (FILE *f = fopen(path, "wb"))
        {
            fputs(kMessages[i], f);
            fclose(f);
            written++;
        }
    }
    return 0;
}

/* ---- packets ---- */

int fuzz_packet(const uint8_t *data, size_t size)
{
    struct Packet pckt;
    memset(&pckt, 0, sizeof(pckt));
    memcpy(&pckt, data, size < sizeof(pckt) ? size : sizeof(pckt));
    struct PacketLimits lim;
    packet_limits_current(&lim);
    int bad = 0;
    for (int cheats = 0; cheats < 2; cheats++)
        for (int host = 0; host < 2; host++)
            packet_check(&lim, &pckt, cheats, host, &bad);
    packet_validate_for_user((NetUserId)(data[size ? 0 : 0] % MAX_NET_USERS), &pckt);
    return 0;
}

int seeds_packet(const char *dir)
{
    struct Packet p;
    int written = 0;
    for (int action = 0; action < 256; action += 3)
    {
        memset(&p, 0, sizeof(p));
        p.action = (uint8_t)action;
        p.actn_par1 = action % 7;
        p.actn_par2 = action % 5;
        p.pos_x = 256 * 20;
        p.pos_y = 256 * 20;
        char path[512];
        snprintf(path, sizeof(path), "%s/a%03d", dir, action);
        if (FILE *f = fopen(path, "wb"))
        {
            fwrite(&p, 1, sizeof(p), f);
            fclose(f);
            written++;
        }
    }
    return 0;
}

/* ---- replay ---- */

/** A recorded replay: its header chunk, where the turns begin, and the turns. Four, made when the target starts, for one
 *  and two users, with and without checksums. The input is the turns (after a byte that picks the header): opening a whole
 *  file loads its campaign and every configuration, which is slow and is not what a hostile turn stream attacks. */
struct ReplayBase {
    struct PacketSaveHead head;
    uint64_t offset;
    std::vector<uint8_t> tail;
};
std::vector<ReplayBase> g_replay_bases;

int record_replay_bases(void)
{
    if (!g_replay_bases.empty())
        return 0;
    if (init_runtime() != 0)
        return -1;
    for (int variant = 0; variant < 4; variant++)
    {
        memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
        memset(&kfx_net_state, 0, sizeof(kfx_net_state));
        memset(&replay, 0, sizeof(replay));
        kfx_config_state.neutral_player_num = PLAYER_NEUTRAL;
        const int users = 1 + variant % 2;
        local_system_flags |= GSF_NetworkActive;
        memset(net_user_info, 0, sizeof(net_user_info));
        for (int u = 0; u < users; u++)
            net_user_info[u].network_user_active = 1;
        for (int64_t i = 0; i < PLAYERS_COUNT; i++)
        {
            kfx_sim_state.players[i].id_number = (PlayerNumber)i;
            kfx_sim_state.players[i].user_id = -1;
        }
        for (int u = 0; u < users; u++)
        {
            kfx_sim_state.players[u].user_id = u;
            kfx_sim_state.players[u].allocflags |= PlaF_Allocated;
        }
        setup_network_player_numbers();
        const std::string path = std::string(fuzz_runtime_dir()) + "/base" + std::to_string(variant) + ".pck";
        snprintf(replay.fname, sizeof(replay.fname), "%s", path.c_str());
        replay.checksum_verify = (variant >= 2);
        if (!open_new_packet_file_for_save())
            return -1;
        ReplayBase base;
        base.offset = (uint64_t)LbFilePosition(replay.fp);
        base.head = replay.head;
        for (GameTurn t = 0; t < 30 + variant * 20; t++)
        {
            for (int u = 0; u < users; u++)
            {
                struct Packet *p = &sim_packets[u];
                memset(p, 0, sizeof(*p));
                p->turn = t;
                p->action = (uint8_t)((t * 7 + u * 3 + variant) % 40);
                p->actn_par1 = (int32_t)(t % 11);
                p->actn_par2 = (int32_t)((t * 5) % 13);
                p->pos_x = (int32_t)(256 * (10 + t % 40));
                p->pos_y = (int32_t)(256 * (10 + (t * 3) % 40));
                p->control_flags = (uint32_t)((t % 5) == 0 ? 3 : 0);
            }
            kfx_sim_state.play_gameturn = t;
            save_packets();
        }
        close_packet_file();
        if (FILE *f = fopen(path.c_str(), "rb"))
        {
            std::vector<uint8_t> all;
            uint8_t buf[4096];
            size_t got;
            while ((got = fread(buf, 1, sizeof(buf), f)) > 0)
                all.insert(all.end(), buf, buf + got);
            fclose(f);
            if (all.size() > base.offset)
                base.tail.assign(all.begin() + (std::ptrdiff_t)base.offset, all.end());
        }
        g_replay_bases.push_back(base);
    }
    memset(&replay, 0, sizeof(replay));
    // (recording needed users; playing a replay back is not a network game)
    local_system_flags &= ~GSF_NetworkActive;
    return 0;
}

int fuzz_replay_file(const uint8_t *data, size_t size)
{
    if (size < 1 || g_replay_bases.empty())
        return 0;
    const ReplayBase &base = g_replay_bases[data[0] % g_replay_bases.size()];
    char rel[96];
    fuzz_scratch_dir(rel, sizeof(rel), "rpl");
    std::vector<uint8_t> file(base.offset, 0);
    file.insert(file.end(), data + 1, data + size);
    fuzz_write_file(rel, "r.pck", file.data(), file.size());
    const std::string path = std::string(fuzz_runtime_dir()) + "/" + rel + "/r.pck";
    const int64_t turns = replay_open_turns_for_test(path.c_str(), base.offset, &base.head);
    if (getenv("KFX_FUZZ_VERBOSE"))
        fprintf(stderr, "replay_file: turns=%lld\n", (long long)turns);
    if (turns >= 0)
    {
        for (GameTurn t = 0; t < 40; t++)
            load_packets_for_turn(t);
        close_packet_file();
    }
    fuzz_scratch_clean(rel);
    return 0;
}

int seeds_replay(const char *dir)
{
    if (record_replay_bases() != 0)
        return 1;
    int written = 0;
    for (size_t i = 0; i < g_replay_bases.size(); i++)
    {
        char path[512];
        snprintf(path, sizeof(path), "%s/replay%zu", dir, i);
        if (FILE *f = fopen(path, "wb"))
        {
            const uint8_t sel = (uint8_t)i;
            fwrite(&sel, 1, 1, f);
            fwrite(g_replay_bases[i].tail.data(), 1, g_replay_bases[i].tail.size(), f);
            fclose(f);
            written++;
        }
    }
    fprintf(stderr, "replay_file: %d seeds\n", written);
    return written > 0 ? 0 : 1;
}

/* ---- network messages ---- */

const char *g_net_msg;
size_t g_net_msg_size;
size_t fake_sp_peek(NetUserId, const char **buffer)
{
    *buffer = g_net_msg;
    return g_net_msg_size;
}
size_t fake_sp_read(NetUserId, char *buffer, size_t max_size)
{
    const size_t n = g_net_msg_size < max_size ? g_net_msg_size : max_size;
    memcpy(buffer, g_net_msg, n);
    return n;
}
void fake_sp_send(NetUserId, const char *, size_t) {}
void fake_sp_send_all(const char *, size_t) {}
void fake_sp_drop(NetUserId, enum NetJoinRejection) {}
const struct NetSP kFakeSp = {
    NULL, NULL, NULL, NULL, NULL, fake_sp_send, fake_sp_send, fake_sp_send_all, NULL, fake_sp_peek, fake_sp_read, fake_sp_drop,
};

/** A message from a peer, as the network layer reads it: byte 0 sets who we are and how far each user got, byte 1 is
 *  who sent it, byte 2 the kind of message the caller expects, the rest is the message. */
int fuzz_net_message(const uint8_t *data, size_t size)
{
    if (size < 4)
        return 0;
    netstate.sp = &kFakeSp;
    netstate.max_users = MAX_NET_USERS;
    netstate.resync_pending = false;
    memset(netstate.msg_buffer, 0, sizeof(netstate.msg_buffer));
    netstate.my_id = (data[0] & 1) ? SERVER_ID : 1;
    for (int u = 0; u < MAX_NET_USERS; u++)
    {
        netstate.users[u].id = u;
        netstate.users[u].progress = (enum NetUserProgress)(((data[0] >> (1 + 2 * u)) & 3) % 3);
        snprintf(netstate.users[u].name, sizeof(netstate.users[u].name), "user%d", u);
    }
    netstate.phase = (enum NetSessionPhase)((data[0] >> 6) & 3);
    local_system_flags |= GSF_NetworkActive;
    const NetUserId source = data[1] % MAX_NET_USERS;
    const enum NetMessageType expected = (enum NetMessageType)(data[2] % 12);
    static const size_t kFrameSizes[] = { sizeof(struct Packet), sizeof(struct Packet), 64, 1, 4096 };
    const size_t frame_size = kFrameSizes[data[3] % (sizeof(kFrameSizes) / sizeof(kFrameSizes[0]))];
    std::vector<char> frames(frame_size * MAX_NET_USERS + 64, 0);
    g_net_msg = (const char *)data + 4;
    g_net_msg_size = size - 4;
    NetUserId peer = 0;
    process_network_message(source, frames.data(), frame_size, expected, &peer);
    return 0;
}

struct TbNetworkUserInfo g_net_user_info[MAX_NET_USERS];

int init_net(void)
{
    if (init_runtime() != 0)
        return -1;
    LbTimerInit();
    // the network layer's own set-up (it keeps a pointer to the table of users); a message is then read through a fake
    // transport (kFakeSp), so no socket is opened
    struct ServiceInitData sid;
    memset(&sid, 0, sizeof(sid));
    LbNetwork_Init(NS_ENET_UDP, MAX_NET_USERS, g_net_user_info, &sid);
    return 0;
}

/** One message of every kind, from a few different senders, with bodies of zeros, ones and a name-like text. */
int seeds_net(const char *dir)
{
    int written = 0;
    for (int type = 0; type < 12; type++)
        for (int variant = 0; variant < 3; variant++)
        {
            std::vector<uint8_t> m;
            m.push_back((uint8_t)(variant == 0 ? 0xFF : variant == 1 ? 0x55 : 0xAA)); // who we are, how far the users got
            m.push_back((uint8_t)(variant % 2)); // source
            m.push_back((uint8_t)type);          // the kind expected
            m.push_back((uint8_t)variant);       // frame size
            m.push_back((uint8_t)type);          // the message's kind
            if (variant == 0)
                m.insert(m.end(), 40, 0);
            else if (variant == 1)
                m.insert(m.end(), 40, 1);
            else
                for (const char *c = "\x01\x00\x00\x00\x00\x00\x00\x00guest-player\0password\0"; *c; c++)
                    m.push_back((uint8_t)*c);
            char path[512];
            snprintf(path, sizeof(path), "%s/t%02d-%d", dir, type, variant);
            if (FILE *f = fopen(path, "wb"))
            {
                fwrite(m.data(), 1, m.size(), f);
                fclose(f);
                written++;
            }
        }
    fprintf(stderr, "net_message: %d seeds\n", written);
    return 0;
}

/* ---- sound and sprites ---- */

int fuzz_audio_buffer(const uint8_t *data, size_t size)
{
    custom_sound_load_wav_mem(data, size, "fuzz", 0);
    return 0;
}

int fuzz_sprite_zip(const uint8_t *data, size_t size)
{
    char rel[96];
    fuzz_scratch_dir(rel, sizeof(rel), "zip");
    fuzz_write_file(rel, "map00001.zip", data, size);
    snprintf(campaign.levels_location, sizeof(campaign.levels_location), "%s", rel);
    set_selected_level_number(1);
    load_sprites_for_multi_front(1);
    fuzz_scratch_clean(rel);
    return 0;
}

} // namespace

static const char *const kLuaDict[] = { "function", "end", "local", "return", "for", "while", "repeat", "until", "string.rep(", "table.concat(", "setmetatable(",
    "getmetatable(", "load(", "loadstring(", "require(", "_G", "debug", "os", "io", "string.dump(", "coroutine.wrap(", "goto ", "::a::", "0x7fffffff", "1e999",
    "..", "[[", "]]", "[=[", "]=]", NULL };
static const char *const kApiDict[] = { "\"action\"", "\"ack\"", "\"player\"", "\"command\"", "\"token\"", "{", "}", "[", "]", ":", ",", "\"\\u0000\"", "\"\\ud800\"",
    "9223372036854775807", "-1", "1e400", "null", "true", NULL };

FUZZ_TARGET(target_lua_serialised, "lua_serialised", "the Lua game data of a save or a resync (binser): lua_set_serialised_data()",
    init_runtime, fuzz_lua_serialised, 8192, NULL, seeds_lua_serialised)
FUZZ_TARGET(target_lua_source, "lua_source", "Lua source run in the level script's sandbox, within its instruction and memory budget",
    init_runtime, fuzz_lua_source, 4096, kLuaDict, NULL)
FUZZ_TARGET(target_lua_api, "lua_api", "calls of the Lua API's C functions with arguments of every type (a list of calls: function, count, typed arguments)",
    init_lua_api, fuzz_lua_api, 1024, NULL, NULL)
FUZZ_TARGET(target_api_json, "api_json", "a message to the remote API (JSON), as api_process_buffer() gets it",
    init_api, fuzz_api_json, 4096, kApiDict, seeds_api)
FUZZ_TARGET(target_packet, "packet", "a game packet: the validator every received packet passes",
    init_runtime, fuzz_packet, 128, NULL, seeds_packet)
FUZZ_TARGET(target_replay_file, "replay_file", "a replay (.pck) file: the chunks, then 40 turns through the packet codec",
    record_replay_bases, fuzz_replay_file, 65536, NULL, seeds_replay)
FUZZ_TARGET(target_net_message, "net_message", "a message from a peer (login, user update, frames, chat, repair, turn sync...): process_network_message()",
    init_net, fuzz_net_message, 2048, NULL, seeds_net)
FUZZ_TARGET(target_audio_buffer, "audio_buffer", "a sound file (WAV, OGG, FLAC, MP3) as a campaign's custom sound",
    init_runtime, fuzz_audio_buffer, 65536, NULL, NULL)
FUZZ_TARGET(target_sprite_zip, "sprite_zip", "a sprite/ensign/icon pack (zip with PNGs and JSON) as a map's own map<N>.zip",
    init_runtime, fuzz_sprite_zip, 65536, NULL, NULL)
