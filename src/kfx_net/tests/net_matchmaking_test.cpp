// kfx_net: net_matchmaking.c's server selection. Only matchmaking_set_server()
// and the enabled/URL globals are exercised -- pure string handling, no curl,
// threads or sockets. One TEST_CASE, in order: the first CHECKs read the
// static initial values, before anything here mutates them (nothing else in
// kfx_net_utest touches matchmaking state).
#include <catch2/catch_test_macros.hpp>

#include "net_matchmaking.h"

#include <string>

TEST_CASE("matchmaking is off by default and never targets upstream KeeperFX's server", "[kfx_net][net_matchmaking]") {
    // Built-in default: disabled, no server.
    CHECK_FALSE(matchmaking_enabled);
    CHECK(std::string(matchmaking_ws_url).empty());
    CHECK(std::string(matchmaking_ip_url).empty());

    // A user-chosen server is accepted; scheme and trailing slash are stripped.
    matchmaking_enabled = true; // what config_keeperfx.c does before set_server
    matchmaking_set_server("https://lobby.example.org/");
    CHECK(matchmaking_enabled);
    CHECK(std::string(matchmaking_ws_url) == "wss://lobby.example.org/ws");
    CHECK(std::string(matchmaking_ip_url) == "https://lobby.example.org/ip");

    // Upstream's server is refused in any spelling a copied keeperfx.cfg might use.
    for (const char *host : { "matchmaking.keeperfx.workers.dev",
                              "https://matchmaking.keeperfx.workers.dev/",
                              "MatchMaking.KeeperFX.workers.dev" }) {
        INFO(host);
        matchmaking_enabled = true;
        matchmaking_set_server(host);
        CHECK_FALSE(matchmaking_enabled);
        CHECK(std::string(matchmaking_ws_url).empty());
        CHECK(std::string(matchmaking_ip_url).empty());
    }

    // "OFF"/empty -> disabled.
    matchmaking_enabled = true;
    matchmaking_set_server("");
    CHECK_FALSE(matchmaking_enabled);
}

// The transfers matchmaking makes may only use https/wss, whatever URL ends up on a handle and whatever a server
// answers: nothing here touches the network (an unsupported protocol is refused before any connection is tried).
#include <curl/curl.h>

TEST_CASE("a matchmaking handle refuses every protocol but https and wss", "[kfx_net][net_matchmaking]") {
    for (const char *url : { "http://127.0.0.1:9/ip", "ftp://127.0.0.1:9/x", "file:///etc/hostname",
                             "gopher://127.0.0.1:9/", "ws://127.0.0.1:9/ws", "dict://127.0.0.1:9/d:x" }) {
        INFO(url);
        CURL *handle = curl_easy_init();
        REQUIRE(handle != nullptr);
        REQUIRE(matchmaking_secure_handle(handle));
        curl_easy_setopt(handle, CURLOPT_URL, url);
        curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT_MS, 300L);
        curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
        CHECK(curl_easy_perform(handle) == CURLE_UNSUPPORTED_PROTOCOL);
        curl_easy_cleanup(handle);
    }
    // https is allowed: the transfer gets as far as trying to connect (to a port nobody listens on) and fails there.
    CURL *handle = curl_easy_init();
    REQUIRE(handle != nullptr);
    REQUIRE(matchmaking_secure_handle(handle));
    curl_easy_setopt(handle, CURLOPT_URL, "https://127.0.0.1:9/ip");
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT_MS, 300L);
    curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
    CHECK(curl_easy_perform(handle) != CURLE_UNSUPPORTED_PROTOCOL);
    curl_easy_cleanup(handle);
}
