/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file cfgc_schema.cpp
 *     See cfgc_schema.h.
 */
#include "pre_inc.h"
#include "cfgc_schema.h"
#include "cfgc_content.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include "post_inc.h"

/******************************************************************************/
namespace {

std::string upper(std::string s)
{
    for (char &c : s)
        c = (char)std::toupper((unsigned char)c);
    return s;
}

bool iequals(const std::string &a, const std::string &b)
{
    return upper(a) == upper(b);
}

// The loader's parameter_is_number(): optional '-', then digits.
bool is_number(const std::string &s)
{
    size_t i = 0;
    if (i < s.size() && s[i] == '-')
        i++;
    if (i >= s.size())
        return false;
    for (; i < s.size(); i++)
        if (!std::isdigit((unsigned char)s[i]))
            return false;
    return true;
}

std::vector<std::string> split_words(const std::string &text)
{
    std::vector<std::string> out;
    size_t i = 0;
    while (i < text.size())
    {
        while (i < text.size() && (text[i] == ' ' || text[i] == '\t'))
            i++;
        size_t j = i;
        while (j < text.size() && text[j] != ' ' && text[j] != '\t')
            j++;
        if (j > i)
            out.push_back(text.substr(i, j - i));
        i = j;
    }
    return out;
}

bool name_known(const CfgValueSpec &part, const CfgNameSets *names, const std::string &word, bool &checkable)
{
    checkable = false;
    // Static names first (a registry part may also list literal extras such as NULL), then the registry.
    if (!part.enum_names.empty())
    {
        checkable = true;
        for (const std::string &n : part.enum_names)
            if (iequals(n, word))
                return true;
        if (part.enum_registry.empty())
            return false;
    }
    if (!part.enum_registry.empty() && names != nullptr)
    {
        const std::set<std::string> *set = names->find(part.enum_registry);
        if (set != nullptr)
        {
            checkable = true;
            return set->count(upper(word)) > 0;
        }
        checkable = false;
    }
    return false;
}

void add(std::vector<CfgDiagnostic> &out, CfgSeverity sev, const char *code, const std::string &msg)
{
    CfgDiagnostic d;
    d.severity = sev;
    d.code = code;
    d.message = msg;
    out.push_back(std::move(d));
}

void validate_part(const CfgFieldSpec &spec, const CfgValueSpec &part, const std::string &word, const CfgNameSets *names,
    std::vector<CfgDiagnostic> &out)
{
    switch (part.kind)
    {
    case CfgKind_Number:
    case CfgKind_Coord:
    case CfgKind_Enum:
    {
        if (is_number(word))
        {
            if (part.kind == CfgKind_Enum)
                return;
            const long long v = std::atoll(word.c_str());
            if (v < part.min)
                add(out, CfgSev_Warning, "number_range",
                    spec.key + ": " + word + " is below the minimum " + std::to_string(part.min) + " and would be raised to it");
            else if (v > part.max)
                add(out, CfgSev_Warning, "number_range",
                    spec.key + ": " + word + " is above the maximum " + std::to_string(part.max) + " and would be lowered to it");
            return;
        }
        bool checkable = false;
        const bool known = name_known(part, names, word, checkable);
        const bool has_names = !part.enum_names.empty() || !part.enum_registry.empty();
        if (checkable && !known)
            add(out, CfgSev_Warning, "unknown_name", spec.key + ": '" + word + "' is not a known name");
        else if (!has_names)
            add(out, CfgSev_Warning, "not_a_number", spec.key + ": expected a number, got '" + word + "'");
        return;
    }
    case CfgKind_Flags:
    {
        if (is_number(word) || iequals(word, "none") || word == "0" || word == "1")
            return;
        bool checkable = false;
        if (!name_known(part, names, word, checkable) && checkable)
            add(out, CfgSev_Warning, "unknown_name", spec.key + ": '" + word + "' is not a known flag");
        return;
    }
    default:
        return;
    }
}

} // namespace

const CfgFieldSpec *CfgSectionSpec::find_field(const std::string &key) const
{
    for (const CfgFieldSpec &f : fields)
        if (iequals(f.key, key))
            return &f;
    return nullptr;
}

CfgFieldSpec *CfgSectionSpec::find_field(const std::string &key)
{
    return const_cast<CfgFieldSpec *>(static_cast<const CfgSectionSpec *>(this)->find_field(key));
}

const CfgSectionSpec *CfgFileSchema::find_section(const std::string &basename) const
{
    for (const CfgSectionSpec &s : sections)
        if (s.basename == basename)
            return &s;
    return nullptr;
}

