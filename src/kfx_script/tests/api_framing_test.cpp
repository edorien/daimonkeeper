// kfx_script: finding whole JSON messages in an API client's byte stream (api_framing.h). One message can arrive over
// several reads and one read can hold several; braces inside strings (a plan that mentions "{", an agent memory that
// is itself JSON in a string) must not end a message early.
#include <catch2/catch_test_macros.hpp>

#include "api_framing.h"

#include <cstring>
#include <string>

namespace {
std::string next(const std::string &buf, size_t *consumed = nullptr, bool *complete = nullptr)
{
    size_t s = 0, e = 0;
    const int ok = api_frame_next(buf.data(), buf.size(), &s, &e);
    if (complete) *complete = (ok != 0);
    if (consumed) *consumed = ok ? e : s;
    return ok ? buf.substr(s, e - s) : std::string();
}
}

TEST_CASE("api framing: one object, and several in one read", "[api_framing]")
{
    size_t used = 0;
    CHECK(next("{\"a\":1}\n", &used) == "{\"a\":1}");
    CHECK(used == 7);
    std::string two = "{\"a\":1}\n{\"b\":{\"c\":2}}\n";
    CHECK(next(two, &used) == "{\"a\":1}");
    CHECK(next(two.substr(used)) == "{\"b\":{\"c\":2}}");
}

TEST_CASE("api framing: an unfinished object waits for more bytes", "[api_framing]")
{
    bool complete = true;
    size_t keep_from = 0;
    CHECK(next("junk\n{\"a\":{\"b\":", &keep_from, &complete).empty());
    CHECK_FALSE(complete);
    CHECK(keep_from == 5);        // the junk before the object can go
    CHECK(next("\n\n", &keep_from, &complete).empty());
    CHECK_FALSE(complete);
    CHECK(keep_from == 2);        // nothing but newlines: all of it can go
}

TEST_CASE("api framing: braces and quotes inside strings do not count", "[api_framing]")
{
    const std::string msg = "{\"data\":\"{\\\"plan\\\":\\\"} dig {\\\"}\",\"x\":\"a\\\\\"}";
    CHECK(next(msg + "{\"next\":1}") == msg);
    bool complete = true;
    next("{\"data\":\"}\"", nullptr, &complete);
    CHECK_FALSE(complete);        // the string's brace did not close it
}

TEST_CASE("api framing: a message far bigger than one read", "[api_framing]")
{
    std::string big = "{\"action\":\"set_agent_memory\",\"data\":\"" + std::string(70000, 'x') + "\"}";
    bool complete = false;
    CHECK(next(big.substr(0, 4095), nullptr, &complete).empty());
    CHECK_FALSE(complete);
    CHECK(next(big) == big);
}

// Security pass S02 (docs/security-pass/README.md): what the API server refuses before any message is run.
TEST_CASE("api: a stream that starts like HTTP (or anything but a JSON object) is not an API client", "[api_framing][security]")
{
    auto looks = [](const std::string &s) { return api_stream_looks_like_api(s.data(), s.size()); };
    CHECK(looks("{\"action\":\"hello\"}") == 1);
    CHECK(looks("  \r\n\t{\"a\":1}") == 1);            // whitespace first is fine
    CHECK(looks("{") == 1);
    CHECK(looks("") == -1);                              // nothing yet
    CHECK(looks(" \r\n") == -1);                         // only whitespace so far: wait
    CHECK(looks("POST / HTTP/1.1\r\nHost: 127.0.0.1:5599\r\n\r\n{\"action\":\"console_command\"}") == 0);
    CHECK(looks("GET / HTTP/1.1\r\n\r\n") == 0);
    CHECK(looks("OPTIONS / HTTP/1.1\r\n\r\n") == 0);
    CHECK(looks("\x16\x03\x01") == 0);                   // a TLS hello
    CHECK(looks("[1,2]") == 0);                          // JSON but not an object
    CHECK(looks("junk\n{\"a\":1}") == 0);                // stray text before the first object is no longer skipped
}

TEST_CASE("api: lua is not run for an API client, however the command is spelled", "[api_framing][security]")
{
    for (const char *refused : {"lua print(1)", "LUA x=1", "Lua", "  lua os.exit()", "\tlua\tx", "lua"})
        CHECK(api_console_command_refused(refused));
    for (const char *allowed : {"luatypedump", "lual", "ver", "creature.create IMP", "", "xlua", "player.flag lua", "stats lua"})
        CHECK_FALSE(api_console_command_refused(allowed));
    CHECK_FALSE(api_console_command_refused(nullptr));
}

TEST_CASE("api: the token compare", "[api_framing][security]")
{
    CHECK(api_token_matches("s3cret-token", "s3cret-token"));
    CHECK_FALSE(api_token_matches("s3cret-token", "s3cret-tokeN"));
    CHECK_FALSE(api_token_matches("s3cret-token", "s3cret-toke"));    // a prefix
    CHECK_FALSE(api_token_matches("s3cret-token", "s3cret-token2"));  // longer
    CHECK_FALSE(api_token_matches("s3cret-token", ""));
    CHECK_FALSE(api_token_matches("s3cret-token", nullptr));
    CHECK_FALSE(api_token_matches("", ""));                           // no token set: nothing matches
    CHECK_FALSE(api_token_matches(nullptr, "x"));
}
