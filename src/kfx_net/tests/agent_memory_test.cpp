// kfx_net: the memory an External seat's agent keeps about its game, held by the engine and written into saves as
// the optional AGNT chunk (docs/refactor/AI/omissions/09-persistent-memory.md section 4). The chunk payload must
// round-trip exactly, and a malformed one must leave nothing behind rather than half a table.
#include <catch2/catch_test_macros.hpp>

#include "agent_memory.h"
#include "player_data.h"

#include <cstdlib>
#include <cstring>
#include <string>

namespace {
std::string get(PlayerNumber p)
{
    size_t len = 0;
    const char *d = agent_memory_get(p, &len);
    return d ? std::string(d, len) : std::string("<none>");
}

void put_u32(std::string &s, uint32_t v)
{
    for (int i = 0; i < 4; i++) s.push_back((char)((v >> (8 * i)) & 0xFF));
}
}

TEST_CASE("agent memory: set, get, replace and clear", "[agent_memory]")
{
    agent_memory_clear_all();
    CHECK(get(1) == "<none>");
    REQUIRE(agent_memory_set(1, "plan A", 6));
    CHECK(get(1) == "plan A");
    REQUIRE(agent_memory_set(1, "plan B", 6));
    CHECK(get(1) == "plan B");
    CHECK(get(2) == "<none>");
    REQUIRE(agent_memory_set(1, nullptr, 0));
    CHECK(get(1) == "<none>");
    REQUIRE(agent_memory_set(3, "x", 1));
    agent_memory_clear_all();
    CHECK(get(3) == "<none>");
}

TEST_CASE("agent memory: refuses a bad player or a text over the cap, keeping the old one", "[agent_memory]")
{
    agent_memory_clear_all();
    REQUIRE(agent_memory_set(0, "keep", 4));
    CHECK_FALSE(agent_memory_set(-1, "x", 1));
    CHECK_FALSE(agent_memory_set(PLAYERS_COUNT, "x", 1));
    std::string big(AGENT_MEMORY_MAX + 1, 'a');
    CHECK_FALSE(agent_memory_set(0, big.data(), big.size()));
    CHECK(get(0) == "keep");
    std::string max(AGENT_MEMORY_MAX, 'b');
    CHECK(agent_memory_set(0, max.data(), max.size()));
    CHECK(get(0).size() == (size_t)AGENT_MEMORY_MAX);
    agent_memory_clear_all();
}

TEST_CASE("agent memory: the chunk payload round-trips, embedded NULs included", "[agent_memory]")
{
    agent_memory_clear_all();
    char *buf = nullptr;
    CHECK(agent_memory_serialise(&buf) == 0);
    CHECK(buf == nullptr);

    const std::string with_nul("a\0b", 3);
    REQUIRE(agent_memory_set(0, with_nul.data(), with_nul.size()));
    REQUIRE(agent_memory_set(5, "{\"plan\":\"dig\"}", 14));
    const size_t len = agent_memory_serialise(&buf);
    REQUIRE(len == 4 + (8 + 3) + (8 + 14));
    agent_memory_clear_all();
    REQUIRE(agent_memory_deserialise(buf, len));
    std::free(buf);
    CHECK(get(0) == with_nul);
    CHECK(get(5) == "{\"plan\":\"dig\"}");
    CHECK(get(1) == "<none>");

    // A save without the chunk: everything goes.
    REQUIRE(agent_memory_deserialise(nullptr, 0));
    CHECK(get(0) == "<none>");
    CHECK(get(5) == "<none>");
}

TEST_CASE("agent memory: a malformed payload clears everything and reports it", "[agent_memory]")
{
    auto primed = []() { agent_memory_clear_all(); REQUIRE(agent_memory_set(2, "old", 3)); };
    std::string good;
    put_u32(good, 1); put_u32(good, 1); put_u32(good, 2); good += "hi";

    primed();
    REQUIRE(agent_memory_deserialise(good.data(), good.size()));
    CHECK(get(1) == "hi");
    CHECK(get(2) == "<none>");

    SECTION("a length past the end") {
        primed();
        std::string bad = good.substr(0, good.size() - 1);
        CHECK_FALSE(agent_memory_deserialise(bad.data(), bad.size()));
    }
    SECTION("a count past the end") {
        primed();
        std::string bad;
        put_u32(bad, 2); put_u32(bad, 1); put_u32(bad, 2); bad += "hi";
        CHECK_FALSE(agent_memory_deserialise(bad.data(), bad.size()));
    }
    SECTION("a player out of range") {
        primed();
        std::string bad;
        put_u32(bad, 1); put_u32(bad, PLAYERS_COUNT); put_u32(bad, 2); bad += "hi";
        CHECK_FALSE(agent_memory_deserialise(bad.data(), bad.size()));
    }
    SECTION("trailing bytes") {
        primed();
        std::string bad = good + "x";
        CHECK_FALSE(agent_memory_deserialise(bad.data(), bad.size()));
    }
    SECTION("shorter than a count") {
        primed();
        CHECK_FALSE(agent_memory_deserialise("ab", 2));
    }
    for (PlayerNumber p = 0; p < PLAYERS_COUNT; p++) CHECK(get(p) == "<none>");
}
