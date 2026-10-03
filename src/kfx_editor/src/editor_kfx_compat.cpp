/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_kfx_compat.cpp
 *     Whether a map loads in the KeeperFX release this game is compatible with.
 * @par Purpose:
 *     See editor_kfx_compat.h.
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "editor_kfx_compat.h"

#include "map_content.h"
#include "config_creature.h"
#include "config_effects.h"
#include "config_objects.h"
#include "config_terrain.h"
#include "config_trapdoor.h"

#include <cctype>
#include <cstring>
#include <map>
#include <set>
#include "post_inc.h"

#include "kfx_compat_reference.inc"

namespace {

std::vector<std::string> s_last_save_compat_problems;

bool is_ident_start(char c) { return std::isalpha((unsigned char)c) || c == '_'; }
bool is_ident_char(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

bool in_list(const char *const *list, const std::string &name, bool ignore_case)
{
    for (; *list != nullptr; list++)
        if (ignore_case ? (strcasecmp(*list, name.c_str()) == 0) : (name == *list))
            return true;
    return false;
}

std::string upper(std::string s)
{
    for (char &c : s)
        c = (char)std::toupper((unsigned char)c);
    return s;
}

// Level script (.txt): every identifier outside quoted strings on non-REM lines
// -- commands lead a line, subfunctions (RANDOM, DRAWFROM, ...) sit in arguments.
void check_script(const std::string &script, std::vector<std::string> &problems)
{
    std::set<std::string> reported;
    size_t line_no = 0, pos = 0;
    while (pos <= script.size())
    {
        size_t end = script.find('\n', pos);
        if (end == std::string::npos)
            end = script.size();
        const std::string line = script.substr(pos, end - pos);
        pos = end + 1;
        line_no++;
        size_t i = line.find_first_not_of(" \t\r");
        if ((i == std::string::npos) || (upper(line.substr(i, 3)) == "REM" && (i + 3 >= line.size() || !is_ident_char(line[i + 3]))))
            continue;
        bool in_string = false;
        for (; i < line.size(); i++)
        {
            if (line[i] == '"')
                in_string = !in_string;
            if (in_string || !is_ident_start(line[i]))
                continue;
            size_t j = i;
            while (j < line.size() && is_ident_char(line[j]))
                j++;
            const std::string word = upper(line.substr(i, j - i));
            if (in_list(kfx_ref_forkonly_script_commands, word, true) && reported.insert(word).second)
                problems.push_back("script line " + std::to_string(line_no) + ": " + word);
            i = j - 1;
        }
    }
}

// Level Lua: calls (name(, :name(, .name() to fork-only API functions, outside
// comments and strings, unless the script defines that name itself.
void check_lua(const std::string &lua, std::vector<std::string> &problems)
{
    // Blank out comments and strings, keeping newlines so line numbers hold.
    std::string code = lua;
    for (size_t i = 0; i < code.size(); i++)
    {
        auto blank_until = [&](size_t from, size_t to) {
            for (size_t k = from; k < to && k < code.size(); k++)
                if (code[k] != '\n')
                    code[k] = ' ';
        };
        if (code.compare(i, 4, "--[[") == 0)
        {
            size_t e = code.find("]]", i + 4);
            e = (e == std::string::npos) ? code.size() : e + 2;
            blank_until(i, e);
            i = e - 1;
        }
        else if (code.compare(i, 2, "--") == 0)
        {
            size_t e = code.find('\n', i);
            e = (e == std::string::npos) ? code.size() : e;
            blank_until(i, e);
            i = e - 1;
        }
        else if (code.compare(i, 2, "[[") == 0)
        {
            size_t e = code.find("]]", i + 2);
            e = (e == std::string::npos) ? code.size() : e + 2;
            blank_until(i, e);
            i = e - 1;
        }
        else if (code[i] == '"' || code[i] == '\'')
        {
            const char q = code[i];
            size_t e = i + 1;
            while (e < code.size() && code[e] != q && code[e] != '\n')
                e += (code[e] == '\\') ? 2 : 1;
            blank_until(i, e + 1);
            i = e;
        }
    }
    // Names the script defines itself (function Name / Name = function) aren't API calls.
    std::set<std::string> defined;
    for (size_t i = 0; (i = code.find("function", i)) != std::string::npos; i += 8)
    {
        if ((i > 0 && is_ident_char(code[i - 1])) || (i + 8 < code.size() && is_ident_char(code[i + 8])))
            continue;
        size_t j = code.find_first_not_of(" \t", i + 8);
        size_t k = j;
        while (k != std::string::npos && k < code.size() && (is_ident_char(code[k]) || code[k] == '.' || code[k] == ':'))
            k++;
        if (j != std::string::npos && k > j)
        {
            std::string full = code.substr(j, k - j);
            size_t sep = full.find_last_of(".:");
            defined.insert(sep == std::string::npos ? full : full.substr(sep + 1));
        }
        // "Name = function": the identifier before the '='.
        size_t eq = code.find_last_not_of(" \t", i == 0 ? 0 : i - 1);
        if (eq != std::string::npos && code[eq] == '=')
        {
            size_t e = code.find_last_not_of(" \t", eq == 0 ? 0 : eq - 1);
            size_t s = e;
            while (s > 0 && is_ident_char(code[s - 1]))
                s--;
            if (e != std::string::npos && is_ident_char(code[e]))
                defined.insert(code.substr(s, e - s + 1));
        }
    }
    std::set<std::string> reported;
    size_t line_no = 1;
    for (size_t i = 0; i < code.size(); i++)
    {
        if (code[i] == '\n')
        {
            line_no++;
            continue;
        }
        if (!is_ident_start(code[i]) || (i > 0 && is_ident_char(code[i - 1])))
            continue;
        size_t j = i;
        while (j < code.size() && is_ident_char(code[j]))
            j++;
        const std::string word = code.substr(i, j - i);
        size_t k = code.find_first_not_of(" \t", j);
        if (k != std::string::npos && code[k] == '(' && !defined.count(word)
            && in_list(kfx_ref_forkonly_lua_functions, word, false) && reported.insert(word).second)
            problems.push_back("Lua line " + std::to_string(line_no) + ": " + word + "()");
        i = j - 1;
    }
}

const char *thing_kind(ThingClass cls)
{
    switch (cls)
    {
    case TCls_Object:    return "object";
    case TCls_Trap:      return "trap";
    case TCls_Door:      return "door";
    case TCls_Creature:  return "creature";
    case TCls_EffectGen: return "effectgen";
    default:             return nullptr;
    }
}

void check_kinds(const MapContent &content, const EditorKindNameFn &name_of, std::vector<std::string> &problems)
{
    // How often each (kind, model) is used.
    std::map<std::pair<std::string, int64_t>, int64_t> used;
    for (const MapThingRecord &t : content.things)
        if (const char *kind = thing_kind(t.thing_class))
            used[{kind, (int64_t)t.model}]++;
    for (SlabKind s : content.slab_kind)
        used[{"slab", (int64_t)s}]++;
    for (const KfxRefForkOnlyKind *k = kfx_ref_forkonly_kinds; k->kind != nullptr; k++)
    {
        auto it = used.find({k->kind, k->index});
        if (it == used.end())
            continue;
        // Only while it's still the base game's kind: a campaign or level that
        // redefines this model number ships its own definition with the map.
        if (upper(name_of(k->kind, k->index)) != k->name)
            continue;
        problems.push_back(std::string(k->kind) + " " + k->name + " (model " + std::to_string(k->index)
            + "), used " + std::to_string(it->second) + (it->second == 1 ? " time" : " times"));
    }
}

std::string running_game_kind_name(const char *kind, int64_t model)
{
    const std::string k = kind;
    if (k == "object")    return object_code_name((ThingModel)model);
    if (k == "trap")      return trap_code_name(model);
    if (k == "door")      return door_code_name(model);
    if (k == "creature")  return creature_code_name((ThingModel)model);
    if (k == "effectgen") return effectgenerator_code_name((ThingModel)model);
    if (k == "slab")      return slab_code_name((SlabKind)model);
    return "";
}

} // namespace

const char *editor_kfx_compat_reference_label(void)
{
    return KFX_COMPAT_REFERENCE_LABEL;
}

std::vector<std::string> editor_kfx_compat_problems(const MapContent &content, const EditorKindNameFn &name_of)
{
    std::vector<std::string> problems;
    check_script(content.script_text, problems);
    if (content.has_lua)
        check_lua(content.lua_text, problems);
    check_kinds(content, name_of, problems);
    return problems;
}

std::vector<std::string> editor_kfx_compat_problems(const MapContent &content)
{
    return editor_kfx_compat_problems(content, running_game_kind_name);
}

const std::vector<std::string> &editor_last_save_compat_problems(void)
{
    return s_last_save_compat_problems;
}

void editor_set_last_save_compat_problems(const std::vector<std::string> &problems)
{
    s_last_save_compat_problems = problems;
}
