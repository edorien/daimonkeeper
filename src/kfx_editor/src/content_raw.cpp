/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file content_raw.cpp
 *     See content_raw.h.
 */
#include "pre_inc.h"
#include "content_raw.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "cfgc_writebatch.h"
#include "post_inc.h"

namespace fs = std::filesystem;

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

std::string kind_of_file(const std::string &file_name, bool creature_model)
{
    if (creature_model)
        return "creaturemodel";
    const std::string n = lower(file_name);
    for (const CfgFileSchema &f : engine_schema().files)
        if (lower(f.file_name) == n)
            return f.kind;
    return std::string();
}

void list_dir(std::vector<RawFileEntry> &out, const std::string &dir, const char *prefix, bool creature)
{
    if (dir.empty())
        return;
    std::error_code ec;
    std::vector<RawFileEntry> found;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
    {
        if (!it->is_regular_file(ec))
            continue;
        const std::string ext = lower(it->path().extension().string());
        if (ext != ".cfg" && (creature || ext != ".toml"))
            continue;
        RawFileEntry e;
        e.file_name = it->path().filename().string();
        e.name = std::string(prefix) + e.file_name;
        e.creature_model = creature;
        e.kind = kind_of_file(e.file_name, creature);
        found.push_back(std::move(e));
    }
    std::sort(found.begin(), found.end(), [](const RawFileEntry &a, const RawFileEntry &b) { return lower(a.name) < lower(b.name); });
    out.insert(out.end(), found.begin(), found.end());
}

} // namespace

std::vector<RawFileEntry> content_raw_list_files(const ConfigTarget &target)
{
    std::vector<RawFileEntry> out;
    list_dir(out, target.base_dir, "", false);
    list_dir(out, target.base_crtr_dir, "creatrs/", true);
    return out;
}

const CfgFileSchema *RawConfigSession::schema() const
{
    return file_.kind.empty() ? nullptr : engine_schema().find(file_.kind);
}

bool RawConfigSession::open(const ConfigTarget &target, const RawFileEntry &file, CfgLayer layer)
{
    open_ = false;
    target_ = target;
    file_ = file;
    layer_ = layer;
    path_ = target.path_for(file.file_name, layer, file.creature_model);
    original_.clear();
    exists_ = !path_.empty() && read_file(path_, original_);
    text_ = original_;
    open_ = !path_.empty();
    return open_;
}

ConfigStack RawConfigSession::build_stack() const
{
    ConfigStack stack(schema());
    for (int l = 0; l < (int)layer_; l++)
    {
        std::string bytes;
        const std::string p = target_.path_for(file_.file_name, (CfgLayer)l, file_.creature_model);
        if (p.empty() || !read_file(p, bytes))
            continue;
        stack.set_layer((CfgLayer)l, read_config_content(ConfigDocument::parse(bytes), file_.kind, l != CfgLayer_Base));
    }
    stack.set_layer(layer_, read_config_content(ConfigDocument::parse(text_), file_.kind, layer_ != CfgLayer_Base));
    return stack;
}

std::vector<CfgDiagnostic> RawConfigSession::validate() const
{
    const ConfigDocument doc = ConfigDocument::parse(text_);
    CfgNameSets names;
    build_stack().collect_names(names);
    return cfgc_validate_document(doc, schema(), &names);
}

std::vector<RawEffectiveRow> RawConfigSession::effective_rows(bool only_layer) const
{
    std::vector<RawEffectiveRow> rows;
    const ConfigStack stack = build_stack();
    const ConfigContent &mine = stack.layer(layer_);
    auto join = [](const std::vector<std::string> &v) {
        std::string s;
        for (size_t i = 0; i < v.size(); i++)
            s += (i ? " ; " : "") + v[i];
        return s;
    };
    std::vector<std::string> ids;
    if (only_layer)
    {
        for (const CfgContentSection &s : mine.sections)
        {
            const std::string id = ConfigStack::canonical_id(s);
            if (!cfgc_is_banner_section(s.name) && std::find(ids.begin(), ids.end(), id) == ids.end())
                ids.push_back(id);
        }
    }
    else
        ids = stack.section_ids();
    for (const std::string &id : ids)
        for (const std::string &key : stack.keys_of(id))
        {
            CfgEffective e;
            if (!stack.effective(id, key, e))
                continue;
            RawEffectiveRow r;
            r.section = id;
            r.key = key;
            r.value = join(e.values);
            r.source = e.source;
            r.has_beneath = e.has_beneath;
            r.beneath = join(e.beneath);
            r.in_layer = (e.source == layer_);
            if (only_layer && !r.in_layer)
            {
                // A key the text sets that is then overridden by a higher layer cannot occur (nothing sits above);
                // keys the text does not set are skipped here.
                continue;
            }
            rows.push_back(std::move(r));
        }
    return rows;
}

bool RawConfigSession::apply(std::string *error)
{
    if (!writable())
    {
        if (error != nullptr)
            *error = layer_ == CfgLayer_Base ? "Base files are never edited." : "This target has no file for that layer.";
        return false;
    }
    WriteBatch batch;
    if (text_.empty())
    {
        if (exists_)
            batch.remove(path_);
    }
    else if (!exists_ || text_ != original_)
        batch.put(path_, text_);
    std::string err;
    if (!batch.commit(&err))
    {
        if (error != nullptr)
            *error = err;
        return false;
    }
    original_ = text_;
    exists_ = !text_.empty();
    return true;
}

bool RawConfigSession::delete_file(std::string *error)
{
    if (!writable() || !exists_)
    {
        if (error != nullptr)
            *error = "There is no file to delete.";
        return false;
    }
    WriteBatch batch;
    batch.remove(path_);
    std::string err;
    if (!batch.commit(&err))
    {
        if (error != nullptr)
            *error = err;
        return false;
    }
    exists_ = false;
    original_.clear(); // the text stays as an unsaved file
    return true;
}

std::vector<std::string> RawConfigSession::known_keys() const
{
    std::vector<std::string> out;
    const CfgFileSchema *s = schema();
    if (s == nullptr)
        return out;
    for (const CfgSectionSpec &sec : s->sections)
        for (const CfgFieldSpec &f : sec.fields)
            out.push_back(f.key);
    return out;
}
