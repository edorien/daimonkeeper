// Syntax colouring for the script editor (editor_script_syntax.cpp).
// TextEditor colourises inside a render pass, which a unit test doesn't
// have, so this walks a line the way the widget's own tokenizer loop does
// (custom tokenizer -> identifier -> number -> punctuation, identifiers
// lower-cased because the language is case-insensitive) using the very
// Language object the editor gets. Names come from the engine's real,
// statically initialised command / player / variable tables; tables filled
// from config at load time (creatures, rooms, spells) are empty in a bare
// test binary, so they aren't asserted here.
#include <catch2/catch_test_macros.hpp>

#include <imgui.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#include <TextEditor.h>
#pragma GCC diagnostic pop

#include "editor_script_syntax.h"

#include <cctype>
#include <string>
#include <vector>

namespace {

using Color = TextEditor::Color;

// One colour per character of `line`.
std::vector<Color> colour_line(const TextEditor::Language *lang, const std::string &line)
{
    std::vector<TextEditor::Glyph> glyphs;
    for (char c : line)
        glyphs.emplace_back((ImWchar)(unsigned char)c, Color::text);
    std::vector<Color> out(line.size(), Color::text);
    TextEditor::Iterator begin(glyphs.data());
    TextEditor::Iterator end(glyphs.data() + glyphs.size());

    size_t i = 0;
    while (i < line.size())
    {
        TextEditor::Iterator tok(glyphs.data() + i);
        Color color = Color::text;
        TextEditor::Iterator next = tok;
        if (std::isspace((unsigned char)line[i]))
        {
            out[i++] = Color::whitespace;
            continue;
        }
        if (lang->customTokenizer && (next = lang->customTokenizer(tok, end, color)) != tok)
        {
        }
        else if (lang->getIdentifier && (next = lang->getIdentifier(tok, end)) != tok)
        {
            std::string word;
            for (size_t j = i; j < i + (next - tok); j++)
                word += (char)std::tolower((unsigned char)line[j]);
            color = Color::identifier;
            if (lang->keywords.count(word))
                color = Color::keyword;
            else if (lang->declarations.count(word))
                color = Color::declaration;
            else if (lang->identifiers.count(word))
                color = Color::knownIdentifier;
        }
        else if (lang->getNumber && (next = lang->getNumber(tok, end)) != tok)
        {
            color = Color::number;
        }
        else if (lang->isPunctuation && lang->isPunctuation((ImWchar)(unsigned char)line[i]))
        {
            color = Color::punctuation;
            next = tok;
            ++next;
        }
        else
        {
            next = tok;
            ++next;
        }
        size_t len = next - tok;
        for (size_t j = 0; j < len; j++)
            out[i + j] = color;
        i += len;
    }
    (void)begin;
    return out;
}

struct Highlighter
{
    TextEditor editor;
    const TextEditor::Language *lang;
    Highlighter()
    {
        editor_script_syntax_apply(editor, true);
        lang = editor.GetLanguage();
    }
    Color at(const std::string &line, size_t pos) const { return colour_line(lang, line)[pos]; }
};

} // namespace

TEST_CASE("syntax: language attaches and detaches", "[kfx_editor][script_syntax]") {
    TextEditor editor;
    editor_script_syntax_apply(editor, true);
    REQUIRE(editor.HasLanguage());
    editor_script_syntax_apply(editor, false);
    CHECK_FALSE(editor.HasLanguage());
}

TEST_CASE("syntax: flow control is a keyword, other commands are declarations", "[kfx_editor][script_syntax]") {
    Highlighter h;
    const std::string line = "IF(PLAYER0,MONEY>=1000)";
    CHECK(h.at(line, 0) == Color::keyword);      // IF
    CHECK(h.at(line, 1) == Color::keyword);
    CHECK(h.at(line, 2) == Color::punctuation);  // (
    CHECK(h.at(line, line.find("PLAYER0")) == Color::knownIdentifier);
    CHECK(h.at(line, line.find(',')) == Color::punctuation);
    CHECK(h.at(line, line.find("MONEY")) == Color::knownIdentifier);
    CHECK(h.at(line, line.find(">=")) == Color::punctuation);
    CHECK(h.at(line, line.find("=") ) == Color::punctuation);
    CHECK(h.at(line, line.find("1000")) == Color::number);
    CHECK(h.at(line, line.find(')')) == Color::punctuation);

    const std::string cmd = "START_MONEY(PLAYER1,20000)";
    CHECK(h.at(cmd, 0) == Color::declaration);
    CHECK(h.at(cmd, cmd.find("PLAYER1")) == Color::knownIdentifier);
    CHECK(h.at(cmd, cmd.find("20000")) == Color::number);

    CHECK(h.at("ENDIF", 0) == Color::keyword);
    CHECK(h.at("WIN_GAME", 0) == Color::keyword);
}

TEST_CASE("syntax: matching is case-insensitive like the engine", "[kfx_editor][script_syntax]") {
    Highlighter h;
    CHECK(h.at("start_money(player1,5)", 0) == Color::declaration);
    CHECK(h.at("start_money(player1,5)", 12) == Color::knownIdentifier); // player1
    CHECK(h.at("endif", 0) == Color::keyword);
}

TEST_CASE("syntax: REM comments out the rest of the line", "[kfx_editor][script_syntax]") {
    Highlighter h;
    const std::string line = "REM it's IF (a comment)";
    auto c = colour_line(h.lang, line);
    for (size_t i = 0; i < line.size(); i++)
        if (!std::isspace((unsigned char)line[i]))
            CHECK(c[i] == Color::comment);
    // A command that merely starts with REM is not a comment.
    CHECK(h.at("REMOVE_SACRIFICE_RECIPE(A)", 0) == Color::declaration);
}

TEST_CASE("syntax: strings never leak past their line or swallow REM", "[kfx_editor][script_syntax]") {
    Highlighter h;
    const std::string line = "QUICK_OBJECTIVE(1,\"REM (not a comment), 12\")";
    auto c = colour_line(h.lang, line);
    size_t open = line.find('"');
    size_t close = line.rfind('"');
    for (size_t i = open; i <= close; i++)
        CHECK(c[i] == Color::string);
    CHECK(c[close + 1] == Color::punctuation);
    CHECK(c[0] == Color::declaration);

    // Unterminated: string runs to the end of this line only (state is per line).
    const std::string open_line = "QUICK_OBJECTIVE(1,\"abc";
    auto c2 = colour_line(h.lang, open_line);
    CHECK(c2.back() == Color::string);
}

TEST_CASE("syntax: unknown names keep the plain identifier colour", "[kfx_editor][script_syntax]") {
    Highlighter h;
    const std::string line = "ADD_TO_PARTY(MYPARTY,0)";
    CHECK(h.at(line, 0) == Color::declaration);
    CHECK(h.at(line, 13) == Color::identifier); // MYPARTY: a user-chosen name
}
