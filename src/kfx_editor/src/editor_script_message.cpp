/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_script_message.cpp
 *     Plain-string helpers for the objective / message window.
 * @par Purpose:
 *     See editor_script_message.h.
 * @par Comment:
 *     The number limit (0..255) and text limit (1023) come from
 *     quick_objective_check() / quick_information_check() in
 *     lvl_script_commands.c; both commands write the same
 *     kfx_sim_state.quick_messages[] table, and the engine only warns
 *     ("overwritten by different text") on reuse, so uniqueness is advice,
 *     not a hard rule.
 */
#include <stdint.h>
#include "pre_inc.h"
#include "editor_script_message.h"
#include "post_inc.h"

#include <cctype>
#include <cstdlib>
#include <vector>

namespace {

const char *const kBeginMarker = "REM --- editor-managed setup: do not hand-edit between these markers ---";
const char *const kEndMarker = "REM --- end editor-managed setup ---";

std::string trim(const std::string &s)
{
    size_t b = 0, e = s.size();
    while (b < e && std::isspace((unsigned char)s[b]))
        b++;
    while (e > b && std::isspace((unsigned char)s[e - 1]))
        e--;
    return s.substr(b, e - b);
}

std::vector<std::string> split_lines(const std::string &text)
{
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= text.size())
    {
        size_t nl = text.find('\n', start);
        if (nl == std::string::npos)
        {
            lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, nl - start));
        start = nl + 1;
    }
    return lines;
}

bool is_identifier(const std::string &s)
{
    if (s.empty())
        return false;
    for (char c : s)
        if (!(std::isalnum((unsigned char)c) || c == '_'))
            return false;
    return true;
}

// Message numbers used by one script line, or -1.
int64_t message_number_in_line(const std::string &raw_line)
{
    std::string line = trim(raw_line);
    if (line.compare(0, 3, "REM") == 0)
        return -1;
    static const char *const kPrefixes[] = { "QUICK_OBJECTIVE", "QUICK_INFORMATION" };
    for (const char *prefix : kPrefixes)
    {
        size_t pos = line.find(prefix);
        if (pos == std::string::npos)
            continue;
        pos += std::char_traits<char>::length(prefix);
        // _WITH_POS variants.
        if (line.compare(pos, 9, "_WITH_POS") == 0)
            pos += 9;
        while (pos < line.size() && std::isspace((unsigned char)line[pos]))
            pos++;
        if (pos >= line.size() || line[pos] != '(')
            continue;
        pos++;
        while (pos < line.size() && std::isspace((unsigned char)line[pos]))
            pos++;
        if (pos >= line.size() || !std::isdigit((unsigned char)line[pos]))
            continue;
        return std::atoi(line.c_str() + pos);
    }
    return -1;
}

} // namespace

static const char *editor_script_message_command(int64_t kind)
{
    return (kind == MsgKind_Information) ? "QUICK_INFORMATION" : "QUICK_OBJECTIVE";
}

std::string editor_script_format_message(int64_t kind, int64_t number, const std::string &text, const std::string &location)
{
    std::string clean;
    for (char c : text)
    {
        if (c == '"')
            clean += '\'';
        else if (c == '\n' || c == '\r' || c == '\t')
            clean += ' ';
        else
            clean += c;
    }
    if (clean.size() > kScriptMessageMaxChars)
        clean.resize(kScriptMessageMaxChars);
    std::string loc = trim(location);
    std::string out = editor_script_message_command(kind);
    out += "(" + std::to_string(number) + ",\"" + clean + "\"";
    if (is_identifier(loc))
        out += "," + loc;
    out += ")";
    return out;
}

bool editor_script_message_number_used(const std::string &script_text, int64_t number)
{
    for (const std::string &line : split_lines(script_text))
        if (message_number_in_line(line) == number)
            return true;
    return false;
}

int64_t editor_script_next_message_number(const std::string &script_text)
{
    std::vector<bool> used(kScriptMessageCount, false);
    for (const std::string &line : split_lines(script_text))
    {
        int64_t n = message_number_in_line(line);
        if (n >= 0 && n < kScriptMessageCount)
            used[n] = true;
    }
    for (int64_t i = 0; i < kScriptMessageCount; i++)
        if (!used[i])
            return i;
    return -1;
}

size_t editor_script_safe_insert_line(const std::string &script_text, size_t line)
{
    std::vector<std::string> lines = split_lines(script_text);
    size_t first = lines.size(), last = lines.size();
    for (size_t i = 0; i < lines.size(); i++)
    {
        std::string t = trim(lines[i]);
        if (first == lines.size() && t == kBeginMarker)
            first = i;
        else if (first != lines.size() && t == kEndMarker)
        {
            last = i;
            break;
        }
    }
    if (first == lines.size() || last == lines.size())
        return line; // no complete region
    if (line >= first && line <= last)
        return last + 1;
    return line;
}

std::string editor_script_insert_block(const std::string &script_text, size_t line, const std::string &block)
{
    const std::string eol = (script_text.find("\r\n") != std::string::npos) ? "\r\n" : "\n";
    line = editor_script_safe_insert_line(script_text, line);
    std::vector<std::string> lines = split_lines(script_text);
    if (line > lines.size())
        line = lines.size();
    std::string out;
    for (size_t i = 0; i < lines.size(); i++)
    {
        if (i == line)
            out += block + eol;
        out += lines[i];
        // split_lines keeps a final empty element when the text ends with a
        // newline; only re-add the separator between elements.
        if (i + 1 < lines.size())
            out += "\n";
    }
    if (line >= lines.size())
    {
        // Appending: make sure the previous last line is terminated (unless
        // the script was empty or already ended with a newline, i.e. its
        // last element is empty).
        if (!lines.empty() && !lines.back().empty())
            out += eol;
        out += block + eol;
    }
    return out;
}
