// First real (non-pilot) kfx_config coverage, per
// docs/refactor/testing/stage-02-testability-and-fakes.md §5's rollout
// order: config-parsing helpers in config.c are pure text/lookup
// functions with no kfx_config_state dependency, no pattern-A fixture
// needed.
#include <catch2/catch_test_macros.hpp>

#include "config.h"

#include <cstring>

TEST_CASE("parameter_is_number accepts plain integers", "[kfx_config][config]") {
    CHECK(parameter_is_number("0"));
    CHECK(parameter_is_number("123"));
    CHECK(parameter_is_number("-123"));
}

TEST_CASE("parameter_is_number trims surrounding spaces", "[kfx_config][config]") {
    CHECK(parameter_is_number("  42  "));
    CHECK(parameter_is_number("-7 "));
}

TEST_CASE("parameter_is_number rejects non-numeric input", "[kfx_config][config]") {
    CHECK_FALSE(parameter_is_number(nullptr));
    CHECK_FALSE(parameter_is_number(""));
    CHECK_FALSE(parameter_is_number("   "));
    CHECK_FALSE(parameter_is_number("abc"));
    CHECK_FALSE(parameter_is_number("12a3"));
    CHECK_FALSE(parameter_is_number("1.5"));
}

TEST_CASE("get_id looks up a name case-insensitively", "[kfx_config][config]") {
    static const struct NamedCommand commands[] = {
        {"FIRST",  1},
        {"Second", 2},
        {nullptr,  0},
    };
    CHECK(get_id(commands, "first") == 1);
    CHECK(get_id(commands, "SECOND") == 2);
    CHECK(get_id(commands, "Second") == 2);
}

TEST_CASE("get_id returns -1 for an unknown name or a null argument", "[kfx_config][config]") {
    static const struct NamedCommand commands[] = {
        {"FIRST", 1},
        {nullptr, 0},
    };
    CHECK(get_id(commands, "nope") == -1);
    CHECK(get_id(commands, nullptr) == -1);
    CHECK(get_id(nullptr, "FIRST") == -1);
}

// get_conf_parameter_whole/_single, recognize_conf_parameter and
// get_conf_parameter_text are the old-style (pre-TOML) line-oriented
// config tokenizer -- pure buffer/position-cursor functions, same "no
// kfx_config_state dependency" shape as parameter_is_number/get_id
// above, just operating on a raw char buffer plus an in/out int32_t
// cursor instead of a single string.

TEST_CASE("get_conf_parameter_whole reads the rest of the line, skipping leading blanks", "[kfx_config][config]") {
    const char *buf = "  hello world\n";
    int64_t pos = 0;
    char dst[32];
    int64_t len = get_conf_parameter_whole(buf, &pos, (int64_t)strlen(buf), dst, sizeof(dst));
    CHECK(len == 11);
    CHECK(std::strcmp(dst, "hello world") == 0);
    CHECK(pos == 13); // left pointing at the '\n', not past it
}

TEST_CASE("get_conf_parameter_whole truncates at dstlen", "[kfx_config][config]") {
    const char *buf = "hello\n";
    int64_t pos = 0;
    char dst[4];
    int64_t len = get_conf_parameter_whole(buf, &pos, (int64_t)strlen(buf), dst, sizeof(dst));
    CHECK(len == 3);
    CHECK(std::strcmp(dst, "hel") == 0);
}

TEST_CASE("get_conf_parameter_whole returns 0 once pos reaches buflen", "[kfx_config][config]") {
    const char *buf = "x";
    int64_t pos = 1;
    char dst[8];
    CHECK(get_conf_parameter_whole(buf, &pos, 1, dst, sizeof(dst)) == 0);
}

TEST_CASE("get_conf_parameter_single reads only the next whitespace-delimited token", "[kfx_config][config]") {
    const char *buf = "foo bar\n";
    int64_t pos = 0;
    char dst[32];
    int64_t len = get_conf_parameter_single(buf, &pos, (int64_t)strlen(buf), dst, sizeof(dst));
    CHECK(len == 3);
    CHECK(std::strcmp(dst, "foo") == 0);
    CHECK(pos == 3); // left pointing at the separating space

    // A second call from the advanced position skips that space and
    // reads the next token.
    len = get_conf_parameter_single(buf, &pos, (int64_t)strlen(buf), dst, sizeof(dst));
    CHECK(len == 3);
    CHECK(std::strcmp(dst, "bar") == 0);
}

TEST_CASE("recognize_conf_parameter matches a whole token case-insensitively and reports the command number", "[kfx_config][config]") {
    static const struct NamedCommand commands[] = {
        {"FOO", 1},
        {"BAR", 2},
        {nullptr, 0},
    };
    const char *buf = "foo\n";
    int64_t pos = 0;
    CHECK(recognize_conf_parameter(buf, &pos, (int64_t)strlen(buf), commands) == 1);
    CHECK(pos == 3); // stops before the EOLN, doesn't consume it
}

TEST_CASE("recognize_conf_parameter advances past a trailing blank when one follows the token", "[kfx_config][config]") {
    static const struct NamedCommand commands[] = {
        {"FOO", 1},
        {nullptr, 0},
    };
    const char *buf = "FOO extra";
    int64_t pos = 0;
    CHECK(recognize_conf_parameter(buf, &pos, (int64_t)strlen(buf), commands) == 1);
    CHECK(pos == 4); // past "FOO "
}

TEST_CASE("recognize_conf_parameter requires a full token match, not just a name prefix", "[kfx_config][config]") {
    static const struct NamedCommand commands[] = {
        {"FOO", 1},
        {nullptr, 0},
    };
    // "FOOBAR" starts with "FOO", but the character right after isn't a
    // line end or blank, so this must NOT match.
    const char *buf = "FOOBAR\n";
    int64_t pos = 0;
    CHECK(recognize_conf_parameter(buf, &pos, (int64_t)strlen(buf), commands) == 0);
}

TEST_CASE("recognize_conf_parameter returns 0 for an unrecognized token or when pos reaches buflen", "[kfx_config][config]") {
    static const struct NamedCommand commands[] = {
        {"FOO", 1},
        {nullptr, 0},
    };
    const char *buf = "XYZ\n";
    int64_t pos = 0;
    CHECK(recognize_conf_parameter(buf, &pos, (int64_t)strlen(buf), commands) == 0);

    pos = 1;
    CHECK(recognize_conf_parameter("x", &pos, 1, commands) == 0);
}

TEST_CASE("get_conf_parameter_text looks up a command's name by its number", "[kfx_config][config]") {
    static const struct NamedCommand commands[] = {
        {"ONE", 1},
        {"TWO", 2},
        {nullptr, 0},
    };
    CHECK(std::strcmp(get_conf_parameter_text(commands, 2), "TWO") == 0);
}

TEST_CASE("get_conf_parameter_text returns an empty string for an unknown number", "[kfx_config][config]") {
    static const struct NamedCommand commands[] = {
        {"ONE", 1},
        {nullptr, 0},
    };
    CHECK(std::strcmp(get_conf_parameter_text(commands, 99), "") == 0);
}
