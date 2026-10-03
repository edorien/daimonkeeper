/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file cfgc_help.cpp
 *     See cfgc_help.h.
 */
#include "pre_inc.h"
#include "cfgc_help.h"
#include "cfgc_content.h"

#include <cctype>
#include "post_inc.h"

/******************************************************************************/
namespace {

std::string lower(std::string s)
{
    for (char &c : s)
        c = (char)std::tolower((unsigned char)c);
    return s;
}

std::string comment_text(const std::string &line)
{
    size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
        i++;
    while (i < line.size() && line[i] == ';')
        i++;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
        i++;
    size_t e = line.size();
    while (e > i && (line[e - 1] == ' ' || line[e - 1] == '\t' || line[e - 1] == '\r'))
        e--;
    return line.substr(i, e - i);
}

} // namespace

std::string cfgc_help_key(const std::string &section, const std::string &key)
{
    std::string base;
    int64_t index;
    cfgc_split_section_name(section, base, index);
    return lower(base) + "/" + lower(key);
}

std::map<std::string, std::string> cfgc_extract_help(const ConfigDocument &doc)
{
    std::map<std::string, std::string> out;
    std::string section;
    std::string pending; // comment lines directly above the current line
    for (const CfgLine &l : doc.lines())
    {
        switch (l.kind)
        {
        case CfgLine_Comment:
        {
            const std::string t = comment_text(l.text);
            if (!t.empty())
                pending += (pending.empty() ? "" : " ") + t;
            break;
        }
        case CfgLine_Section:
            section = l.name;
            pending.clear();
            break;
        case CfgLine_Key:
        {
            if (!pending.empty() && !section.empty())
            {
                const std::string k = cfgc_help_key(section, l.name);
                if (out.find(k) == out.end())
                    out[k] = pending;
            }
            pending.clear();
            break;
        }
        default: // blank and unrecognised lines end the comment run
            pending.clear();
            break;
        }
    }
    return out;
}
