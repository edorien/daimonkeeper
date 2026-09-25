/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file cfgc_stack.cpp
 *     See cfgc_stack.h.
 */
#include "pre_inc.h"
#include "cfgc_stack.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include "post_inc.h"

/******************************************************************************/
namespace {

std::string join_path(const std::string &dir, const std::string &file)
{
    if (dir.empty())
        return file;
    const char last = dir[dir.size() - 1];
    return (last == '/' || last == '\\') ? dir + file : dir + "/" + file;
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

std::string join_words(const std::vector<std::string> &words)
{
    std::string out;
    for (size_t i = 0; i < words.size(); i++)
    {
        if (i > 0)
            out += ' ';
        out += words[i];
    }
    return out;
}

bool iequals(const std::string &a, const std::string &b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); i++)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i]))
            return false;
    return true;
}

// The blocks of one layer the loader actually reads for `section_id`, in the order it reads them.
// parse_named_field_blocks() walks every block header in file order but reads the block through
// find_conf_block(), which always returns the FIRST block of that exact spelling; so a block that
// repeats an earlier block's name is never read, and the earlier one is read again instead.
std::vector<const CfgContentSection *> applied_blocks(const ConfigContent &layer, const std::string &section_id)
{
    std::vector<const CfgContentSection *> out;
    for (const CfgContentSection &s : layer.sections)
    {
        if (ConfigStack::canonical_id(s) != section_id)
            continue;
        for (const CfgContentSection &first : layer.sections)
            if (first.name == s.name)
            {
                out.push_back(&first);
                break;
            }
    }
    return out;
}

} // namespace

const char *cfgc_layer_name(CfgLayer layer)
{
    switch (layer)
    {
    case CfgLayer_Base: return "base";
    case CfgLayer_Campaign: return "campaign";
    case CfgLayer_Level: return "level";
    default: return "?";
    }
}

std::string ConfigTarget::path_for(const std::string &file_name, CfgLayer layer, bool creature_model) const
{
    switch (layer)
    {
    case CfgLayer_Base:
        return base_dir.empty() && base_crtr_dir.empty() ? std::string()
            : join_path(creature_model ? base_crtr_dir : base_dir, file_name);
    case CfgLayer_Campaign:
    {
        const std::string &dir = creature_model ? campaign_crtr_dir : campaign_cfg_dir;
        return dir.empty() ? std::string() : join_path(dir, file_name);
    }
    case CfgLayer_Level:
    {
        if (level_dir.empty() || level_number < 0)
            return std::string();
        char prefix[32];
        std::snprintf(prefix, sizeof(prefix), "map%05lld.", (long long)level_number);
        return join_path(level_dir, prefix + file_name);
    }
    default:
        return std::string();
    }
}

std::string ConfigStack::canonical_id(const CfgContentSection &s)
{
    return s.index >= 0 ? s.basename + std::to_string(s.index) : s.name;
}

void ConfigStack::set_layer(CfgLayer layer, ConfigContent content)
{
    layers_[layer] = std::move(content);
    present_[layer] = true;
}

bool ConfigStack::load_layer(CfgLayer layer, const std::string &path)
{
    present_[layer] = false;
    layers_[layer] = ConfigContent();
    if (path.empty())
        return false;
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return false;
    std::stringstream ss;
    ss << f.rdbuf();
    layers_[layer] = read_config_content(ConfigDocument::parse(ss.str()), kind_, layer != CfgLayer_Base);
    present_[layer] = true;
    return true;
}

ConfigStack ConfigStack::load(const ConfigTarget &target, const std::string &kind, const std::string &file_name,
    const CfgFileSchema *schema, bool creature_model)
{
    ConfigStack stack(schema);
    stack.kind_ = kind;
    for (int l = 0; l < CfgLayer_Count; l++)
        stack.load_layer((CfgLayer)l, target.path_for(file_name, (CfgLayer)l, creature_model));
    return stack;
}

const CfgSectionSpec *ConfigStack::spec_for(const std::string &section_id) const
{
    if (schema_ == nullptr)
        return nullptr;
    std::string basename;
    int64_t index;
    cfgc_split_section_name(section_id, basename, index);
    const CfgSectionSpec *s = schema_->find_section(basename);
    if (s == nullptr && index >= 0)
        s = schema_->find_section(section_id);
    return s;
}

std::vector<std::string> ConfigStack::section_ids() const
{
    std::vector<std::string> out;
    for (int l = 0; l < CfgLayer_Count; l++)
        for (const CfgContentSection &s : layers_[l].sections)
        {
            if (cfgc_is_banner_section(s.name))
                continue;
            const std::string id = canonical_id(s);
            if (std::find(out.begin(), out.end(), id) == out.end())
                out.push_back(id);
        }
    return out;
}

