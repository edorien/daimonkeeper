/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file content_strings.cpp
 *     See content_strings.h.
 */
#include "pre_inc.h"
#include "content_strings.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>

#include "cfgc_writebatch.h"
#include "post_inc.h"

namespace fs = std::filesystem;

/******************************************************************************/
namespace {

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

// "\r\n" and "\r" become "\n" for display and editing.
std::string to_lf(const std::string &s)
{
    std::string out;
    for (size_t i = 0; i < s.size(); i++)
    {
        if (s[i] == '\r')
        {
            if (i + 1 < s.size() && s[i + 1] == '\n')
                i++;
            out += '\n';
        }
        else
            out += s[i];
    }
    return out;
}

// The shipped files store line breaks as CR LF.
std::string to_crlf(const std::string &s)
{
    std::string out;
    for (const char c : to_lf(s))
        if (c == '\n')
            out += "\r\n";
        else
            out += c;
    return out;
}

std::string lower(std::string s)
{
    for (char &c : s)
        c = (char)std::tolower((unsigned char)c);
    return s;
}

} // namespace

std::string content_strings_display(const std::string &raw)
{
    return to_lf(cfgc_strings_decode(raw));
}

StringsPaths content_strings_paths(const std::string &root, const ContentCampaign *campaign, const std::string &level_dir,
    int64_t level_number, const std::string &lang)
{
    StringsPaths p;
    p.base = content_resolve_location(root, "fxdata/gtext_" + lang + ".dat");
    if (campaign != nullptr)
    {
        const auto it = campaign->strings.find(lang);
        if (it != campaign->strings.end())
            p.campaign = it->second;
    }
    if (level_number >= 0 && !level_dir.empty())
    {
        char name[64];
        snprintf(name, sizeof(name), "map%05lld.%s.dat", (long long)level_number, lang.c_str());
        p.level = content_resolve_location(level_dir, name);
    }
    return p;
}

std::vector<std::string> content_strings_languages(const std::string &root, const ContentCampaign *campaign)
{
    std::set<std::string> found;
    std::error_code ec;
    for (fs::directory_iterator it(content_resolve_location(root, "fxdata"), ec), end; !ec && it != end; it.increment(ec))
    {
        const std::string name = lower(it->path().filename().string());
        if (name.size() == 13 && name.compare(0, 6, "gtext_") == 0 && name.compare(9, 4, ".dat") == 0)
            found.insert(name.substr(6, 3));
    }
    if (campaign != nullptr)
        for (const auto &s : campaign->strings)
            found.insert(s.first);
    std::vector<std::string> out;
    if (found.erase("eng"))
        out.push_back("eng");
    out.insert(out.end(), found.begin(), found.end());
    return out;
}

std::map<int64_t, std::vector<std::string>> content_strings_usage(const ContentCampaign *campaign, const std::string &levels_dir,
    const std::vector<int64_t> &levels)
{
    std::map<int64_t, std::vector<std::string>> used;
    if (campaign != nullptr)
        for (const auto &n : campaign->name_ids)
            used[n.first].push_back(n.second + " name");
    static const std::regex display("(DISPLAY_OBJECTIVE(_WITH_POS)?|DISPLAY_INFORMATION(_WITH_POS)?)\\s*\\(\\s*([0-9]+)",
        std::regex::icase);
    for (int64_t lv : levels)
    {
        char name[32];
        snprintf(name, sizeof(name), "map%05lld.txt", (long long)lv);
        std::string script;
        if (!read_file(content_resolve_location(levels_dir, name), script))
            continue;
        std::set<int64_t> ids;
        for (std::sregex_iterator it(script.begin(), script.end(), display), end; it != end; ++it)
            ids.insert(std::atoll((*it)[4].str().c_str()));
        char label[32];
        snprintf(label, sizeof(label), "map%05lld script", (long long)lv);
        for (int64_t id : ids)
            used[id].push_back(label);
    }
    return used;
}

/******************************************************************************/
bool StringsSession::load_layers()
{
    stack_ = StringsStack();
    exists_ = false;
    const std::string *path[StringLayer_Count] = {&paths_.base, &paths_.campaign, &paths_.level};
    for (int l = 0; l <= (int)layer_; l++)
    {
        std::string bytes;
        if (path[l]->empty() || !read_file(*path[l], bytes))
            continue;
        stack_.layers[l] = StringsFile::parse(bytes);
        stack_.present[l] = true;
        if (l == (int)layer_)
            exists_ = true;
    }
    return true;
}

bool StringsSession::open(const StringsPaths &paths, const std::string &lang, CfgLayer layer)
{
    open_ = false;
    pending_.clear();
    paths_ = paths;
    lang_ = lang;
    layer_ = layer;
    path_ = layer == CfgLayer_Base ? paths.base : layer == CfgLayer_Campaign ? paths.campaign : paths.level;
    load_layers();
    open_ = true;
    return true;
}

bool StringsSession::writable() const
{
    if (!open_ || layer_ == CfgLayer_Base || path_.empty() || !cfgc_lang_is_legacy_codepage(lang_))
        return false;
    // A campaign that points its [strings] at the base file has no file of its own.
    if (layer_ == CfgLayer_Campaign && path_ == paths_.base)
        return false;
    return true;
}

