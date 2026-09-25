/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file cfgc_validate.cpp
 *     See cfgc_validate.h.
 */
#include "pre_inc.h"
#include "cfgc_validate.h"
#include "cfgc_content.h"

#include <set>
#include "post_inc.h"

/******************************************************************************/
namespace {

const CfgSectionSpec *spec_for(const CfgFileSchema *schema, const std::string &name)
{
    if (schema == nullptr)
        return nullptr;
    std::string base;
    int64_t index;
    cfgc_split_section_name(name, base, index);
    const CfgSectionSpec *s = schema->find_section(base);
    if (s == nullptr && index >= 0)
        s = schema->find_section(name);
    return s;
}

std::string id_of(const std::string &name)
{
    std::string base;
    int64_t index;
    cfgc_split_section_name(name, base, index);
    return index >= 0 ? base + std::to_string(index) : name;
}

bool only_control_or_blank(const std::string &s)
{
    for (char c : s)
        if (c != ' ' && c != '\t' && c != '\r' && (unsigned char)c >= 32)
            return false;
    return true; // blanks, ^Z and other control bytes: nothing to report
}

} // namespace

std::vector<CfgDiagnostic> cfgc_validate_document(const ConfigDocument &doc, const CfgFileSchema *schema,
    const CfgNameSets *names)
{
    std::vector<CfgDiagnostic> out;
    auto add = [&](CfgSeverity sev, const char *code, size_t line0, const std::string &section, const std::string &key,
                   const std::string &msg) {
        CfgDiagnostic d;
        d.severity = sev;
        d.code = code;
        d.message = msg;
        d.line = (int64_t)line0 + 1;
        d.section = section;
        d.key = key;
        out.push_back(std::move(d));
    };

    std::set<std::string> seen_headers;
    bool in_block = false;
    bool banner = false;
    std::string block_name;
    const CfgSectionSpec *spec = nullptr;
    bool spec_missing = false;

    for (size_t i = 0; i < doc.lines().size(); i++)
    {
        const CfgLine &l = doc.lines()[i];
        switch (l.kind)
        {
        case CfgLine_Section:
        {
            in_block = true;
            block_name = l.name;
            banner = cfgc_is_banner_section(l.name);
            spec = nullptr;
            spec_missing = false;
            if (banner)
                break; // a comment written as a header
            if (!seen_headers.insert(l.name).second)
            {
                add(CfgSev_Warning, "duplicate_block_ignored", i, id_of(l.name), "",
                    "[" + l.name + "] repeats an earlier block's name; the loader never reads this one");
                spec_missing = true; // do not judge its keys
                break;
            }
            spec = spec_for(schema, l.name);
            if (schema != nullptr && spec == nullptr)
            {
                add(CfgSev_Warning, "unknown_section", i, id_of(l.name), "", "[" + l.name + "] is not a block of this file kind");
                spec_missing = true;
            }
            break;
        }
        case CfgLine_Key:
        {
            if (!in_block)
            {
                add(CfgSev_Warning, "key_outside_block", i, "", l.name, l.name + " is before the first [block]; the loader skips it");
                break;
            }
            if (banner || spec_missing || schema == nullptr || spec == nullptr)
                break;
            const CfgFieldSpec *f = spec->find_field(l.name);
            if (f == nullptr)
            {
                add(CfgSev_Warning, "unknown_key", i, id_of(block_name), l.name,
                    "[" + block_name + "] " + l.name + " is not a key of this block");
                break;
            }
            // Text without the inline comment, as the loader reads it.
            const std::string value = l.text.substr(l.value_begin, l.value_end - l.value_begin);
            std::vector<CfgDiagnostic> found;
            cfgc_validate_value(*f, value, names, found);
            for (CfgDiagnostic &d : found)
            {
                d.line = (int64_t)i + 1;
                d.section = id_of(block_name);
                d.key = l.name;
                out.push_back(std::move(d));
            }
            break;
        }
        case CfgLine_Other:
            if (!only_control_or_blank(l.text))
                add(CfgSev_Info, "unrecognised_line", i, in_block ? id_of(block_name) : "", "",
                    "not a key, block or comment; the loader skips this line");
            break;
        default:
            break;
        }
    }
    return out;
}
