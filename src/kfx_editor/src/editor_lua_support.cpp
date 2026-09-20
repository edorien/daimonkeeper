/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_lua_support.cpp
 *     See editor_lua_support.h.
 */
#include "pre_inc.h"
#include "editor_lua_support.h"
#include "config.h" // prepare_file_path_buf, FGrp_*

#include <cctype>
#include <filesystem>
#include <fstream>
#include <system_error>
#include "post_inc.h"

namespace {

bool ident_start(char c) { return std::isalpha((unsigned char)c) || c == '_'; }
bool ident_char(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

void scan_line(const std::string &line, std::set<std::string> &out)
{
    size_t i = 0;
    if (line.compare(0, 9, "function ") == 0)
    {
        i = 9;
        while (i < line.size() && line[i] == ' ')
            i++;
        size_t s = i;
        while (i < line.size() && ident_char(line[i]))
            i++;
        if (i > s && ident_start(line[s]))
            out.insert(line.substr(s, i - s)); // "Class.method" / "Class:method" -> Class
        return;
    }
    if (!line.empty() && ident_start(line[0]))
    {
        while (i < line.size() && ident_char(line[i]))
            i++;
        const size_t end = i;
        while (i < line.size() && line[i] == ' ')
            i++;
        if (i < line.size() && line[i] == '=' && (i + 1 >= line.size() || line[i + 1] != '='))
        {
            const std::string name = line.substr(0, end);
            if (name != "local" && name != "return")
                out.insert(name);
        }
    }
}

std::set<std::string> s_api;
bool s_api_loaded = false;

} // namespace

std::set<std::string> editor_lua_scan_api(const std::string &dir)
{
    std::set<std::string> out;
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
    {
        if (!it->is_regular_file(ec) || it->path().extension() != ".lua")
            continue;
        std::ifstream f(it->path(), std::ios::binary);
        std::string line;
        while (std::getline(f, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            scan_line(line, out);
        }
    }
    return out;
}

void editor_lua_scan_api_text(const std::string &text, std::set<std::string> &out)
{
    size_t pos = 0;
    while (pos <= text.size())
    {
        size_t nl = text.find('\n', pos);
        std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        scan_line(line, out);
        if (nl == std::string::npos)
            break;
        pos = nl + 1;
    }
}

void editor_lua_api_names_reload()
{
    char dir[1024];
    prepare_file_path_buf(dir, sizeof(dir), FGrp_FxData, "lua");
    s_api = editor_lua_scan_api(dir);
    s_api_loaded = true;
}

const std::set<std::string> &editor_lua_api_names()
{
    if (!s_api_loaded)
        editor_lua_api_names_reload();
    return s_api;
}

std::string editor_lua_template()
{
    return
        "-- Level script (Lua). Runs before this level's .txt script; anything you\n"
        "-- set up in OnGameStart() runs after it.\n"
        "\n"
        "function OnGameStart()\n"
        "    -- Example: call a function every 100 game turns.\n"
        "    -- RegisterTimerEvent(\"OnTick\", 100, true)\n"
        "end\n"
        "\n"
        "-- function OnTick()\n"
        "-- end\n";
}

std::string editor_lua_require_at(const std::string &line, size_t column)
{
    size_t pos = 0;
    while ((pos = line.find("require", pos)) != std::string::npos)
    {
        size_t i = pos + 7;
        const size_t call_start = pos;
        pos = i;
        if (call_start > 0 && ident_char(line[call_start - 1]))
            continue;
        while (i < line.size() && (line[i] == ' ' || line[i] == '('))
            i++;
        if (i >= line.size() || (line[i] != '"' && line[i] != '\''))
            continue;
        const char q = line[i];
        const size_t s = i + 1;
        const size_t e = line.find(q, s);
        if (e == std::string::npos)
            continue;
        if (column >= call_start && column <= e + 1)
            return line.substr(s, e - s);
    }
    return std::string();
}

std::string editor_lua_resolve_module(const std::string &name, const std::vector<std::string> &roots)
{
    if (name.empty())
        return std::string();
    std::string rel = name;
    for (char &c : rel)
        if (c == '.')
            c = '/';
    for (const std::string &root : roots)
    {
        const std::filesystem::path p = std::filesystem::path(root) / (rel + ".lua");
        std::error_code ec;
        if (std::filesystem::is_regular_file(p, ec))
            return p.string();
    }
    return std::string();
}

std::vector<std::string> editor_lua_search_roots(const std::string &level_dir)
{
    std::vector<std::string> roots;
    roots.push_back(level_dir);
    char dir[1024];
    prepare_file_path_buf(dir, sizeof(dir), FGrp_CmpgConfig, "lua");
    roots.push_back(dir);
    prepare_file_path_buf(dir, sizeof(dir), FGrp_FxData, "lua");
    roots.push_back(dir);
    return roots;
}

std::string editor_lua_copy_target(const std::string &level_dir, const std::string &name)
{
    std::string rel = name;
    for (char &c : rel)
        if (c == '.')
            c = '/';
    return (std::filesystem::path(level_dir) / (rel + ".lua")).string();
}

bool editor_lua_copy_file(const std::string &src, const std::string &dst)
{
    std::error_code ec;
    if (std::filesystem::exists(dst, ec))
        return false;
    std::filesystem::create_directories(std::filesystem::path(dst).parent_path(), ec);
    ec.clear();
    return std::filesystem::copy_file(src, dst, std::filesystem::copy_options::none, ec) && !ec;
}

bool editor_lua_write_file(const std::string &path, const std::string &text)
{
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f)
            return false;
        f.write(text.data(), (std::streamsize)text.size());
        f.flush();
        if (!f)
        {
            std::error_code rm;
            std::filesystem::remove(tmp, rm);
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec)
    {
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

bool editor_lua_same_file(const std::string &a, const std::string &b)
{
    std::error_code ec;
    const bool eq = std::filesystem::equivalent(a, b, ec);
    return !ec && eq;
}