std::string StringsSession::why_read_only() const
{
    if (!open_)
        return "";
    if (layer_ == CfgLayer_Base)
        return "Base strings are never edited.";
    if (path_.empty())
        return layer_ == CfgLayer_Campaign ? "This campaign has no string file for this language ([strings] in its .cfg)."
                                           : "This target has no level to hold strings.";
    if (!cfgc_lang_is_legacy_codepage(lang_))
        return "This language uses a multi-byte code page; it can be read here but not edited.";
    if (layer_ == CfgLayer_Campaign && path_ == paths_.base)
        return "This campaign uses the base strings for this language; use the Level layer, or give the campaign its own file.";
    return "";
}

size_t StringsSession::id_count() const
{
    size_t n = stack_.id_count();
    if (!pending_.empty())
        n = std::max(n, pending_.rbegin()->first + 1);
    return n;
}

StringsSession::View StringsSession::view(size_t id) const
{
    View v;
    const auto p = pending_.find(id);
    // The stack as if the pending edit were already in this layer's file.
    StringsStack::Effective e = stack_.effective(id);
    if (p != pending_.end())
    {
        v.pending = true;
        StringsStack copy = stack_;
        copy.present[layer_] = true;
        copy.layers[layer_].set_raw(id, p->second.empty() ? std::string() : to_crlf(p->second));
        e = copy.effective(id);
        // Encoding is done at apply time; the display of a pending text is the text itself.
        if (!p->second.empty())
        {
            v.found = true;
            v.source = layer_;
            v.text = p->second;
            v.overridden_here = true;
            StringsStack::Effective below = stack_.effective(id);
            // What is beneath this layer: the highest lower layer with text.
            for (int l = (int)layer_ - 1; l >= 0; l--)
                if (stack_.present[l] && !stack_.layers[l].is_empty(id))
                {
                    v.has_beneath = true;
                    v.beneath = content_strings_display(stack_.layers[l].raw(id));
                    break;
                }
            (void)below;
            return v;
        }
    }
    v.found = e.found;
    v.source = (CfgLayer)e.source;
    v.text = content_strings_display(e.raw);
    v.overridden_here = e.found && e.source == (StringLayer)layer_;
    v.has_beneath = e.has_beneath;
    v.beneath = content_strings_display(e.beneath_raw);
    return v;
}

std::string StringsSession::own_text(size_t id) const
{
    const auto p = pending_.find(id);
    if (p != pending_.end())
        return p->second;
    if (!stack_.present[layer_])
        return std::string();
    return content_strings_display(stack_.layers[layer_].raw(id));
}

void StringsSession::set_text(size_t id, const std::string &text)
{
    if (!writable())
        return;
    const std::string current = stack_.present[layer_] ? content_strings_display(stack_.layers[layer_].raw(id)) : std::string();
    if (to_lf(text) == current)
        pending_.erase(id);
    else
        pending_[id] = to_lf(text);
}

void StringsSession::reset(size_t id)
{
    set_text(id, std::string());
}

size_t StringsSession::next_free_id() const
{
    for (size_t id = 1; id < kStringsMax; id++)
    {
        if (stack_.effective(id).found)
            continue;
        const auto p = pending_.find(id);
        if (p != pending_.end() && !p->second.empty())
            continue;
        return id;
    }
    return kStringsMax; // full
}

std::vector<StringsSession::Problem> StringsSession::diagnostics() const
{
    std::vector<Problem> out;
    for (const auto &p : pending_)
    {
        if (p.first >= kStringsMax)
            out.push_back({p.first, "id " + std::to_string(p.first) + " is beyond the " + std::to_string(kStringsMax) + " strings the game reads"});
        if (p.second.empty())
            continue;
        const StringsEncodeResult r = cfgc_strings_encode(p.second);
        for (uint64_t cp : r.unrepresentable)
        {
            char buf[120];
            snprintf(buf, sizeof(buf), "id %zu: the character U+%04llX cannot be stored in this language's code page", p.first, (unsigned long long)cp);
            out.push_back({p.first, buf});
        }
    }
    return out;
}

bool StringsSession::apply(std::string *error)
{
    if (!writable())
    {
        if (error != nullptr)
            *error = why_read_only();
        return false;
    }
    if (pending_.empty())
        return true;
    for (const Problem &p : diagnostics())
    {
        if (error != nullptr)
            *error = p.message;
        return false; // a character that cannot be stored would be lost: the edit must be fixed first
    }
    StringsFile file = stack_.present[layer_] ? stack_.layers[layer_] : StringsFile();
    for (const auto &p : pending_)
        file.set_raw(p.first, p.second.empty() ? std::string() : cfgc_strings_encode(to_crlf(p.second)).bytes);
    file.trim_trailing_empty();
    bool any = false;
    for (size_t i = 0; i < file.size(); i++)
        any = any || !file.is_empty(i);
    WriteBatch batch;
    if (!any && layer_ == CfgLayer_Level)
    {
        if (exists_)
            batch.remove(path_); // a level's own strings file with nothing left in it is not kept
    }
    else
        batch.put(path_, file.serialize_for_write());
    std::string err;
    if (!batch.commit(&err))
    {
        if (error != nullptr)
            *error = err;
        return false;
    }
    pending_.clear();
    load_layers();
    return true;
}
