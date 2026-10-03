/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file script_setup.cpp
 *     See script_setup.h.
 * @par Purpose:
 *     docs/refactor/editor/05-script-and-level-settings.md §4.2. No
 *     existing generic single-line script-command parser is reusable here
 *     (checked: `script_scan_line()`/`get_next_token()`, kfx_game's own
 *     lvl_script.c, both require a live script-execution context and have
 *     immediate side effects for some commands -- START_MONEY calls
 *     player_add_offmap_gold() the moment it's scanned) -- this hand-rolls
 *     a small parser/generator for just the four commands this slice
 *     actually needs, working purely on plain text, no live session
 *     required.
 * @par Comment:
 *     None.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "script_setup.h"

#include "bflib_basics.h" // NamedCommand, get_rid
#include "config_creature.h" // creature_desc, creature_code_name
#include "config_terrain.h" // room_desc
#include "config_magic.h" // power_desc
#include "config_trapdoor.h" // trap_desc/door_desc

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "post_inc.h"

/******************************************************************************/
namespace {

const char *const kBeginMarker = "REM --- editor-managed setup: do not hand-edit between these markers ---";
const char *const kEndMarker = "REM --- end editor-managed setup ---";

std::string trim(const std::string &s)
{
    size_t begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
        return std::string();
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

// "COMMAND(arg1,arg2)" -> ["arg1", "arg2"], trimmed. Returns an empty
// vector if there's no parenthesised argument list at all (malformed/
// unrecognized line, silently skipped by the caller).
std::vector<std::string> split_args(const std::string &line)
{
    std::vector<std::string> args;
    size_t open_paren = line.find('(');
    size_t close_paren = line.rfind(')');
    if (open_paren == std::string::npos || close_paren == std::string::npos || close_paren <= open_paren)
        return args;
    std::string inner = line.substr(open_paren + 1, close_paren - open_paren - 1);
    size_t start = 0;
    while (start <= inner.size())
    {
        size_t comma = inner.find(',', start);
        if (comma == std::string::npos)
        {
            args.push_back(trim(inner.substr(start)));
            break;
        }
        args.push_back(trim(inner.substr(start, comma - start)));
        start = comma + 1;
    }
    return args;
}

// "PLAYER3" -> 3; anything else (PLAYER_GOOD/PLAYER_NEUTRAL/malformed) ->
// -1, treated as "not a keeper player slot this dialog manages" and
// skipped by the caller. Matches the real per-player script-keyword
// convention confirmed against config_players.h's own PLAYER0..PLAYER6.
int64_t parse_player_index(const std::string &token)
{
    static const char kPrefix[] = "PLAYER";
    static const size_t kPrefixLen = sizeof(kPrefix) - 1;
    if (token.size() <= kPrefixLen || token.compare(0, kPrefixLen, kPrefix) != 0)
        return -1;
    for (size_t i = kPrefixLen; i < token.size(); i++)
    {
        if (!isdigit((unsigned char)token[i]))
            return -1; // PLAYER_GOOD/PLAYER_NEUTRAL/ALL_PLAYERS etc.
    }
    return atoi(token.c_str() + kPrefixLen);
}

const char *const kAvailCommandNames[AvailKind_Count] = {
    "CREATURE_AVAILABLE", "ROOM_AVAILABLE", "MAGIC_AVAILABLE", "TRAP_AVAILABLE", "DOOR_AVAILABLE",
};

// "ALL_PLAYERS" -> -1, "PLAYERn" -> n, anything else -> -2 (skipped).
int64_t parse_availability_player(const std::string &token)
{
    if (token == "ALL_PLAYERS")
        return -1;
    int64_t idx = parse_player_index(token);
    return (idx >= 0) ? idx : -2;
}

const char *const kOperators[] = { "==", "!=", ">=", "<=", ">", "<" };

// "IF(PLAYER0, GAME_TURN > 125)" -> clause. False for anything the rule
// editor doesn't model (other argument shapes, non-numeric values, ...).
bool parse_if_clause(const std::string &line, WinLoseClause &out)
{
    if (line.compare(0, 2, "IF") != 0)
        return false;
    size_t p = 2;
    while (p < line.size() && isspace((unsigned char)line[p]))
        p++;
    if (p >= line.size() || line[p] != '(')
        return false;
    std::vector<std::string> args = split_args(line.substr(p));
    if (args.size() != 2)
        return false;
    const int64_t player = parse_player_index(args[0]);
    if (player < 0)
        return false;
    // Two-character operators first so ">=" is not read as ">".
    size_t at = std::string::npos, oplen = 0;
    const char *found = nullptr;
    for (const char *op : kOperators)
    {
        const size_t pos = args[1].find(op);
        if (pos != std::string::npos && (at == std::string::npos || pos < at || (pos == at && strlen(op) > oplen)))
        {
            at = pos;
            oplen = strlen(op);
            found = op;
        }
    }
    if (found == nullptr)
        return false;
    const std::string var = trim(args[1].substr(0, at));
    const std::string val = trim(args[1].substr(at + oplen));
    if (var.empty() || val.empty())
        return false;
    for (char c : var)
        if (!isalnum((unsigned char)c) && c != '_')
            return false;
    size_t i = (val[0] == '-') ? 1 : 0;
    if (i >= val.size())
        return false;
    for (; i < val.size(); i++)
        if (!isdigit((unsigned char)val[i]))
            return false;
    out.player = player;
    out.variable = var;
    out.op = found;
    out.value = atoi(val.c_str());
    return true;
}

} // namespace

std::vector<DuplicateWinLose> script_setup_find_duplicate_win_lose(const std::string &script_text)
{
    std::vector<DuplicateWinLose> issues;
    const std::string body = script_setup_extract_region(script_text);
    if (body.empty())
        return issues;
    const ManagedSetupValues values = script_setup_parse(body, 1);
    bool managed_win = false, managed_lose = false;
    for (const WinLoseRule &r : values.rules)
        (r.win ? managed_win : managed_lose) = true;
    if (!managed_win && !managed_lose)
        return issues;

    const size_t begin_pos = script_text.find(kBeginMarker);
    size_t end_pos = script_text.find(kEndMarker, begin_pos);
    end_pos = (end_pos == std::string::npos) ? begin_pos : end_pos + strlen(kEndMarker);
    size_t line_no = 0, pos = 0;
    while (pos <= script_text.size())
    {
        const size_t nl = script_text.find('\n', pos);
        const std::string line = trim(script_text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
        const bool outside = pos < begin_pos || pos >= end_pos;
        if (outside && ((line == "WIN_GAME" && managed_win) || (line == "LOSE_GAME" && managed_lose)))
        {
            DuplicateWinLose d;
            d.line = line_no;
            d.win = (line == "WIN_GAME");
            issues.push_back(d);
        }
        if (nl == std::string::npos)
            break;
        pos = nl + 1;
        line_no++;
    }
    return issues;
}

std::string script_setup_trim(const std::string &s) { return trim(s); }
std::vector<std::string> script_setup_split_args(const std::string &line) { return split_args(line); }
bool script_setup_parse_if_clause(const std::string &line, WinLoseClause &out) { return parse_if_clause(line, out); }

const char *const *script_setup_win_lose_operators(int64_t *count)
{
    if (count != nullptr)
        *count = (int64_t)(sizeof(kOperators) / sizeof(kOperators[0]));
    return kOperators;
}

const struct NamedCommand *script_setup_availability_desc(int64_t kind)
{
    switch (kind)
    {
    case AvailKind_Creature: return creature_desc;
    case AvailKind_Room:     return room_desc;
    case AvailKind_Magic:    return power_desc;
    case AvailKind_Trap:     return trap_desc;
    case AvailKind_Door:     return door_desc;
    default:                 return nullptr;
    }
}

static const char *script_setup_availability_item_name(int64_t kind, int64_t item)
{
    const struct NamedCommand *desc = script_setup_availability_desc(kind);
    if (desc == nullptr)
        return "";
    for (int64_t i = 0; desc[i].name != nullptr; i++)
    {
        if (desc[i].num == item)
            return desc[i].name;
    }
    return "";
}

AvailabilityEntry *script_setup_availability_find(ManagedSetupValues &values, int64_t kind, int64_t player, int64_t item)
{
    for (size_t i = 0; i < values.availability.size(); i++)
    {
        AvailabilityEntry &e = values.availability[i];
        if (e.kind == kind && e.player == player && e.item == item)
            return &e;
    }
    return nullptr;
}

std::string script_setup_extract_region(const std::string &script_text)
{
    size_t begin_pos = script_text.find(kBeginMarker);
    if (begin_pos == std::string::npos)
        return std::string();
    size_t body_start = script_text.find('\n', begin_pos);
    if (body_start == std::string::npos)
        return std::string();
    body_start++;
    size_t end_pos = script_text.find(kEndMarker, body_start);
    if (end_pos == std::string::npos || end_pos < body_start)
        return std::string();
    return script_text.substr(body_start, end_pos - body_start);
}

std::string script_setup_replace_region(const std::string &script_text, const std::string &new_body)
{
    size_t begin_pos = script_text.find(kBeginMarker);
    size_t body_start = (begin_pos != std::string::npos) ? script_text.find('\n', begin_pos) : std::string::npos;
    size_t end_pos = (body_start != std::string::npos) ? script_text.find(kEndMarker, body_start + 1) : std::string::npos;

    if (begin_pos == std::string::npos || body_start == std::string::npos || end_pos == std::string::npos)
    {
        // No existing (or malformed) markers -- insert a fresh block ahead
        // of everything else in the script.
        std::string block;
        block += kBeginMarker;
        block += "\n";
        block += new_body;
        block += kEndMarker;
        block += "\n\n";
        return block + script_text;
    }

    std::string result = script_text.substr(0, body_start + 1);
    result += new_body;
    result += script_text.substr(end_pos);
    return result;
}

int64_t script_setup_level_version(const std::string &script_text)
{
    // The engine pre-scans the whole file for LEVEL_VERSION before running
    // anything else (preload_script), so position is irrelevant; last wins.
    int64_t version = 0;
    size_t line_start = 0;
    while (line_start <= script_text.size())
    {
        const size_t line_end = script_text.find('\n', line_start);
        const std::string line = trim(script_text.substr(line_start,
            (line_end == std::string::npos) ? std::string::npos : line_end - line_start));
        if (line.compare(0, 13, "LEVEL_VERSION") == 0)
        {
            const std::vector<std::string> args = split_args(line);
            if (args.size() == 1 && !args[0].empty())
                version = atoi(args[0].c_str());
        }
        if (line_end == std::string::npos)
            break;
        line_start = line_end + 1;
    }
    return version;
}

ManagedSetupValues script_setup_parse(const std::string &managed_body, int64_t players, int64_t level_version)
{
    ManagedSetupValues values;
    values.generate_speed = 0;
    values.start_money.assign((size_t)((players > 0) ? players : 0), 0);
    values.max_creatures.assign((size_t)((players > 0) ? players : 0), 0);

    // Win/lose rule blocks: IF(..) [IF(..)...] WIN_GAME|LOSE_GAME ENDIF...
    // A block is a rule only if it holds nothing else.
    WinLoseRule pending;
    int64_t depth = 0;
    bool rule_ok = true;
    bool have_result = false;

    size_t line_start = 0;
    while (line_start <= managed_body.size())
    {
        size_t line_end = managed_body.find('\n', line_start);
        std::string line = trim(managed_body.substr(line_start,
            (line_end == std::string::npos) ? std::string::npos : line_end - line_start));
        line_start = (line_end == std::string::npos) ? managed_body.size() + 1 : line_end + 1;
        if (line.empty())
            continue;

        if (line.compare(0, 2, "IF") == 0 && line.size() > 2 && (line[2] == '(' || isspace((unsigned char)line[2])))
        {
            WinLoseClause c;
            if (depth == 0)
            {
                pending = WinLoseRule();
                rule_ok = true;
                have_result = false;
            }
            if (have_result || !parse_if_clause(line, c))
                rule_ok = false;
            else
                pending.clauses.push_back(c);
            depth++;
        }
        else if (depth > 0 && (line == "WIN_GAME" || line == "LOSE_GAME"))
        {
            if (have_result)
                rule_ok = false;
            pending.win = (line == "WIN_GAME");
            have_result = true;
        }
        else if (depth > 0 && line == "ENDIF")
        {
            depth--;
            if (depth == 0 && rule_ok && have_result && !pending.clauses.empty())
                values.rules.push_back(pending);
        }
        else if (depth > 0)
        {
            rule_ok = false; // some other command inside a block: not a plain rule
        }
        else if (line.compare(0, 18, "SET_GENERATE_SPEED") == 0)
        {
            std::vector<std::string> args = split_args(line);
            if (!args.empty())
                values.generate_speed = atoi(args[0].c_str());
        }
        else if (line.compare(0, 11, "START_MONEY") == 0)
        {
            std::vector<std::string> args = split_args(line);
            if (args.size() >= 2)
            {
                int64_t idx = parse_player_index(args[0]);
                if (idx >= 0 && idx < (int64_t)values.start_money.size())
                    values.start_money[(size_t)idx] = atoi(args[1].c_str());
            }
        }
        else if (line.compare(0, 13, "MAX_CREATURES") == 0)
        {
            std::vector<std::string> args = split_args(line);
            if (args.size() >= 2)
            {
                int64_t idx = parse_player_index(args[0]);
                if (idx >= 0 && idx < (int64_t)values.max_creatures.size())
                    values.max_creatures[(size_t)idx] = atoi(args[1].c_str());
            }
        }
        else if (line.compare(0, 20, "ADD_CREATURE_TO_POOL") == 0)
        {
            std::vector<std::string> args = split_args(line);
            if (args.size() >= 2)
            {
                int64_t kind = get_rid(creature_desc, args[0].c_str());
                if (kind > 0)
                    values.creature_pool.push_back(std::make_pair((ThingModel)kind, atoi(args[1].c_str())));
            }
        }
        else
        {
            for (int64_t kind = 0; kind < AvailKind_Count; kind++)
            {
                size_t len = strlen(kAvailCommandNames[kind]);
                if (line.compare(0, len, kAvailCommandNames[kind]) != 0)
                    continue;
                std::vector<std::string> args = split_args(line);
                if (args.size() < 4)
                    break;
                int64_t player = parse_availability_player(args[0]);
                int64_t item = get_rid(script_setup_availability_desc(kind), args[1].c_str());
                if (player == -2 || item <= 0)
                    break;
                int64_t a = atoi(args[2].c_str());
                int64_t b = atoi(args[3].c_str());
                if (level_version <= 0 && kind == AvailKind_Creature)
                {
                    // v0 CREATURE_AVAILABLE ignores arg 3, arg 4 is "available".
                    a = b;
                    b = 0;
                }
                // Later line for the same (kind, player, item) wins, matching
                // the engine's own last-writer-wins execution order.
                AvailabilityEntry *existing = script_setup_availability_find(values, kind, player, (int64_t)item);
                if (existing != nullptr)
                {
                    existing->a = a;
                    existing->b = b;
                }
                else
                {
                    AvailabilityEntry e = { kind, player, (int64_t)item, a, b };
                    values.availability.push_back(e);
                }
                break;
            }
        }
        // Anything else (blank, REM comment, unrecognized) -- skipped.
    }
    return values;
}

std::string script_setup_generate(const ManagedSetupValues &values, int64_t players, int64_t level_version)
{
    char line[256];
    std::string body;

    // 0 means "not set" for speed/gold/max creatures -- emitting an explicit
    // zero for a level that never had one would silently override the
    // engine's own default (a level with no managed block yet opens with
    // all zeros), so those lines are simply omitted.
    if (values.generate_speed != 0)
    {
        snprintf(line, sizeof(line), "SET_GENERATE_SPEED(%" PRId64 ")\n", (int64_t)(values.generate_speed));
        body += line;
    }

    for (int64_t i = 0; i < players; i++)
    {
        int64_t gold = (i < (int64_t)values.start_money.size()) ? values.start_money[(size_t)i] : 0;
        if (gold == 0)
            continue;
        snprintf(line, sizeof(line), "START_MONEY(PLAYER%" PRId64 ",%" PRId64 ")\n", (int64_t)(i), (int64_t)(gold));
        body += line;
    }
    for (int64_t i = 0; i < players; i++)
    {
        int64_t max_creatures = (i < (int64_t)values.max_creatures.size()) ? values.max_creatures[(size_t)i] : 0;
        if (max_creatures == 0)
            continue;
        snprintf(line, sizeof(line), "MAX_CREATURES(PLAYER%" PRId64 ",%" PRId64 ")\n", (int64_t)(i), (int64_t)(max_creatures));
        body += line;
    }
    for (size_t i = 0; i < values.creature_pool.size(); i++)
    {
        const char *name = creature_code_name(values.creature_pool[i].first);
        snprintf(line, sizeof(line), "ADD_CREATURE_TO_POOL(%s,%" PRId64 ")\n", name, (int64_t)(values.creature_pool[i].second));
        body += line;
    }
    // One line per (kind, player, item) -- TRAP/DOOR_AVAILABLE's amount
    // accumulates rather than overwrites, so duplicates would stack.
    // ALL_PLAYERS lines first, then per-player, so a later PLAYERn line
    // overrides the ALL_PLAYERS baseline, matching execution order.
    for (int64_t kind = 0; kind < AvailKind_Count; kind++)
    {
        for (int64_t pass = 0; pass < 2; pass++)
        {
            for (size_t i = 0; i < values.availability.size(); i++)
            {
                const AvailabilityEntry &e = values.availability[i];
                if (e.kind != kind || ((e.player < 0) != (pass == 0)))
                    continue;
                const char *item_name = script_setup_availability_item_name(kind, e.item);
                if (item_name[0] == '\0')
                    continue;
                char player_name[24];
                if (e.player < 0)
                    snprintf(player_name, sizeof(player_name), "ALL_PLAYERS");
                else
                    snprintf(player_name, sizeof(player_name), "PLAYER%" PRId64, (int64_t)(e.player));
                // v0 CREATURE_AVAILABLE(p,c,_,available) has no force flag.
                const bool v0_creature = (level_version <= 0) && (kind == AvailKind_Creature);
                snprintf(line, sizeof(line), "%s(%s,%s,%" PRId64 ",%" PRId64 ")\n", kAvailCommandNames[kind], player_name, item_name,
                    (int64_t)(e.a), (int64_t)(v0_creature ? e.a : e.b));
                body += line;
            }
        }
    }
    for (size_t r = 0; r < values.rules.size(); r++)
    {
        const WinLoseRule &rule = values.rules[r];
        if (rule.clauses.empty())
            continue;
        std::string indent;
        for (const WinLoseClause &c : rule.clauses)
        {
            snprintf(line, sizeof(line), "%sIF(PLAYER%" PRId64 ",%s %s %" PRId64 ")\n", indent.c_str(), (int64_t)(c.player), c.variable.c_str(),
                c.op.c_str(), (int64_t)(c.value));
            body += line;
            indent += "\t";
        }
        body += indent + (rule.win ? "WIN_GAME\n" : "LOSE_GAME\n");
        for (size_t i = rule.clauses.size(); i-- > 0;)
        {
            indent.pop_back();
            body += indent + "ENDIF\n";
        }
    }
    return body;
}
/******************************************************************************/
