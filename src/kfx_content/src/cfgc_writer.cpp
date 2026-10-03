/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file cfgc_writer.cpp
 *     See cfgc_writer.h.
 */
#include "pre_inc.h"
#include "cfgc_writer.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include "post_inc.h"

/******************************************************************************/
namespace {

const char *const kGeneratedMarker = "written by the map editor";

bool iequals(const std::string &a, const std::string &b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); i++)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i]))
            return false;
    return true;
}

std::string trim(const std::string &s)
{
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t'))
        b++;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r'))
        e--;
    return s.substr(b, e - b);
}

std::string join(const std::vector<std::string> &v)
{
    std::string out;
    for (size_t i = 0; i < v.size(); i++)
    {
        if (i > 0)
            out += ' ';
        out += v[i];
    }
    return out;
}

// The id the loader would give the block named `name` ("trap02" -> "trap2").
std::string id_of_name(const std::string &name)
{
    std::string base;
    int64_t index;
    cfgc_split_section_name(name, base, index);
    return index >= 0 ? base + std::to_string(index) : name;
}

// The header text for a block id ("trap2" stays "trap2").
int64_t find_block(const ConfigDocument &doc, const std::string &id)
{
    for (size_t i = 0; i < doc.sections().size(); i++)
    {
        const CfgSection &s = doc.sections()[i];
        if (s.header_line >= 0 && id_of_name(s.name) == id)
            return (int64_t)i;
    }
    return -1;
}

std::vector<int64_t> block_key_lines(const ConfigDocument &doc, int64_t block, const std::string &key)
{
    std::vector<int64_t> out;
    for (int64_t li : doc.key_lines(block))
        if (iequals(doc.lines()[(size_t)li].name, key))
            out.push_back(li);
    return out;
}

// The spelling of `key` the file already uses (any block), else the caller's.
std::string spelling_of(const ConfigDocument &doc, const std::string &key)
{
    for (const CfgLine &l : doc.lines())
        if (l.kind == CfgLine_Key && iequals(l.name, key))
            return l.name;
    return key;
}

// "Key = value" laid out like the block's last key line: same indent, same "=" or blank
// separator, value in the same column.
std::string make_key_line(const ConfigDocument &doc, int64_t block, const std::string &key, const std::string &value)
{
    const std::vector<int64_t> keys = doc.key_lines(block);
    if (keys.empty())
        return key + " = " + value;
    const CfgLine &n = doc.lines()[(size_t)keys.back()];
    size_t indent = 0;
    while (indent < n.text.size() && (n.text[indent] == ' ' || n.text[indent] == '\t'))
        indent++;
    std::string line = n.text.substr(0, indent) + key;
    const size_t column = n.value_begin;
    if (n.has_equals)
    {
        // Key padded so the "=" lines up with the neighbour's, then one blank.
        size_t equals_col = column >= 2 ? column - 2 : 0;
        // The neighbour may have a single space around "="; only pad when the value column is beyond that.
        if (line.size() + 1 <= equals_col)
            line += std::string(equals_col - line.size(), ' ');
        else
            line += " ";
        line += "= ";
    }
    else
    {
        if (line.size() < column)
            line += std::string(column - line.size(), ' ');
        else
            line += " ";
    }
    return line + value;
}

std::string eol_of(const ConfigDocument &doc)
{
    return doc.dominant_eol();
}

class Applier
{
public:
    Applier(const ConfigContentWriter &w, ConfigDocument &doc, const ConfigStack *lower, ChangeResult &res)
        : w_(w), doc_(doc), lower_(lower), res_(res)
    {
        if (lower_ != nullptr)
            lower_->collect_names(names_);
        cfgc_collect_names(read_config_content(doc_, "", true), names_);
    }

