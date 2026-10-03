/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file content_struct.cpp
 *     See content_struct.h.
 */
#include "pre_inc.h"
#include "content_struct.h"
#include "content_names.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include "post_inc.h"

/******************************************************************************/
namespace {

const ConfigSchema &engine_schema()
{
    static const ConfigSchema s = build_engine_schema();
    return s;
}

std::string lower(std::string s)
{
    for (char &c : s)
        c = (char)std::tolower((unsigned char)c);
    return s;
}

bool read_file(const std::string &path, std::string &out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

std::string join_words(const std::vector<std::string> &v)
{
    std::string s;
    for (size_t i = 0; i < v.size(); i++)
        s += (i ? " " : "") + v[i];
    return s;
}

// What the loader uses when nothing sets the key: the schema's defaults, one per value.
std::string default_text(const CfgFieldSpec *f)
{
    if (f == nullptr || f->parts.empty() || f->whole_string)
        return std::string();
    std::vector<std::string> words;
    for (const CfgValueSpec &p : f->parts)
    {
        if (p.kind != CfgKind_Number && p.kind != CfgKind_Coord && p.kind != CfgKind_Enum)
            return std::string();
        words.push_back(std::to_string((long long)p.default_value));
    }
    return join_words(words);
}

} // namespace

StructuredSession::Key StructuredSession::make_key(const std::string &section, const std::string &key)
{
    return Key(section, lower(key));
}

bool StructuredSession::load_layers()
{
    stack_ = ConfigStack(schema_);
    lower_ = ConfigStack(schema_);
    names_ = CfgNameSets();
    exists_ = false;
    for (int l = 0; l <= (int)layer_; l++)
    {
        const std::string p = target_.path_for(file_name_, (CfgLayer)l, creature_);
        std::string bytes;
        if (p.empty() || !read_file(p, bytes))
            continue;
        if (l == (int)layer_)
            exists_ = true;
        ConfigContent c = read_config_content(ConfigDocument::parse(bytes), kind_, l != CfgLayer_Base);
        if (l < (int)layer_)
            lower_.set_layer((CfgLayer)l, c);
        stack_.set_layer((CfgLayer)l, std::move(c));
    }
    stack_.collect_names(names_);
    // Names other files define (creatures for a sacrifice recipe, spells, objects for a trap's crate, ...).
    const CfgNameSets others = content_collect_names(target_, layer_);
    for (const auto &r : others.registries)
        for (const std::string &n : r.second)
            names_.add(r.first, n);
    return true;
}

bool StructuredSession::open(const ConfigTarget &target, const std::string &kind, const std::string &file_name, CfgLayer layer,
    bool creature_model)
{
    open_ = false;
    pending_.clear();
    target_ = target;
    kind_ = kind;
    file_name_ = file_name;
    creature_ = creature_model;
    layer_ = layer;
    schema_ = engine_schema().find(kind);
    path_ = target.path_for(file_name, layer, creature_model);
    if (schema_ == nullptr || path_.empty())
        return false;
    load_layers();
    open_ = true;
    return true;
}

FieldView StructuredSession::value_of(const std::string &section, const std::string &key) const
{
    FieldView v;
    const CfgSectionSpec *sec = schema_ != nullptr ? schema_->find_section(section) : nullptr;
    if (sec == nullptr)
    {
        std::string base;
        int64_t index;
        cfgc_split_section_name(section, base, index);
        sec = schema_ != nullptr ? schema_->find_section(base) : nullptr;
    }
    const CfgFieldSpec *spec = sec != nullptr ? sec->find_field(key) : nullptr;

    CfgEffective below;
    const bool below_set = lower_.effective(section, key, below) && below.values.size() == 1;
    const auto it = pending_.find(make_key(section, key));
    if (it != pending_.end() && !it->second.is_list)
    {
        v.pending = true;
        if (it->second.is_reset)
        {
            // Back to what shows through.
            if (below_set)
            {
                v.text = below.values[0];
                v.is_set = true;
                v.source = below.source;
            }
            else
                v.text = default_text(spec);
            return v;
        }
        v.text = it->second.values.empty() ? std::string() : it->second.values[0];
        v.is_set = true;
        v.source = layer_;
        v.overridden_here = true;
    }
    else
    {
        CfgEffective e;
        if (stack_.effective(section, key, e) && e.values.size() == 1)
        {
            v.text = e.values[0];
            v.is_set = true;
            v.source = e.source;
            v.overridden_here = (e.source == layer_);
        }
        else
            v.text = default_text(spec);
    }
    if (v.overridden_here && below_set)
    {
        v.has_beneath = true;
        v.beneath = below.values[0];
        v.same_as_beneath = (v.text == v.beneath);
    }
    return v;
}

ListView StructuredSession::list(const std::string &section, const std::string &key) const
{
    ListView v;
    const auto it = pending_.find(make_key(section, key));
    if (it != pending_.end() && it->second.is_list)
    {
        v.pending = true;
        if (it->second.is_reset)
        {
            CfgEffective below;
            if (lower_.effective(section, key, below))
            {
                v.lines = below.values;
                v.is_set = true;
                v.source = below.source;
            }
            return v;
        }
        v.lines = it->second.values;
        v.is_set = true;
        v.source = layer_;
        v.overridden_here = true;
        return v;
    }
    CfgEffective e;
    if (stack_.effective(section, key, e))
    {
        v.lines = e.values;
        v.is_set = true;
        v.source = e.source;
        v.overridden_here = (e.source == layer_);
    }
    return v;
}

bool StructuredSession::section_touched(const std::string &section) const
{
    for (const auto &p : pending_)
        if (p.first.first == section)
            return true;
    for (CfgLayer l : stack_.layers_defining(section))
        if (l == layer_)
            return true;
    return false;
}

void StructuredSession::set(const std::string &section, const std::string &key, const std::string &text)
{
    if (!writable())
        return;
    const Key k = make_key(section, key);
    // Back to what is already there (the file's own value, or the inherited one when the file has none):
    // no edit at all.
    CfgEffective e;
    const bool in_file = stack_.effective(section, key, e) && e.values.size() == 1 && e.source == layer_;
    if (in_file && e.values[0] == text)
    {
        pending_.erase(k);
        return;
    }
    if (!in_file)
    {
        CfgEffective inherited;
        const bool set_below = stack_.effective(section, key, inherited) && inherited.values.size() == 1;
        const CfgSectionSpec *sec = schema_->find_section(section);
        if (sec == nullptr)
        {
            std::string base;
            int64_t index;
            cfgc_split_section_name(section, base, index);
            sec = schema_->find_section(base);
        }
        const std::string current = set_below ? inherited.values[0] : default_text(sec != nullptr ? sec->find_field(key) : nullptr);
        if (current == text)
        {
            pending_.erase(k);
            return;
        }
    }
    Pending p;
    p.key_text = key;
    p.values.push_back(text);
    pending_[k] = p;
}

void StructuredSession::reset(const std::string &section, const std::string &key)
{
    if (!writable())
        return;
    const Key k = make_key(section, key);
    CfgEffective e;
    if (stack_.effective(section, key, e) && e.source == layer_)
    {
        Pending p;
        p.key_text = key;
        p.is_reset = true;
        pending_[k] = p;
    }
    else
        pending_.erase(k);
}

void StructuredSession::set_list(const std::string &section, const std::string &key, const std::vector<std::string> &lines)
{
    if (!writable())
        return;
    const Key k = make_key(section, key);
    CfgEffective e;
    if (stack_.effective(section, key, e) && e.source == layer_ && e.values == lines)
    {
        pending_.erase(k);
        return;
    }
    Pending p;
    p.key_text = key;
    p.is_list = true;
    p.values = lines;
    pending_[k] = p;
}

void StructuredSession::reset_list(const std::string &section, const std::string &key)
{
    if (!writable())
        return;
    const Key k = make_key(section, key);
    CfgEffective e;
    if (stack_.effective(section, key, e) && e.source == layer_)
    {
        Pending p;
        p.key_text = key;
        p.is_list = true;
        p.is_reset = true;
        pending_[k] = p;
    }
    else
        pending_.erase(k);
}

std::string StructuredSession::spelled(const std::string &section, const std::string &key) const
{
    auto find_in = [&](const std::string &id) -> std::string {
        for (const std::string &k : stack_.keys_of(id))
            if (lower(k) == lower(key))
                return k;
        return std::string();
    };
    std::string s = find_in(section);
    if (!s.empty())
        return s;
    std::string base;
    int64_t index;
    cfgc_split_section_name(section, base, index);
    for (const std::string &id : stack_.numbered_section_ids(base))
    {
        s = find_in(id);
        if (!s.empty())
            return s;
    }
    return key;
}

ChangeSet StructuredSession::changes() const
{
    ChangeSet cs;
    for (const auto &p : pending_)
    {
        const std::string &section = p.first.first;
        const Pending &pd = p.second;
        const std::string key = spelled(section, pd.key_text.empty() ? p.first.second : pd.key_text);
        if (pd.is_list)
        {
            if (pd.is_reset)
                cs.reset_section(section); // a list block replaces the lower one whole
            else
                cs.replace_list(section, key, pd.values);
        }
        else if (pd.is_reset)
            cs.reset(section, key);
        else
            cs.set(section, key, pd.values.empty() ? std::string() : pd.values[0]);
    }
    return cs;
}

std::vector<CfgDiagnostic> StructuredSession::diagnostics() const
{
    std::vector<CfgDiagnostic> out;
    for (const auto &p : pending_)
    {
        if (p.second.is_reset)
            continue;
        const std::string &section = p.first.first;
        const CfgSectionSpec *sec = schema_->find_section(section);
        if (sec == nullptr)
        {
            std::string base;
            int64_t index;
            cfgc_split_section_name(section, base, index);
            sec = schema_->find_section(base);
        }
        const CfgFieldSpec *spec = sec != nullptr ? sec->find_field(p.first.second) : nullptr;
        if (spec == nullptr)
            continue;
        for (const std::string &v : p.second.values)
        {
            std::vector<CfgDiagnostic> found;
            cfgc_validate_value(*spec, v, &names_, found);
            for (CfgDiagnostic &d : found)
            {
                d.section = section;
                d.key = spec->key;
                out.push_back(std::move(d));
            }
        }
    }
    return out;
}

bool StructuredSession::apply(std::string *error, size_t *warnings)
{
    if (!writable())
    {
        if (error != nullptr)
            *error = layer_ == CfgLayer_Base ? "Base files are never edited." : "This target has no file for that layer.";
        return false;
    }
    if (pending_.empty())
        return true;
    std::unique_ptr<ConfigContentWriter> w = cfgc_make_writer(engine_schema(), kind_);
    if (!w)
    {
        if (error != nullptr)
            *error = "No writer for this file kind.";
        return false;
    }
    WriteBatch batch;
    ChangeResult res;
    if (!w->write(target_, layer_, file_name_, changes(), batch, &res, creature_))
    {
        if (error != nullptr)
            *error = "The changes could not be prepared.";
        return false;
    }
    std::string err;
    if (!batch.commit(&err))
    {
        if (error != nullptr)
            *error = err;
        return false;
    }
    if (warnings != nullptr)
    {
        *warnings = 0;
        for (const CfgDiagnostic &d : res.diagnostics)
            if (d.severity >= CfgSev_Warning)
                (*warnings)++;
    }
    pending_.clear();
    load_layers();
    return true;
}