std::vector<std::string> ConfigStack::numbered_section_ids(const std::string &basename) const
{
    std::vector<std::pair<int64_t, std::string>> found;
    for (int l = 0; l < CfgLayer_Count; l++)
        for (const CfgContentSection &s : layers_[l].sections)
            if (s.index >= 0 && s.basename == basename)
            {
                const std::string id = canonical_id(s);
                bool seen = false;
                for (const auto &f : found)
                    seen = seen || f.second == id;
                if (!seen)
                    found.emplace_back(s.index, id);
            }
    std::sort(found.begin(), found.end());
    std::vector<std::string> out;
    for (const auto &f : found)
        out.push_back(f.second);
    return out;
}

int64_t ConfigStack::section_count(const std::string &basename) const
{
    int64_t count = 0;
    for (int l = 0; l < CfgLayer_Count; l++)
        for (const CfgContentSection &s : layers_[l].sections)
            if (s.index >= 0 && s.basename == basename)
                count = std::max(count, s.index + 1);
    return count;
}

std::vector<CfgLayer> ConfigStack::layers_defining(const std::string &section_id) const
{
    std::vector<CfgLayer> out;
    for (int l = 0; l < CfgLayer_Count; l++)
        for (const CfgContentSection &s : layers_[l].sections)
            if (canonical_id(s) == section_id)
            {
                out.push_back((CfgLayer)l);
                break;
            }
    return out;
}

bool ConfigStack::merge_upto(const std::string &section_id, const std::string &key, int upto,
    std::vector<std::string> &values, int &top_layer) const
{
    const CfgSectionSpec *sec = spec_for(section_id);
    const CfgFieldSpec *field = sec != nullptr ? sec->find_field(key) : nullptr;

    if (sec != nullptr && sec->list_replaces)
    {
        // The highest layer that has the block owns the whole list.
        for (int l = upto; l >= 0; l--)
        {
            bool has_block = false;
            std::vector<std::string> list;
            // The list parsers use find_conf_block(): only the first block of the name is read.
            for (const CfgContentSection *s : applied_blocks(layers_[l], section_id))
            {
                has_block = true;
                const CfgField *f = s->find_field(key);
                if (f != nullptr)
                    list = f->values;
                break;
            }
            if (has_block)
            {
                values = list;
                top_layer = l;
                return !list.empty();
            }
        }
        return false;
    }

    const bool overlay = field != nullptr && !field->whole_string && field->parts.size() > 1;
    bool any = false;
    std::string current;
    top_layer = -1;
    for (int l = 0; l <= upto; l++)
        for (const CfgContentSection *s : applied_blocks(layers_[l], section_id))
        {
            const CfgField *f = s->find_field(key);
            if (f == nullptr)
                continue;
            for (const std::string &v : f->values)
            {
                if (overlay && any)
                {
                    std::vector<std::string> words = split_words(current);
                    const std::vector<std::string> add = split_words(v);
                    if (add.size() > words.size())
                        words.resize(add.size());
                    for (size_t i = 0; i < add.size(); i++)
                        words[i] = add[i];
                    current = join_words(words);
                }
                else
                {
                    current = v;
                }
                any = true;
                top_layer = l;
            }
        }
    if (any)
        values.assign(1, current);
    return any;
}

bool ConfigStack::effective(const std::string &section_id, const std::string &key, CfgEffective &out) const
{
    out = CfgEffective();
    int top = -1;
    if (!merge_upto(section_id, key, CfgLayer_Count - 1, out.values, top))
        return false;
    out.source = (CfgLayer)top;
    if (top > 0)
    {
        int below_top = -1;
        std::vector<std::string> below;
        if (merge_upto(section_id, key, top - 1, below, below_top))
        {
            out.has_beneath = true;
            out.beneath = below;
        }
    }
    return true;
}

std::vector<std::string> ConfigStack::keys_of(const std::string &section_id) const
{
    std::vector<std::string> out;
    for (int l = 0; l < CfgLayer_Count; l++)
        for (const CfgContentSection &s : layers_[l].sections)
        {
            if (canonical_id(s) != section_id)
                continue;
            for (const CfgField &f : s.fields)
            {
                bool seen = false;
                for (const std::string &k : out)
                    seen = seen || iequals(k, f.key);
                if (!seen)
                    out.push_back(f.key);
            }
        }
    return out;
}

void ConfigStack::collect_names(CfgNameSets &names) const
{
    for (int l = 0; l < CfgLayer_Count; l++)
        cfgc_collect_names(layers_[l], names);
}
