/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_lua_stubs.cpp
 *     See editor_lua_stubs.h.
 */
#include "pre_inc.h"
#include "editor_lua_stubs.h"
#include "config.h" // prepare_file_path_buf, FGrp_FxData

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>
#include "post_inc.h"

namespace {

std::string trim(const std::string &s)
{
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r'))
        a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r'))
        b--;
    return s.substr(a, b - a);
}

// First whitespace-delimited word of `s`; `s` becomes the rest, trimmed.
std::string take_word(std::string &s)
{
    s = trim(s);
    size_t i = 0;
    while (i < s.size() && s[i] != ' ' && s[i] != '\t')
        i++;
    std::string w = s.substr(0, i);
    s = trim(s.substr(i));
    return w;
}

} // namespace

std::vector<LuaFunctionDoc> editor_lua_parse_stub_text(const std::string &text, const std::string &group)
{
    std::vector<LuaFunctionDoc> out;
    std::string doc;
    std::vector<LuaParamDoc> params;
    std::string returns;
    std::istringstream in(text);
    std::string raw;
    while (std::getline(in, raw))
    {
        const std::string line = trim(raw);
        if (line.compare(0, 3, "---") == 0)
        {
            size_t i = 3;
            while (i < line.size() && line[i] == '-')
                i++;
            std::string body = trim(line.substr(i));
            if (i == 3 && !body.empty() && body[0] == '@')
            {
                std::string rest = body.substr(1);
                const std::string tag = take_word(rest);
                if (tag == "param")
                {
                    LuaParamDoc p;
                    p.name = take_word(rest);
                    if (!p.name.empty() && p.name.back() == '?')
                    {
                        p.optional = true;
                        p.name.pop_back();
                    }
                    p.type = take_word(rest);
                    p.description = rest;
                    params.push_back(p);
                }
                else if (tag == "return" && returns.empty())
                    returns = rest;
            }
            else if (!body.empty())
            {
                if (!doc.empty())
                    doc += ' ';
                doc += body;
            }
            continue;
        }
        if (line.compare(0, 9, "function ") == 0)
        {
            std::string rest = trim(line.substr(9));
            size_t p = rest.find('(');
            if (p != std::string::npos)
            {
                LuaFunctionDoc f;
                f.name = trim(rest.substr(0, p));
                f.group = group;
                f.doc = doc;
                f.params = params;
                f.returns = returns;
                // Parameters the stub declares by name only.
                if (f.params.empty())
                {
                    const size_t q = rest.find(')', p);
                    std::string names = rest.substr(p + 1, q == std::string::npos ? std::string::npos : q - p - 1);
                    std::stringstream ns(names);
                    std::string n;
                    while (std::getline(ns, n, ','))
                    {
                        n = trim(n);
                        if (!n.empty())
                        {
                            LuaParamDoc pd;
                            pd.name = n;
                            f.params.push_back(pd);
                        }
                    }
                }
                out.push_back(f);
            }
        }
        doc.clear();
        params.clear();
        returns.clear();
    }
    return out;
}

std::vector<LuaFunctionDoc> editor_lua_load_stubs(const std::string &dir)
{
    std::vector<LuaFunctionDoc> out;
    for (const char *sub : {"bindings", "triggers"})
    {
        std::error_code ec;
        for (std::filesystem::directory_iterator it(std::filesystem::path(dir) / sub, ec), end; !ec && it != end;
             it.increment(ec))
        {
            if (!it->is_regular_file(ec) || it->path().extension() != ".lua")
                continue;
            std::ifstream f(it->path(), std::ios::binary);
            std::stringstream ss;
            ss << f.rdbuf();
            for (const LuaFunctionDoc &fn : editor_lua_parse_stub_text(ss.str(), it->path().stem().string()))
                if (fn.name.find_first_of(":.") == std::string::npos && fn.name[0] != '_')
                    out.push_back(fn);
        }
    }
    std::sort(out.begin(), out.end(), [](const LuaFunctionDoc &a, const LuaFunctionDoc &b) {
        return a.group != b.group ? a.group < b.group : a.name < b.name;
    });
    return out;
}

const std::vector<LuaFunctionDoc> &editor_lua_stub_catalog()
{
    static std::vector<LuaFunctionDoc> catalog;
    static bool loaded = false;
    if (!loaded)
    {
        char dir[1024];
        prepare_file_path_buf(dir, sizeof(dir), FGrp_FxData, "lua");
        catalog = editor_lua_load_stubs(dir);
        loaded = true;
    }
    return catalog;
}

std::string editor_lua_signature(const LuaFunctionDoc &f)
{
    std::string s = f.name + "(";
    for (size_t i = 0; i < f.params.size(); i++)
    {
        if (i)
            s += ", ";
        s += f.params[i].name;
        if (f.params[i].optional)
            s += "?";
    }
    return s + ")";
}

std::string editor_lua_call_template(const LuaFunctionDoc &f)
{
    std::string s = f.name + "(";
    bool first = true;
    for (const LuaParamDoc &p : f.params)
    {
        if (p.optional)
            continue;
        if (!first)
            s += ", ";
        s += p.name;
        first = false;
    }
    return s + ")";
}

std::vector<const LuaFunctionDoc *> editor_lua_event_functions(const std::vector<LuaFunctionDoc> &catalog)
{
    std::vector<const LuaFunctionDoc *> out;
    for (const LuaFunctionDoc &f : catalog)
        if (f.name.compare(0, 8, "Register") == 0 && f.name.size() > 13
            && f.name.compare(f.name.size() - 5, 5, "Event") == 0 && !f.params.empty() && f.params[0].name == "action")
            out.push_back(&f);
    return out;
}

std::string editor_lua_event_snippet(const LuaFunctionDoc &f)
{
    const std::string handler = "On" + f.name.substr(8, f.name.size() - 8 - 5);
    std::string s = "function " + handler + "(eventData, triggerData)\n    -- TODO\nend\n\n";
    s += "-- Call this from OnGameStart():\n" + f.name + "(" + handler;
    for (size_t i = 1; i < f.params.size(); i++)
        s += ", " + f.params[i].name;
    return s + ")\n";
}
