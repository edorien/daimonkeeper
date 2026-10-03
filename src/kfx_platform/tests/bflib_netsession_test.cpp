// kfx_platform: bflib_netsession.c -- net_copy_name_string, and (below) the lobby metadata (upstream #5373).
// net_copy_name_string is
// function in this file, a pure bounded string copy with a
// memset-first-then-conditionally-copy shape.
#include <catch2/catch_test_macros.hpp>

#include "bflib_netsession.h"

#include <cstdio>
#include <cstring>
#include <string>

TEST_CASE("net_copy_name_string copies src into dst, null-terminated", "[kfx_platform][bflib_netsession]") {
    char dst[32];
    std::memset(dst, 0xAA, sizeof(dst));
    net_copy_name_string(dst, "hello", sizeof(dst));
    CHECK(std::strcmp(dst, "hello") == 0);
}

TEST_CASE("net_copy_name_string zero-fills the whole buffer first, clearing any trailing garbage", "[kfx_platform][bflib_netsession]") {
    char dst[16];
    std::memset(dst, 0xAA, sizeof(dst));
    net_copy_name_string(dst, "hi", sizeof(dst));
    CHECK(std::strcmp(dst, "hi") == 0);
    for (size_t i = 3; i < sizeof(dst); i++) {
        CHECK(dst[i] == 0);
    }
}

TEST_CASE("net_copy_name_string leaves dst zeroed when src is NULL", "[kfx_platform][bflib_netsession]") {
    char dst[16];
    std::memset(dst, 0xAA, sizeof(dst));
    net_copy_name_string(dst, nullptr, sizeof(dst));
    for (size_t i = 0; i < sizeof(dst); i++) {
        CHECK(dst[i] == 0);
    }
}

TEST_CASE("net_copy_name_string truncates a src longer than max_len", "[kfx_platform][bflib_netsession]") {
    char dst[6];
    net_copy_name_string(dst, "abcdefghij", sizeof(dst));
    CHECK(std::strlen(dst) == 5); // snprintf null-terminates within max_len
    CHECK(std::strcmp(dst, "abcde") == 0);
}

// Upstream #5373: a hosted lobby advertises its state as JSON metadata (version, phase, joinable,
// capacity, players); a listing reads it back, and says why a lobby can't be joined.
#include <json-dom.h>

namespace {
struct TbNetworkSessionNameEntry make_lobby() {
    struct TbNetworkSessionNameEntry session;
    std::memset(&session, 0, sizeof(session));
    session.phase = NetPhase_Lobby;
    session.joinable = 1;
    session.max_players = 4;
    std::snprintf(session.version, sizeof(session.version), "%s", NET_SESSION_VERSION);
    session.player_count = 2;
    std::snprintf(session.players[0], sizeof(session.players[0]), "%s", "Keeper \"One\"");
    std::snprintf(session.players[1], sizeof(session.players[1]), "%s", "Two");
    return session;
}
struct TbNetworkSessionNameEntry read_back(const char *metadata) {
    std::string json = std::string("{\"createdAt\":1234,") + metadata + "}";
    VALUE root;
    REQUIRE(json_dom_parse(json.c_str(), json.size(), NULL, 0, &root, NULL) == 0);
    struct TbNetworkSessionNameEntry parsed;
    std::memset(&parsed, 0, sizeof(parsed));
    net_session_parse_metadata(&parsed, &root);
    value_fini(&root);
    return parsed;
}
}

TEST_CASE("lobby metadata survives a JSON round trip, names escaped", "[kfx_platform][bflib_netsession]") {
    struct TbNetworkSessionNameEntry session = make_lobby();
    char metadata[SESSION_METADATA_MAX];
    REQUIRE(net_session_metadata_json(&session, metadata, sizeof(metadata)) > 0);
    struct TbNetworkSessionNameEntry parsed = read_back(metadata);
    CHECK(parsed.phase == NetPhase_Lobby);
    CHECK(parsed.joinable == 1);
    CHECK(parsed.max_players == 4);
    CHECK(parsed.created_at == 1234);
    CHECK(parsed.roster_known == 1);
    REQUIRE(parsed.player_count == 2);
    CHECK(std::strcmp(parsed.players[0], "Keeper \"One\"") == 0);
    CHECK(std::strcmp(parsed.version, NET_SESSION_VERSION) == 0);
}

TEST_CASE("a lobby is refused for another game or version, a started game, locked joining, or no room", "[kfx_platform][bflib_netsession]") {
    struct TbNetworkSessionNameEntry session = make_lobby();
    CHECK(net_session_join_rejection(&session) == NetJoin_Accepted);
    std::snprintf(session.version, sizeof(session.version), "%s", "1.4.0.5136"); // a KeeperFX lobby
    CHECK(net_session_incompatible(&session));
    CHECK(net_session_join_rejection(&session) == NetJoin_Version);
    session.version[0] = '\0'; // unknown: not held against it
    CHECK_FALSE(net_session_incompatible(&session));
    session.phase = NetPhase_InGame;
    CHECK(net_session_join_rejection(&session) == NetJoin_InGame);
    session.phase = NetPhase_Lobby;
    session.joinable = 0;
    CHECK(net_session_join_rejection(&session) == NetJoin_Locked);
    session.joinable = 1;
    session.roster_known = 1;
    session.player_count = 4;
    CHECK(net_session_join_rejection(&session) == NetJoin_Full);
}