    void run(const ChangeSet &cs)
    {
        for (const CfgChange &c : cs.changes)
            if (c.section.empty() || (c.kind != CfgChange::ResetSection && c.key.empty()))
            {
                res_.ok = false;
                add_diag(CfgSev_Error, "malformed_change", "a change needs a section and a key");
                return;
            }
        for (const CfgChange &c : cs.changes)
        {
            const std::string before = doc_.serialize();
            switch (c.kind)
            {
            case CfgChange::Set: do_set(c); break;
            case CfgChange::Reset: do_reset(c.section, c.key); break;
            case CfgChange::ResetSection: do_reset_section(c.section); break;
            case CfgChange::ReplaceList: do_replace_list(c); break;
            }
            if (doc_.serialize() == before)
                res_.unchanged++;
            else
                res_.applied++;
        }
    }

private:
    const CfgSectionSpec *spec_of(const std::string &id) const
    {
        const CfgFileSchema *s = w_.schema();
        if (s == nullptr)
            return nullptr;
        std::string base;
        int64_t index;
        cfgc_split_section_name(id, base, index);
        const CfgSectionSpec *sec = s->find_section(base);
        if (sec == nullptr && index >= 0)
            sec = s->find_section(id);
        return sec;
    }

    void add_diag(CfgSeverity sev, const char *code, const std::string &msg)
    {
        CfgDiagnostic d;
        d.severity = sev;
        d.code = code;
        d.message = msg;
        res_.diagnostics.push_back(std::move(d));
    }

    // Reports unknown blocks/keys and what the loader would clamp; never blocks the write.
    const CfgFieldSpec *check(const std::string &id, const std::string &key, const std::vector<std::string> &values)
    {
        const CfgSectionSpec *sec = spec_of(id);
        if (w_.schema() != nullptr && sec == nullptr)
        {
            add_diag(CfgSev_Warning, "unknown_section", "[" + id + "] is not a block of this file kind");
            return nullptr;
        }
        const CfgFieldSpec *f = sec != nullptr ? sec->find_field(key) : nullptr;
        if (sec != nullptr && f == nullptr)
        {
            add_diag(CfgSev_Warning, "unknown_key", "[" + id + "] " + key + " is not a known key");
            return nullptr;
        }
        if (f != nullptr)
            for (const std::string &v : values)
                cfgc_validate_value(*f, v, &names_, res_.diagnostics);
        return f;
    }

    std::string effective_beneath(const std::string &id, const std::string &key, bool &has) const
    {
        has = false;
        if (lower_ == nullptr)
            return std::string();
        CfgEffective e;
        if (!lower_->effective(id, key, e) || e.values.size() != 1)
            return std::string();
        has = true;
        return e.values[0];
    }

    // Makes sure the block exists, appending it (with Name first for numbered blocks; list blocks seeded
    // with the lower layers' lines, since a layer's block replaces theirs whole).
    int64_t ensure_block(const std::string &id, const std::string &exclude_key)
    {
        int64_t b = find_block(doc_, id);
        if (b >= 0)
            return b;
        const std::string eol = eol_of(doc_);
        if (!doc_.lines().empty())
        {
            const CfgLine &last = doc_.lines().back();
            if (last.kind != CfgLine_Blank)
                doc_.insert_line(doc_.lines().size(), "", eol);
        }
        std::string header = id;
        {
            // A campaign file names its level blocks with five digits ("map00007"), as the game's own files do.
            std::string base;
            int64_t index;
            cfgc_split_section_name(id, base, index);
            if (w_.schema() != nullptr && w_.schema()->kind == "campaign" && base == "map" && index >= 0)
            {
                char num[32];
                snprintf(num, sizeof(num), "%05lld", (long long)index);
                header = base + num;
            }
        }
        doc_.insert_line(doc_.lines().size(), "[" + header + "]", eol);
        b = find_block(doc_, id);
        const CfgSectionSpec *sec = spec_of(id);
        if (lower_ == nullptr)
            return b;
        if (sec != nullptr && sec->list_replaces)
        {
            for (const std::string &k : lower_->keys_of(id))
            {
                if (iequals(k, exclude_key))
                    continue;
                CfgEffective e;
                if (!lower_->effective(id, k, e))
                    continue;
                for (const std::string &v : e.values)
                    append_key(b, k, v);
            }
        }
        else
        {
            std::string base;
            int64_t index;
            cfgc_split_section_name(id, base, index);
            CfgEffective e;
            if (index >= 0 && !iequals(exclude_key, "Name") && lower_->effective(id, "Name", e) && e.values.size() == 1)
                append_key(b, "Name", e.values[0]);
        }
        return b;
    }