CfgSectionSpec *CfgFileSchema::find_section(const std::string &basename)
{
    return const_cast<CfgSectionSpec *>(static_cast<const CfgFileSchema *>(this)->find_section(basename));
}

const CfgFileSchema *ConfigSchema::find(const std::string &kind) const
{
    for (const CfgFileSchema &f : files)
        if (f.kind == kind)
            return &f;
    return nullptr;
}

CfgFileSchema *ConfigSchema::find(const std::string &kind)
{
    return const_cast<CfgFileSchema *>(static_cast<const ConfigSchema *>(this)->find(kind));
}

bool cfgc_is_banner_section(const std::string &name)
{
    return !name.empty() && name[0] == '#';
}

void CfgNameSets::add(const std::string &registry, const std::string &name)
{
    for (auto &r : registries)
        if (r.first == registry)
        {
            r.second.insert(upper(name));
            return;
        }
    registries.emplace_back(registry, std::set<std::string>{upper(name)});
}

void CfgNameSets::set(const std::string &registry, const std::vector<std::string> &list)
{
    std::set<std::string> fresh;
    for (const std::string &n : list)
        fresh.insert(upper(n));
    for (auto &r : registries)
        if (r.first == registry)
        {
            r.second = fresh;
            return;
        }
    registries.emplace_back(registry, fresh);
}

const std::set<std::string> *CfgNameSets::find(const std::string &registry) const
{
    for (const auto &r : registries)
        if (r.first == registry)
            return &r.second;
    return nullptr;
}

void cfgc_validate_value(const CfgFieldSpec &spec, const std::string &text, const CfgNameSets *names,
    std::vector<CfgDiagnostic> &out)
{
    if (spec.state == CfgState_Ignored)
    {
        add(out, CfgSev_Info, "ignored_key", spec.key + " has no effect in this build" + (spec.note.empty() ? "" : ": " + spec.note));
        return;
    }
    if (spec.whole_string || spec.parts.empty())
        return;
    const std::vector<std::string> words = split_words(text);
    if (words.empty())
    {
        // Flag lists, names, icons and functions may be left empty (the loader reads "nothing");
        // a number needs a value.
        const CfgFieldKind k = spec.parts[0].kind;
        if (k == CfgKind_Number || k == CfgKind_Coord || k == CfgKind_Enum)
            add(out, CfgSev_Warning, "missing_value", spec.key + " has no value");
        return;
    }
    if (words.size() < spec.parts.size())
        add(out, CfgSev_Warning, "too_few_values",
            spec.key + ": expected " + std::to_string(spec.parts.size()) + " values, got " + std::to_string(words.size()));
    for (size_t i = 0; i < words.size() && i < spec.parts.size(); i++)
        validate_part(spec, spec.parts[i], words[i], names, out);
    if (spec.repeat_last && spec.parts.size() > 1)
        for (size_t i = spec.parts.size(); i < words.size(); i++)
            validate_part(spec, spec.parts.back(), words[i], names, out);
    // A flag list is one part that takes every remaining word.
    if (spec.parts.size() == 1 && spec.parts[0].kind == CfgKind_Flags)
        for (size_t i = 1; i < words.size(); i++)
            validate_part(spec, spec.parts[0], words[i], names, out);
}

void cfgc_collect_names(const ConfigContent &content, CfgNameSets &names)
{
    // Blocks whose Name defines a name: basename -> registry (creature.cfg's [jobN] is the "creaturejob" registry).
    static const char *const registries[][2] = {{"trap", "trap"}, {"door", "door"}, {"slab", "slab"}, {"room", "room"},
        {"object", "object"}, {"shot", "shot"}, {"spell", "spell"}, {"power", "power"}, {"special", "special"},
        {"instance", "instance"}, {"job", "creaturejob"}, {"angerjob", "angerjob"}, {"attackpref", "attackpref"}};
    for (const CfgContentSection &s : content.sections)
    {
        if (s.index < 0)
            continue;
        const char *registry = nullptr;
        for (const auto &r : registries)
            if (s.basename == r[0])
                registry = r[1];
        if (registry == nullptr)
            continue;
        const std::string *name = s.last_value("Name");
        if (name != nullptr && !name->empty())
            names.add(registry, *name);
    }
}

void cfgc_collect_creature_names(const ConfigContent &content, CfgNameSets &names)
{
    const CfgContentSection *common = content.find_section("common");
    if (common == nullptr)
        return;
    const std::string *list = common->last_value("Creatures");
    if (list == nullptr)
        return;
    // A layer that has the list replaces the one beneath (the loader clears its creature table first).
    names.set("creature", split_words(*list));
}
