// docs/refactor/editor/05-script-and-level-settings.md §4.4 -- Catch2
// coverage for editor_script_message.cpp: pure string logic behind the
// Objective / Message window (formatting, message numbering, and splicing a
// line into a script without landing inside the managed setup region).
#include <catch2/catch_test_macros.hpp>

#include "editor_script_message.h"

#include <string>

namespace {
const char *const kBegin = "REM --- editor-managed setup: do not hand-edit between these markers ---";
const char *const kEnd = "REM --- end editor-managed setup ---";
}

TEST_CASE("format_message builds objective and information lines", "[kfx_editor][script_message]") {
    CHECK(editor_script_format_message(MsgKind_Objective, 12, "Build a lair.", "") == "QUICK_OBJECTIVE(12,\"Build a lair.\")");
    CHECK(editor_script_format_message(MsgKind_Information, 3, "Hi", "") == "QUICK_INFORMATION(3,\"Hi\")");
}

TEST_CASE("format_message sanitizes text and location", "[kfx_editor][script_message]") {
    // Quotes can't be escaped in the script tokenizer; line breaks would end the command.
    CHECK(editor_script_format_message(MsgKind_Objective, 1, "say \"hi\"\nthere", "") ==
        "QUICK_OBJECTIVE(1,\"say 'hi' there\")");
    CHECK(editor_script_format_message(MsgKind_Objective, 1, "x", "PLAYER0") == "QUICK_OBJECTIVE(1,\"x\",PLAYER0)");
    // Not a plain identifier -> dropped rather than emitting a broken line.
    CHECK(editor_script_format_message(MsgKind_Objective, 1, "x", "a b),") == "QUICK_OBJECTIVE(1,\"x\")");
}

TEST_CASE("format_message truncates to the engine's text limit", "[kfx_editor][script_message]") {
    std::string long_text(2000, 'a');
    std::string line = editor_script_format_message(MsgKind_Objective, 0, long_text, "");
    CHECK(line == "QUICK_OBJECTIVE(0,\"" + std::string(kScriptMessageMaxChars, 'a') + "\")");
}

TEST_CASE("message numbering scans both commands and skips comments", "[kfx_editor][script_message]") {
    const std::string script =
        "REM QUICK_OBJECTIVE(0,\"commented out\")\n"
        "QUICK_OBJECTIVE(1,\"a\")\n"
        "  QUICK_INFORMATION ( 2 ,\"b\")\n"
        "QUICK_OBJECTIVE_WITH_POS(4,\"c\",1,1)\n";
    CHECK_FALSE(editor_script_message_number_used(script, 0));
    CHECK(editor_script_message_number_used(script, 1));
    CHECK(editor_script_message_number_used(script, 2));
    CHECK(editor_script_message_number_used(script, 4));
    CHECK(editor_script_next_message_number(script) == 0);
    CHECK(editor_script_next_message_number("QUICK_OBJECTIVE(0,\"a\")\nQUICK_OBJECTIVE(1,\"b\")\n") == 2);
    CHECK(editor_script_next_message_number("") == 0);
}

TEST_CASE("insert_block splices a line before the given line", "[kfx_editor][script_message]") {
    CHECK(editor_script_insert_block("a\nb\nc\n", 1, "X") == "a\nX\nb\nc\n");
    CHECK(editor_script_insert_block("a\nb", 0, "X") == "X\na\nb");
    CHECK(editor_script_insert_block("", 0, "X") == "X\n");
}

TEST_CASE("insert_block appends when the line is past the end", "[kfx_editor][script_message]") {
    CHECK(editor_script_insert_block("a\nb\n", (size_t)-1, "X") == "a\nb\nX\n");
    // Last line lacks a newline: it must get one first, not be glued to the block.
    CHECK(editor_script_insert_block("a\nb", (size_t)-1, "X") == "a\nb\nX\n");
}

TEST_CASE("insert_block keeps CRLF scripts consistent", "[kfx_editor][script_message]") {
    CHECK(editor_script_insert_block("a\r\nb\r\n", 1, "X") == "a\r\nX\r\nb\r\n");
}

TEST_CASE("insert never lands inside the managed setup region", "[kfx_editor][script_message]") {
    const std::string script = std::string("REM header\n") + kBegin + "\nSET_GENERATE_SPEED(300)\n" + kEnd + "\nIF(x)\n";
    // Lines: 0 header, 1 begin, 2 body, 3 end, 4 IF.
    CHECK(editor_script_safe_insert_line(script, 0) == 0);
    CHECK(editor_script_safe_insert_line(script, 1) == 4);
    CHECK(editor_script_safe_insert_line(script, 2) == 4);
    CHECK(editor_script_safe_insert_line(script, 3) == 4);
    CHECK(editor_script_safe_insert_line(script, 4) == 4);

    std::string out = editor_script_insert_block(script, 2, "X");
    CHECK(out == std::string("REM header\n") + kBegin + "\nSET_GENERATE_SPEED(300)\n" + kEnd + "\nX\nIF(x)\n");
}

TEST_CASE("insert with the managed region as the script tail", "[kfx_editor][script_message]") {
    const std::string script = std::string(kBegin) + "\nbody\n" + kEnd;
    CHECK(editor_script_safe_insert_line(script, 1) == 3);
    CHECK(editor_script_insert_block(script, 1, "X") == script + "\nX\n");
}

TEST_CASE("an unterminated managed region is not treated as a region", "[kfx_editor][script_message]") {
    const std::string script = std::string(kBegin) + "\nbody\n";
    CHECK(editor_script_safe_insert_line(script, 1) == 1);
}