    // Appends a key line after the block's last key line (or right under the header).
    void append_key(int64_t &block, const std::string &key, const std::string &value)
    {
        const std::vector<int64_t> keys = doc_.key_lines(block);
        const size_t at = keys.empty() ? (size_t)doc_.sections()[(size_t)block].header_line + 1 : (size_t)keys.back() + 1;
        const std::string id = id_of_name(doc_.sections()[(size_t)block].name);
        doc_.insert_line(at, make_key_line(doc_, block, spelling_of(doc_, key), value), eol_of(doc_));
        block = find_block(doc_, id);
    }

    void do_set(const CfgChange &c)
    {
        const std::string value = w_.format_value(check(c.section, c.key, {join(c.values)}), join(c.values));
        const CfgSectionSpec *sec = spec_of(c.section);
        const bool list_block = sec != nullptr && sec->list_replaces;
        bool has = false;
        const std::string beneath = effective_beneath(c.section, c.key, has);
        int64_t b = find_block(doc_, c.section);
        std::vector<int64_t> existing = b >= 0 ? block_key_lines(doc_, b, c.key) : std::vector<int64_t>();

        if (!list_block && has && value == beneath)
        {
            // Equal to what shows through: an override would say nothing.
            do_reset(c.section, c.key);
            return;
        }
        if (!existing.empty())
        {
            const CfgLine &l = doc_.lines()[(size_t)existing.back()];
            const std::string text = l.text.substr(0, l.value_begin) + value + l.text.substr(l.value_end);
            // The same words with different spacing ("99  111") are left as the author wrote them.
            auto words = [](const std::string &v) {
                std::istringstream in(v);
                std::vector<std::string> w;
                for (std::string t; in >> t;)
                    w.push_back(t);
                return w;
            };
            if (text != l.text && words(l.text.substr(l.value_begin, l.value_end - l.value_begin)) != words(value))
                doc_.replace_line((size_t)existing.back(), text);
            return;
        }
        b = ensure_block(c.section, c.key);
        append_key(b, c.key, value);
    }

    // Deletes the block when it has nothing left worth keeping.
    void drop_if_empty(const std::string &id)
    {
        const int64_t b = find_block(doc_, id);
        if (b < 0)
            return;
        const CfgSection s = doc_.sections()[(size_t)b];
        const CfgSectionSpec *sec = spec_of(id);
        if (sec != nullptr && sec->list_replaces)
            return; // an empty list block still clears the lower layers' list
        bool only_name = true;
        for (int64_t li : doc_.key_lines(b))
            if (!iequals(doc_.lines()[(size_t)li].name, "Name"))
                only_name = false;
        const bool has_keys = !doc_.key_lines(b).empty();
        if (has_keys && !(only_name && ConfigContentWriter::is_generated(doc_)))
            return;
        for (int64_t i = s.first_line; i < s.end_line; i++)
        {
            const CfgLine &l = doc_.lines()[(size_t)i];
            if (l.kind == CfgLine_Comment || l.kind == CfgLine_Other)
                return; // authored text stays
        }
        for (int64_t i = s.end_line - 1; i >= s.header_line; i--)
            doc_.erase_line((size_t)i);
    }

