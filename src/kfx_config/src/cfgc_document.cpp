/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file cfgc_document.cpp
 *     See cfgc_document.h.
 */
#include "pre_inc.h"
#include "cfgc_document.h"

#include <cctype>
#include "post_inc.h"

/******************************************************************************/
namespace {

bool is_blank_char(char c)
{
    return c == ' ' || c == '\t';
}

bool is_key_char(char c)
{
    return std::isalnum((unsigned char)c) || c == '_';
}

void classify(CfgLine &line)
{
    line.name.clear();
    line.value.clear();
    line.has_equals = false;

    size_t i = 0;
    // A UTF-8 byte order mark is part of the text but not of the syntax.
    if (line.text.compare(0, 3, "\xEF\xBB\xBF") == 0)
        i = 3;
    while (i < line.text.size() && is_blank_char(line.text[i]))
        i++;
    if (i >= line.text.size() || line.text[i] == '\r')
    {
        line.kind = CfgLine_Blank;
        return;
    }
    const char c = line.text[i];
    if (c == ';')
    {
        line.kind = CfgLine_Comment;
        return;
    }
    if (c == '[')
    {
        const size_t close = line.text.find(']', i + 1);
        if (close != std::string::npos)
        {
            line.kind = CfgLine_Section;
            line.name = line.text.substr(i + 1, close - i - 1);
            return;
        }
        line.kind = CfgLine_Other;
        return;
    }
    if (is_key_char(c))
    {
        size_t j = i;
        while (j < line.text.size() && is_key_char(line.text[j]))
            j++;
        // The key must be followed by "=", blanks, or the end of the line.
        size_t k = j;
        while (k < line.text.size() && is_blank_char(line.text[k]))
            k++;
        if (k < line.text.size() && line.text[k] != '=' && k == j)
        {
            line.kind = CfgLine_Other; // "Key)" and the like
            return;
        }
        line.kind = CfgLine_Key;
        line.name = line.text.substr(i, j - i);
        if (k < line.text.size() && line.text[k] == '=')
        {
            line.has_equals = true;
            k++;
            while (k < line.text.size() && is_blank_char(line.text[k]))
                k++;
        }
        size_t end = line.text.size();
        while (end > k && (is_blank_char(line.text[end - 1]) || line.text[end - 1] == '\r'))
            end--;
        line.value = line.text.substr(k, end - k);
        return;
    }
    line.kind = CfgLine_Other;
}

} // namespace

ConfigDocument ConfigDocument::parse(const std::string &bytes)
{
    ConfigDocument doc;
    size_t pos = 0;
    while (pos < bytes.size())
    {
        const size_t nl = bytes.find('\n', pos);
        CfgLine line;
        if (nl == std::string::npos)
        {
            line.text = bytes.substr(pos);
            line.eol = "";
            pos = bytes.size();
        }
        else
        {
            size_t end = nl;
            line.eol = "\n";
            if (end > pos && bytes[end - 1] == '\r')
            {
                end--;
                line.eol = "\r\n";
            }
            line.text = bytes.substr(pos, end - pos);
            pos = nl + 1;
        }
        classify(line);
        doc.lines_.push_back(std::move(line));
    }
    doc.rebuild_index();
    return doc;
}

std::string ConfigDocument::serialize() const
{
    std::string out;
    for (const CfgLine &l : lines_)
    {
        out += l.text;
        out += l.eol;
    }
    return out;
}

void ConfigDocument::rebuild_index()
{
    sections_.clear();
    CfgSection current;
    current.name.clear();
    current.header_line = -1;
    current.first_line = 0;
    for (size_t i = 0; i < lines_.size(); i++)
    {
        if (lines_[i].kind != CfgLine_Section)
            continue;
        current.end_line = (int64_t)i;
        if (current.header_line >= 0 || current.end_line > current.first_line)
            sections_.push_back(current);
        current = CfgSection();
        current.name = lines_[i].name;
        current.header_line = (int64_t)i;
        current.first_line = (int64_t)i + 1;
    }
    current.end_line = (int64_t)lines_.size();
    if (current.header_line >= 0 || current.end_line > current.first_line)
        sections_.push_back(current);
}

int64_t ConfigDocument::find_section(const std::string &name) const
{
    for (size_t i = 0; i < sections_.size(); i++)
        if (sections_[i].header_line >= 0 && sections_[i].name == name)
            return (int64_t)i;
    return -1;
}

std::vector<int64_t> ConfigDocument::key_lines(int64_t section_index) const
{
    std::vector<int64_t> out;
    if (section_index < 0 || (size_t)section_index >= sections_.size())
        return out;
    const CfgSection &s = sections_[(size_t)section_index];
    for (int64_t i = s.first_line; i < s.end_line; i++)
        if (lines_[(size_t)i].kind == CfgLine_Key)
            out.push_back(i);
    return out;
}

std::string ConfigDocument::dominant_eol() const
{
    size_t crlf = 0, lf = 0;
    for (const CfgLine &l : lines_)
    {
        if (l.eol == "\r\n")
            crlf++;
        else if (l.eol == "\n")
            lf++;
    }
    return crlf > lf ? "\r\n" : "\n";
}
