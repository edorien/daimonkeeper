/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_script_syntax.cpp
 *     Script editor syntax colouring.
 * @par Purpose:
 *     See editor_script_syntax.h.
 * @par Comment:
 *     What gets which colour (the widget's palette slots):
 *       - comment      : `REM` and the rest of its line
 *       - keyword      : flow control -- IF*, ENDIF, WIN_GAME, LOSE_GAME,
 *                        NEXT_COMMAND_REUSABLE (editor_script_command_is_flow)
 *       - declaration  : every other script command
 *       - knownIdentifier : values the engine knows -- players, variables,
 *                        flags, timers, creature / room / door / trap / spell
 *                        names, comparison words
 *       - string       : "quoted text" (to the closing quote or end of line,
 *                        never across lines: a stray quote must not colour
 *                        the rest of the script)
 *       - number, punctuation ( ) , = ! < > -- as usual
 *     Anything else (party names, action point labels, typos) stays the
 *     plain identifier colour, so an unknown command name is visibly not
 *     coloured like a command.
 *     The engine matches names case-insensitively (get_id() uses
 *     strcasecmp), so the language is case-insensitive too.
 *     REM is recognised as a whole word wherever an identifier can start;
 *     that is right for every valid script, since strings are consumed
 *     first and REM is not a valid argument value.
 */
#include "pre_inc.h"
#include "editor_script_syntax.h"
#include "editor_script_commands.h"
#include "editor_script_names.h"
#include "editor_lua_support.h"
#include "lvl_script_lib.h"
#include "lvl_script_commands.h"

#include <imgui.h>
// See editor_script.cpp for why the pragma wraps this include.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#include <TextEditor.h>
#pragma GCC diagnostic pop

#include <cctype>
#include <string>
#include "post_inc.h"

namespace {

std::string lower(const std::string &s)
{
    std::string out = s;
    for (char &c : out)
        c = (char)std::tolower((unsigned char)c);
    return out;
}

bool is_word_char(ImWchar c)
{
    return (c < 128) && (std::isalnum((int)c) || c == '_');
}

TextEditor::Iterator script_tokenizer(TextEditor::Iterator start, TextEditor::Iterator end, TextEditor::Color &color)
{
    if (start == end)
        return start;
    ImWchar c = *start;

    if (c == '"')
    {
        TextEditor::Iterator it = start;
        ++it;
        while (it != end && *it != '"')
            ++it;
        if (it != end)
            ++it; // closing quote
        color = TextEditor::Color::string;
        return it;
    }

    if (c == 'R' || c == 'r')
    {
        TextEditor::Iterator it = start;
        ++it;
        if (it != end && (*it == 'E' || *it == 'e'))
        {
            ++it;
            if (it != end && (*it == 'M' || *it == 'm'))
            {
                ++it;
                if (it == end || !is_word_char(*it))
                {
                    color = TextEditor::Color::comment;
                    return end; // the rest of the line
                }
            }
        }
    }
    return start;
}

bool script_punctuation(ImWchar c)
{
    switch (c)
    {
        case '(': case ')': case ',': case '=': case '!': case '<': case '>':
            return true;
        default:
            return false;
    }
}

} // namespace

void editor_script_syntax_apply(TextEditor &editor, bool enable)
{
    static TextEditor::Language language;
    static bool initialised = false;

    if (!enable)
    {
        editor.SetLanguage(nullptr);
        return;
    }

    if (!initialised)
    {
        language.name = "Dungeon Keeper script";
        language.caseSensitive = false;
        language.customTokenizer = script_tokenizer;
        language.isPunctuation = script_punctuation;
        // Reuse the C-style identifier / number tokenizers: script names are
        // [A-Za-z_][A-Za-z0-9_]* and numbers are plain (possibly signed) digits.
        language.getIdentifier = TextEditor::Language::C()->getIdentifier;
        language.getNumber = TextEditor::Language::C()->getNumber;
        initialised = true;
    }

    // Rebuilt on every attach: config (custom creatures, rooms, spells) may
    // have changed since the last time the script editor opened.
    language.keywords.clear();
    language.declarations.clear();
    language.identifiers.clear();
    for (int i = 0; command_desc[i].textptr != NULL; i++)
    {
        std::string name = command_desc[i].textptr;
        if (editor_script_command_is_flow(name))
            language.keywords.insert(lower(name));
        else
            language.declarations.insert(lower(name));
    }
    // Aliases the parser or the original manual spell differently.
    language.keywords.insert("nextcommandreusable");
    language.keywords.insert("next_command_reuseable");
    for (const ScriptNameGroup &g : editor_script_collect_name_groups())
    {
        if (g.per_level)
            continue; // action point numbers are plain numbers
        for (const std::string &n : g.names)
            language.identifiers.insert(lower(n));
    }
    editor.SetLanguage(&language);
}

void editor_lua_syntax_apply(TextEditor &editor, bool enable)
{
    static TextEditor::Language language;
    static bool initialised = false;

    if (!enable)
    {
        editor.SetLanguage(nullptr);
        return;
    }
    if (!initialised)
    {
        language = *TextEditor::Language::Lua();
        initialised = true;
    }
    // KeeperFX API functions/constants from the stub files, and the engine's
    // player/creature/room/... names, in the "known identifier" colour.
    language.identifiers = TextEditor::Language::Lua()->identifiers;
    for (const std::string &n : editor_lua_api_names())
        language.identifiers.insert(n);
    for (const ScriptNameGroup &g : editor_script_collect_name_groups())
    {
        if (g.per_level)
            continue;
        for (const std::string &n : g.names)
            language.identifiers.insert(n);
    }
    editor.SetLanguage(&language);
}
