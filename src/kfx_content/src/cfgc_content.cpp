/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file cfgc_content.cpp
 *     See cfgc_content.h.
 */
#include "pre_inc.h"
#include "cfgc_content.h"

#include <cctype>
#include "post_inc.h"

/******************************************************************************/
namespace {

bool iequals(const std::string &a, const std::string &b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); i++)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i]))
            return false;
    return true;
}

// The loader treats a token that starts with ';' as the start of a comment, so
// a ';' at the start of the value or after a blank ends the value.
std::string strip_inline_comment(const std::string &value)
{
    size_t cut = value.size();
    for (size_t i = 0; i < value.size(); i++)
    {
        if (value[i] == ';' && (i == 0 || value[i - 1] == ' ' || value[i - 1] == '\t'))
        {
            cut = i;
            break;
        }
    }
    while (cut > 0 && (value[cut - 1] == ' ' || value[cut - 1] == '\t'))
        cut--;
    return value.substr(0, cut);
}

} // namespace

void cfgc_split_section_name(const std::string &name, std::string &basename, int64_t &index)
{
    size_t digits = 0;
    while (digits < name.size() && std::isdigit((unsigned char)name[name.size() - 1 - digits]))
        digits++;
    // Digits only, or none, or too many to be a number: a plain named block.
    if (digits == 0 || digits == name.size() || digits > 9)
    {
        basename = name;
        index = -1;
        return;
    }
    basename = name.substr(0, name.size() - digits);
    index = std::stoll(name.substr(name.size() - digits));
}

const CfgField *CfgContentSection::find_field(const std::string &key) const
{
    for (const CfgField &f : fields)
        if (iequals(f.key, key))
            return &f;
    return nullptr;
}

const std::string *CfgContentSection::last_value(const std::string &key) const
{
    const CfgField *f = find_field(key);
    if (f == nullptr || f->values.empty())
        return nullptr;
    return &f->values.back();
}

const CfgContentSection *ConfigContent::find_section(const std::string &name) const
{
    for (const CfgContentSection &s : sections)
        if (s.name == name)
            return &s;
    return nullptr;
}

const CfgContentSection *ConfigContent::find_section(const std::string &basename, int64_t index) const
{
    for (const CfgContentSection &s : sections)
        if (s.index == index && s.basename == basename)
            return &s;
    return nullptr;
}

ConfigContent read_config_content(const ConfigDocument &doc, const std::string &kind, bool partial)
{
    ConfigContent out;
    out.kind = kind;
    out.partial = partial;
    for (size_t si = 0; si < doc.sections().size(); si++)
    {
        const CfgSection &sec = doc.sections()[si];
        if (sec.header_line < 0)
            continue; // preamble
        CfgContentSection cs;
        cs.name = sec.name;
        cfgc_split_section_name(cs.name, cs.basename, cs.index);
        for (int64_t li : doc.key_lines((int64_t)si))
        {
            const CfgLine &line = doc.lines()[(size_t)li];
            CfgField *field = nullptr;
            for (CfgField &f : cs.fields)
                if (iequals(f.key, line.name))
                {
                    field = &f;
                    break;
                }
            if (field == nullptr)
            {
                cs.fields.push_back(CfgField());
                field = &cs.fields.back();
                field->key = line.name;
            }
            field->values.push_back(strip_inline_comment(line.value));
        }
        out.sections.push_back(std::move(cs));
    }
    return out;
}