    void do_reset(const std::string &id, const std::string &key)
    {
        int64_t b = find_block(doc_, id);
        if (b < 0)
            return;
        const std::vector<int64_t> lines = block_key_lines(doc_, b, key);
        for (size_t i = lines.size(); i-- > 0;)
            doc_.erase_line((size_t)lines[i]);
        drop_if_empty(id);
    }

    void do_reset_section(const std::string &id)
    {
        int64_t b = find_block(doc_, id);
        if (b < 0)
            return;
        const std::vector<int64_t> lines = doc_.key_lines(b);
        for (size_t i = lines.size(); i-- > 0;)
            doc_.erase_line((size_t)lines[i]);
        drop_if_empty(id);
    }

    void do_replace_list(const CfgChange &c)
    {
        check(c.section, c.key, c.values);
        int64_t b = find_block(doc_, c.section);
        size_t at = 0;
        bool placed = false;
        if (b >= 0)
        {
            const std::vector<int64_t> lines = block_key_lines(doc_, b, c.key);
            if (!lines.empty())
            {
                at = (size_t)lines.front();
                placed = true;
                // Identical list: leave the file alone.
                bool same = lines.size() == c.values.size();
                for (size_t i = 0; same && i < lines.size(); i++)
                    same = doc_.lines()[(size_t)lines[i]].value.compare(0, std::string::npos, c.values[i]) == 0;
                if (same)
                    return;
                for (size_t i = lines.size(); i-- > 0;)
                    doc_.erase_line((size_t)lines[i]);
            }
        }
        else
        {
            b = ensure_block(c.section, c.key);
        }
        b = find_block(doc_, c.section);
        if (!placed)
        {
            const std::vector<int64_t> keys = doc_.key_lines(b);
            at = keys.empty() ? (size_t)doc_.sections()[(size_t)b].header_line + 1 : (size_t)keys.back() + 1;
        }
        const std::string key = spelling_of(doc_, c.key);
        for (const std::string &v : c.values)
        {
            b = find_block(doc_, c.section);
            doc_.insert_line(at, make_key_line(doc_, b, key, v), eol_of(doc_));
            at++;
        }
        if (c.values.empty())
            drop_if_empty(c.section);
    }

    const ConfigContentWriter &w_;
    ConfigDocument &doc_;
    const ConfigStack *lower_;
    ChangeResult &res_;
    CfgNameSets names_;
};

} // namespace

ChangeSet &ChangeSet::set(const std::string &section, const std::string &key, const std::string &value)
{
    CfgChange c;
    c.kind = CfgChange::Set;
    c.section = section;
    c.key = key;
    c.values.push_back(value);
    changes.push_back(std::move(c));
    return *this;
}

ChangeSet &ChangeSet::reset(const std::string &section, const std::string &key)
{
    CfgChange c;
    c.kind = CfgChange::Reset;
    c.section = section;
    c.key = key;
    changes.push_back(std::move(c));
    return *this;
}

ChangeSet &ChangeSet::reset_section(const std::string &section)
{
    CfgChange c;
    c.kind = CfgChange::ResetSection;
    c.section = section;
    changes.push_back(std::move(c));
    return *this;
}

ChangeSet &ChangeSet::replace_list(const std::string &section, const std::string &key, const std::vector<std::string> &values)
{
    CfgChange c;
    c.kind = CfgChange::ReplaceList;
    c.section = section;
    c.key = key;
    c.values = values;
    changes.push_back(std::move(c));
    return *this;
}

std::string ConfigContentWriter::format_value(const CfgFieldSpec *, const std::string &text) const
{
    return trim(text);
}

bool ConfigContentWriter::is_generated(const ConfigDocument &doc)
{
    for (const CfgLine &l : doc.lines())
    {
        if (l.kind == CfgLine_Comment)
            return l.text.find(kGeneratedMarker) != std::string::npos;
        if (l.kind != CfgLine_Blank)
            return false;
    }
    return false;
}

