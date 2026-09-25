/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_script_validate.cpp
 *     See editor_script_validate.h.
 * @par Comment:
 *     A script line is one command: NAME(arg, arg, ...). "REM" at the start
 *     of a line is a comment. The engine's argument string uses upper-case
 *     letters for required arguments, lower-case for optional ones, '+' for
 *     repeatable, ' ' padding and '!' as a modifier.
 */
#include "pre_inc.h"
#include "editor_script_validate.h"
#include "script_setup.h"
#include "lvl_script_lib.h"
#include "lvl_script_commands.h" // command_desc

#include <algorithm>
#include <cctype>
#include <cstdio>
#include "post_inc.h"

namespace {

std::string upper(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::toupper(c); });
    return s;
}

bool is_word(char c)
{
    return std::isalnum((unsigned char)c) || c == '_';
}

// Number of arguments in the text after the command name, or -1 if the
// parentheses are unbalanced. The engine's tokenizer separates arguments by
// commas *or* whitespace (IF(PLAYER0,TREASURE >= 1) has four), and a quoted
// string is one argument whatever it contains.
int64_t count_args(const std::string &rest)
{
    size_t i = 0;
    while (i < rest.size() && std::isspace((unsigned char)rest[i]))
        i++;
    if (i >= rest.size() || rest[i] != '(')
        return 0;
    i++;
    int64_t depth = 1;
    bool in_quote = false;
    bool in_token = false;
    int64_t tokens = 0;
    for (; i < rest.size(); i++)
    {
        const char c = rest[i];
        if (in_quote)
        {
            if (c == '"')
                in_quote = false;
            continue;
        }
        if (c == '"')
        {
            in_quote = true;
            if (!in_token)
                tokens++;
            in_token = true;
        }
        else if (c == '(')
        {
            depth++;
            if (!in_token)
                tokens++;
            in_token = true;
        }
        else if (c == ')')
        {
            if (--depth == 0)
                return tokens;
        }
        else if (c == ',' || std::isspace((unsigned char)c))
            in_token = false;
        else
        {
            if (!in_token)
                tokens++;
            in_token = true;
        }
    }
    return -1;
}

void arg_limits(const char *args, int64_t &required, int64_t &maximum)
{
    required = 0;
    maximum = 0;
    bool repeat = false;
    for (const char *p = args; p != nullptr && *p != '\0'; p++)
    {
        if (*p == '+')
            repeat = true;
        else if (std::isupper((unsigned char)*p))
        {
            required++;
            maximum++;
        }
        else if (std::islower((unsigned char)*p))
            maximum++;
    }
    if (repeat)
        maximum = 1000;
}

} // namespace

bool editor_script_command_opens_block(const std::string &name)
{
    return name.compare(0, 2, "IF") == 0 && (name.size() == 2 || name[2] == '_');
}

std::vector<ScriptIssue> editor_script_validate(const std::string &text, const ScriptCommandLookup &lookup)
{
    std::vector<ScriptIssue> issues;
    std::vector<size_t> open_blocks; // line of each unclosed IF
    size_t line_no = 0;
    size_t pos = 0;
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0)
        pos = 3; // UTF-8 byte order mark (some shipped scripts have one)
    while (pos <= text.size())
    {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos)
            eol = text.size();
        std::string line = text.substr(pos, eol - pos);
        pos = eol + 1;
        const size_t this_line = line_no++;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        size_t i = 0;
        while (i < line.size() && std::isspace((unsigned char)line[i]))
            i++;
        if (i >= line.size())
            continue;
        size_t j = i;
        while (j < line.size() && is_word(line[j]))
            j++;
        if (j == i)
        {
            issues.push_back({this_line, ScrIssue_Error, "Expected a command name."});
            continue;
        }
        const std::string name = upper(line.substr(i, j - i));
        if (name == "REM")
            continue;
        const char *args = lookup ? lookup(name) : nullptr;
        if (args == nullptr)
        {
            issues.push_back({this_line, ScrIssue_Error, "Unknown command " + name + "."});
            continue;
        }
        const int64_t given = count_args(line.substr(j));
        if (given < 0)
        {
            issues.push_back({this_line, ScrIssue_Error, name + ": missing closing parenthesis."});
        }
        else
        {
            int64_t required, maximum;
            arg_limits(args, required, maximum);
            char buf[160];
            // Only a warning: the engine's tokenizer is looser than the argument
            // string (comparison operators can be glued to their operands,
            // trailing values default), and 143 of 452 shipped scripts trip a
            // stricter rule while being perfectly good.
            if (given < required)
            {
                snprintf(buf, sizeof(buf), "%s usually needs %" PRId64 " argument%s, found %" PRId64 ".", name.c_str(), (int64_t)(required),
                    required == 1 ? "" : "s", (int64_t)(given));
                issues.push_back({this_line, ScrIssue_Warning, buf});
            }
            else if (given > maximum)
            {
                snprintf(buf, sizeof(buf), "%s takes at most %" PRId64 " argument%s, found %" PRId64 ".", name.c_str(), (int64_t)(maximum),
                    maximum == 1 ? "" : "s", (int64_t)(given));
                issues.push_back({this_line, ScrIssue_Warning, buf});
            }
        }
        if (editor_script_command_opens_block(name))
            open_blocks.push_back(this_line);
        else if (name == "ENDIF")
        {
            if (open_blocks.empty())
                issues.push_back({this_line, ScrIssue_Error, "ENDIF without a matching IF."});
            else
                open_blocks.pop_back();
        }
    }
    for (size_t l : open_blocks)
        issues.push_back({l, ScrIssue_Error, "IF is never closed with ENDIF."});
    std::stable_sort(issues.begin(), issues.end(),
        [](const ScriptIssue &a, const ScriptIssue &b) { return a.line < b.line; });
    return issues;
}

std::vector<ScriptIssue> editor_script_check_duplicate_win_lose(const std::string &script_text)
{
    std::vector<ScriptIssue> issues;
    for (const DuplicateWinLose &d : script_setup_find_duplicate_win_lose(script_text))
    {
        ScriptIssue is;
        is.line = d.line;
        is.severity = ScrIssue_Warning;
        is.message = std::string(d.win ? "WIN_GAME" : "LOSE_GAME") + " here as well as a " + (d.win ? "win" : "lose")
            + " rule in the setup block (Level Settings > Script Setup): the level has two";
        issues.push_back(is);
    }
    return issues;
}

std::vector<ScriptIssue> editor_script_validate_engine(const std::string &text)
{
    std::vector<ScriptIssue> issues = editor_script_validate(text, [](const std::string &name) -> const char * {
        for (int64_t i = 0; command_desc[i].textptr != NULL; i++)
            if (name == command_desc[i].textptr)
                return command_desc[i].args;
        return nullptr;
    });
    // A win/lose rule in the managed setup block plus a hand-written one.
    for (const ScriptIssue &dup : editor_script_check_duplicate_win_lose(text))
        issues.push_back(dup);
    std::stable_sort(issues.begin(), issues.end(),
        [](const ScriptIssue &a, const ScriptIssue &b) { return a.line < b.line; });
    return issues;
}
