// First real (non-pilot) kfx_config coverage, per
// docs/refactor/testing/stage-02-testability-and-fakes.md §5's rollout
// order: config-parsing helpers in config.c are pure text/lookup
// functions with no kfx_config_state dependency, no pattern-A fixture
// needed.
#include <catch2/catch_test_macros.hpp>

#include "config.h"

#include <cstddef>
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

// The lenient parse/assign functions refactor pass 3 (S04) uses to keep the
// hand-written creature parsers' rules (README finding F10).
namespace {
struct LenientRecord {
    unsigned char small;
    long long big;
    long long flags;
};
// field_t() needs C's _Generic; spelled out here.
#define LENIENT_FIELD(member, type) (void *)offsetof(LenientRecord, member), type
LenientRecord lenient_records[2];
int64_t lenient_count = 2;
int64_t *lenient_get_count() { return &lenient_count; }
void *lenient_get_base() { return lenient_records; }
const struct NamedCommand lenient_names[] = {
    {"NULL", 0},
    {"ONE", 1},
    {"TWO", 2},
    {"FOUR", 4},
    {nullptr, 0},
};
const struct NamedField lenient_fields[] = {
    {"SMALL", 0, LENIENT_FIELD(small, dt_uchar), 0, 0, 0, nullptr, value_atoi, assign_cast},
    {"BIG", 0, LENIENT_FIELD(big, dt_longlong), 0, 0, 0, lenient_names, value_id_positive, assign_cast},
    {"FLAGS", -1, LENIENT_FIELD(flags, dt_longlong), 0, 0, 0, lenient_names, value_ids_or, assign_cast},
    {nullptr, 0, nullptr, 0, 0, 0, 0, nullptr, nullptr, nullptr},
};
const struct NamedFieldSet lenient_set = {
    lenient_get_count, "", lenient_fields, nullptr, 2, sizeof(LenientRecord), lenient_get_base,
};
int64_t lenient_parse(int64_t row, const char *text) {
    return parse_named_field_value(&lenient_fields[row], text, &lenient_set, 1, "test", 0);
}
}

TEST_CASE("value_atoi reads like atoi() and assign_cast stores with C's conversion", "[kfx_config][config]") {
    std::memset(lenient_records, 0, sizeof(lenient_records));
    CHECK(lenient_parse(0, "12abc") == 12);
    CHECK(lenient_parse(0, "abc") == 0);
    assign_named_field_value(&lenient_fields[0], lenient_parse(0, "300"), &lenient_set, 1, "test", 0);
    CHECK(lenient_records[1].small == 44); // wraps, where assign_default would refuse it
    assign_named_field_value(&lenient_fields[0], lenient_parse(0, "-1"), &lenient_set, 1, "test", 0);
    CHECK(lenient_records[1].small == 255);
    CHECK(lenient_records[0].small == 0);
}

TEST_CASE("value_id_positive leaves the field as it is for an unknown name or entry 0", "[kfx_config][config]") {
    std::memset(lenient_records, 0, sizeof(lenient_records));
    assign_named_field_value(&lenient_fields[1], lenient_parse(1, "TWO"), &lenient_set, 1, "test", 0);
    CHECK(lenient_records[1].big == 2);
    CHECK(lenient_parse(1, "NULL") == NAMFIELD_KEEP);
    CHECK(lenient_parse(1, "SEVEN") == NAMFIELD_KEEP);
    assign_named_field_value(&lenient_fields[1], NAMFIELD_KEEP, &lenient_set, 1, "test", 0);
    CHECK(lenient_records[1].big == 2);
}

TEST_CASE("value_ids_or ORs the known names of a line, starting from 0", "[kfx_config][config]") {
    CHECK(lenient_parse(2, "ONE FOUR") == 5);
    CHECK(lenient_parse(2, "ONE SEVEN NULL TWO") == 3);
    CHECK(lenient_parse(2, "") == 0);
}

TEST_CASE("a key in a table's last row given more values than it has rows is warned about, not a crash (F11)", "[kfx_config][config]") {
    std::memset(lenient_records, 0, sizeof(lenient_records));
    static const struct NamedField last_single[] = {
        {"SMALL", 0, LENIENT_FIELD(small, dt_uchar), 0, 0, 0, nullptr, value_atoi, assign_cast},
        {nullptr, 0, nullptr, 0, 0, 0, 0, nullptr, nullptr, nullptr},
    };
    const char buf[] = "[block]\nSMALL = 7 8 9\n";
    const int64_t len = (int64_t)std::strlen(buf);
    REQUIRE(parse_named_field_block(buf, len, "test", 0, "block", last_single, &lenient_set, 1));
    CHECK(lenient_records[1].small == 7);
}