bool ConfigContentWriter::has_keys(const ConfigDocument &doc)
{
    for (const CfgLine &l : doc.lines())
        if (l.kind == CfgLine_Key)
            return true;
    return false;
}

ChangeResult ConfigContentWriter::apply(ConfigDocument &doc, const ChangeSet &changes, const ConfigStack *lower) const
{
    ChangeResult res;
    Applier a(*this, doc, lower, res);
    a.run(changes);
    return res;
}

bool ConfigContentWriter::write(const ConfigTarget &target, CfgLayer layer, const std::string &file_name,
    const ChangeSet &changes, WriteBatch &batch, ChangeResult *result, bool creature_model) const
{
    const std::string path = target.path_for(file_name, layer, creature_model);
    if (path.empty())
        return false;

    std::string original;
    bool exists = false;
    {
        std::ifstream f(path, std::ios::binary);
        if (f)
        {
            std::stringstream ss;
            ss << f.rdbuf();
            original = ss.str();
            exists = true;
        }
    }

    // The layers beneath the one being written, for no-op elimination and names.
    ConfigStack lower(schema());
    for (int l = 0; l < (int)layer; l++)
    {
        const std::string p = target.path_for(file_name, (CfgLayer)l, creature_model);
        std::ifstream f(p, std::ios::binary);
        if (p.empty() || !f)
            continue;
        std::stringstream ss;
        ss << f.rdbuf();
        lower.set_layer((CfgLayer)l, read_config_content(ConfigDocument::parse(ss.str()),
            schema() != nullptr ? schema()->kind : "", l != CfgLayer_Base));
    }

    ConfigDocument doc = ConfigDocument::parse(original);
    ChangeResult res = apply(doc, changes, &lower);
    if (result != nullptr)
        *result = res;
    if (!res.ok)
        return false;

    if (!exists)
    {
        if (!has_keys(doc))
            return true; // nothing to create
        doc.insert_line(0, header_line(), doc.dominant_eol());
        doc.insert_line(1, "", doc.dominant_eol());
    }
    else if (!has_keys(doc) && is_generated(doc))
    {
        batch.remove(path); // the editor made it and nothing is left in it
        return true;
    }
    const std::string out = doc.serialize();
    if (!exists || out != original)
        batch.put(path, out);
    return true;
}

TrapDoorConfigWriter::TrapDoorConfigWriter(const ConfigSchema &schema)
    : TableConfigWriter(schema.find("trapdoor"),
          "; KeeperFX Partial Traps and Doors Configuration file version 1.0 -- written by the map editor.")
{
}

RulesConfigWriter::RulesConfigWriter(const ConfigSchema &schema)
    : TableConfigWriter(schema.find("rules"), "; KeeperFX Partial Rules Configuration file version 1.0 -- written by the map editor.")
{
}

std::unique_ptr<ConfigContentWriter> cfgc_make_writer(const ConfigSchema &schema, const std::string &kind)
{
    if (schema.find(kind) == nullptr)
        return nullptr;
    if (kind == "trapdoor")
        return std::unique_ptr<ConfigContentWriter>(new TrapDoorConfigWriter(schema));
    if (kind == "rules")
        return std::unique_ptr<ConfigContentWriter>(new RulesConfigWriter(schema));
    if (kind == "campaign")
        return std::unique_ptr<ConfigContentWriter>(new TableConfigWriter(schema.find(kind),
            "; KeeperFX campaign file -- written by the map editor."));
    const char *title = kind == "objects" ? "Objects" : kind == "terrain" ? "Terrain" : kind == "magic" ? "Magic"
        : kind == "creaturemodel" ? "Creature" : "Configuration";
    return std::unique_ptr<ConfigContentWriter>(new TableConfigWriter(schema.find(kind),
        std::string("; KeeperFX Partial ") + title + " Configuration file version 1.0 -- written by the map editor."));
}
