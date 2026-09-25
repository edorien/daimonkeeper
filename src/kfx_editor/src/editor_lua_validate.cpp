/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_lua_validate.cpp
 *     See editor_lua_validate.h.
 * @par Comment:
 *     The name check is a heuristic on a token stream, so it only ever
 *     warns: an identifier followed by '(' that is not a Lua keyword or
 *     standard function, not defined anywhere in the script (function names,
 *     parameters, locals, loop variables, assignments), not declared by the
 *     stub files and not declared by a required module. Calls through a
 *     table ("x.y(", "x:y(") are never checked.
 */
#include "pre_inc.h"
#include "editor_lua_validate.h"
#include "editor_lua_support.h"
#include "lvl_script_lib.h"
#include "lvl_script_commands.h" // command_desc

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

#include <cctype>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include "post_inc.h"

namespace {

enum TokKind { Tk_Ident, Tk_String, Tk_Number, Tk_Op };

struct Tok
{
    TokKind kind;
    std::string text;
    size_t line;
};

bool ident_start(char c) { return std::isalpha((unsigned char)c) || c == '_'; }
bool ident_char(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

// Level of a long bracket ("[==[" -> 2) starting at i, or -1.
int64_t long_bracket_level(const std::string &s, size_t i)
{
    if (i >= s.size() || s[i] != '[')
        return -1;
    size_t j = i + 1;
    int64_t level = 0;
    while (j < s.size() && s[j] == '=')
    {
        level++;
        j++;
    }
    return (j < s.size() && s[j] == '[') ? level : -1;
}

// Index just past the closing bracket of a long bracket opened at i.
size_t skip_long_bracket(const std::string &s, size_t i, int64_t level, size_t &line)
{
    const std::string close = "]" + std::string((size_t)level, '=') + "]";
    size_t start = i + 2 + (size_t)level;
    size_t end = s.find(close, start);
    end = (end == std::string::npos) ? s.size() : end + close.size();
    for (size_t k = i; k < end; k++)
        if (s[k] == '\n')
            line++;
    return end;
}

std::vector<Tok> tokenize(const std::string &s)
{
    std::vector<Tok> out;
    size_t line = 0;
    size_t i = 0;
    while (i < s.size())
    {
        const char c = s[i];
        if (c == '\n')
        {
            line++;
            i++;
        }
        else if (std::isspace((unsigned char)c))
            i++;
        else if (c == '-' && i + 1 < s.size() && s[i + 1] == '-')
        {
            const int64_t level = long_bracket_level(s, i + 2);
            if (level >= 0)
                i = skip_long_bracket(s, i + 2, level, line);
            else
            {
                while (i < s.size() && s[i] != '\n')
                    i++;
            }
        }
        else if (ident_start(c))
        {
            size_t j = i;
            while (j < s.size() && ident_char(s[j]))
                j++;
            out.push_back({Tk_Ident, s.substr(i, j - i), line});
            i = j;
        }
        else if (std::isdigit((unsigned char)c))
        {
            size_t j = i;
            while (j < s.size() && (std::isalnum((unsigned char)s[j]) || s[j] == '.'))
                j++;
            out.push_back({Tk_Number, s.substr(i, j - i), line});
            i = j;
        }
        else if (c == '"' || c == '\'')
        {
            std::string val;
            const size_t start_line = line;
            size_t j = i + 1;
            while (j < s.size() && s[j] != c && s[j] != '\n')
            {
                if (s[j] == '\\' && j + 1 < s.size())
                    j++;
                val += s[j];
                j++;
            }
            out.push_back({Tk_String, val, start_line});
            i = (j < s.size() && s[j] == c) ? j + 1 : j;
        }
        else if (c == '[' && long_bracket_level(s, i) >= 0)
        {
            const size_t start_line = line;
            const int64_t level = long_bracket_level(s, i);
            const size_t end = skip_long_bracket(s, i, level, line);
            out.push_back({Tk_String, "", start_line});
            i = end;
        }
        else
        {
            std::string op(1, c);
            if (i + 1 < s.size())
            {
                const std::string two = s.substr(i, 2);
                if (two == "==" || two == "~=" || two == "<=" || two == ">=" || two == "..")
                    op = two;
            }
            out.push_back({Tk_Op, op, line});
            i += op.size();
        }
    }
    return out;
}

const std::set<std::string> &lua_words()
{
    static const std::set<std::string> words = {
        // keywords
        "and", "break", "do", "else", "elseif", "end", "false", "for", "function", "goto", "if", "in", "local",
        "nil", "not", "or", "repeat", "return", "then", "true", "until", "while",
        // standard functions and LuaJIT globals
        "assert", "collectgarbage", "dofile", "error", "getfenv", "getmetatable", "ipairs", "load", "loadfile",
        "loadstring", "module", "next", "pairs", "pcall", "print", "rawequal", "rawget", "rawlen", "rawset",
        "require", "select", "setfenv", "setmetatable", "tonumber", "tostring", "type", "unpack", "xpcall",
    };
    return words;
}

bool is_keyword(const std::string &w)
{
    static const std::set<std::string> kw = {"and", "break", "do", "else", "elseif", "end", "false", "for",
        "function", "goto", "if", "in", "local", "nil", "not", "or", "repeat", "return", "then", "true", "until",
        "while"};
    return kw.count(w) != 0;
}

bool op_is(const std::vector<Tok> &t, size_t i, const char *op)
{
    return i < t.size() && t[i].kind == Tk_Op && t[i].text == op;
}

bool ident_is(const std::vector<Tok> &t, size_t i, const char *name)
{
    return i < t.size() && t[i].kind == Tk_Ident && t[i].text == name;
}

// Names the script defines itself.
std::set<std::string> collect_defined(const std::vector<Tok> &t)
{
    std::set<std::string> def;
    for (size_t i = 0; i < t.size(); i++)
    {
        if (t[i].kind != Tk_Ident)
            continue;
        const std::string &w = t[i].text;
        if (w == "function")
        {
            size_t j = i + 1;
            if (j < t.size() && t[j].kind == Tk_Ident)
            {
                def.insert(t[j].text);
                j++;
                while ((op_is(t, j, ".") || op_is(t, j, ":")) && j + 1 < t.size())
                    j += 2;
            }
            if (op_is(t, j, "("))
                for (j++; j < t.size() && !op_is(t, j, ")"); j++)
                    if (t[j].kind == Tk_Ident)
                        def.insert(t[j].text);
        }
        else if (w == "local" || w == "for")
        {
            for (size_t j = i + 1; j < t.size(); j++)
            {
                if (t[j].kind == Tk_Ident && !is_keyword(t[j].text))
                    def.insert(t[j].text);
                else if (!op_is(t, j, ","))
                    break;
                if (j + 1 < t.size() && !op_is(t, j + 1, ","))
                    break;
                j++;
            }
        }
        else if (op_is(t, i + 1, "=") && !(i > 0 && (op_is(t, i - 1, ".") || op_is(t, i - 1, ":"))))
        {
            def.insert(w);
        }
    }
    return def;
}

std::string unescape(const std::string &s)
{
    return s; // tokenize() already resolved the escape's target character
}

int64_t syntax_check(const std::string &text, size_t &line, std::string &message)
{
    lua_State *L = luaL_newstate();
    if (L == nullptr)
        return 0;
    int64_t status = luaL_loadbuffer(L, text.data(), text.size(), "=lua");
    if (status != 0)
    {
        std::string msg = lua_tostring(L, -1) ? lua_tostring(L, -1) : "syntax error";
        line = 0;
        message = msg;
        if (msg.compare(0, 4, "lua:") == 0)
        {
            char *end = nullptr;
            const int64_t n = LbStrToI32(msg.c_str() + 4, &end, 10);
            if (end != msg.c_str() + 4)
            {
                line = (n > 0) ? (size_t)n - 1 : 0;
                message = (*end == ':') ? std::string(end + 1) : std::string(end);
                while (!message.empty() && message[0] == ' ')
                    message.erase(0, 1);
            }
        }
    }
    lua_close(L);
    return status != 0 ? 1 : 0;
}

} // namespace

std::vector<ScriptIssue> editor_lua_validate(const std::string &text, const std::set<std::string> &api,
    const ScriptCommandLookup &command_lookup, const LuaModuleText &module_text)
{
    std::vector<ScriptIssue> issues;
    size_t err_line = 0;
    std::string err_msg;
    const bool syntax_error = syntax_check(text, err_line, err_msg) != 0;
    if (syntax_error)
    {
        ScriptIssue is;
        is.line = err_line;
        is.severity = ScrIssue_Error;
        is.message = "Lua syntax: " + err_msg;
        issues.push_back(is);
    }

    const std::vector<Tok> t = tokenize(text);
    std::set<std::string> known = api;
    known.insert(lua_words().begin(), lua_words().end());
    const std::set<std::string> defined = collect_defined(t);
    known.insert(defined.begin(), defined.end());
    if (module_text)
        for (size_t i = 0; i + 1 < t.size(); i++)
            if (ident_is(t, i, "require"))
            {
                const size_t k = op_is(t, i + 1, "(") ? i + 2 : i + 1;
                if (k < t.size() && t[k].kind == Tk_String)
                    editor_lua_scan_api_text(module_text(t[k].text), known);
            }

    for (size_t i = 0; i < t.size(); i++)
    {
        if (t[i].kind != Tk_Ident || is_keyword(t[i].text))
            continue;
        if (i > 0 && (op_is(t, i - 1, ".") || op_is(t, i - 1, ":") || ident_is(t, i - 1, "function")))
            continue;
        if (!op_is(t, i + 1, "("))
            continue;
        const std::string &name = t[i].text;

        if (name == "RunDKScriptCommand" && i + 2 < t.size() && t[i + 2].kind == Tk_String
            && (op_is(t, i + 3, ")") || op_is(t, i + 3, ",")))
        {
            for (const ScriptIssue &si : editor_script_validate(unescape(t[i + 2].text), command_lookup))
            {
                ScriptIssue is = si;
                is.line = t[i + 2].line;
                is.message = "In RunDKScriptCommand: " + si.message;
                issues.push_back(is);
            }
        }
        if (!syntax_error && known.count(name) == 0)
        {
            ScriptIssue is;
            is.line = t[i].line;
            is.severity = ScrIssue_Warning;
            is.message = "Unknown function '" + name + "'";
            issues.push_back(is);
        }
    }
    std::stable_sort(issues.begin(), issues.end(),
        [](const ScriptIssue &a, const ScriptIssue &b) { return a.line < b.line; });
    return issues;
}

std::vector<ScriptIssue> editor_lua_validate_engine(const std::string &text, const std::string &level_dir)
{
    const std::vector<std::string> roots = editor_lua_search_roots(level_dir);
    LuaModuleText loader = [roots](const std::string &name) {
        const std::string path = editor_lua_resolve_module(name, roots);
        if (path.empty())
            return std::string();
        std::ifstream f(path, std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    };
    ScriptCommandLookup lookup = [](const std::string &name) -> const char * {
        for (int64_t i = 0; command_desc[i].textptr != NULL; i++)
            if (name == command_desc[i].textptr)
                return command_desc[i].args;
        return nullptr;
    };
    return editor_lua_validate(text, editor_lua_api_names(), lookup, loader);
}
