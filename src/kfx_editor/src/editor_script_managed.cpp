/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_script_managed.cpp
 *     See editor_script_managed.h.
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
#include "editor_script_managed.h"

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
int parse_player_index(const std::string &token)
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
int parse_availability_player(const std::string &token)
{
    if (token == "ALL_PLAYERS")
        return -1;
    int idx = parse_player_index(token);
    return (idx >= 0) ? idx : -2;
}

} // namespace

const char *editor_availability_command_name(int kind)
{
    return ((kind >= 0) && (kind < AvailKind_Count)) ? kAvailCommandNames[kind] : "";
}

const struct NamedCommand *editor_availability_desc(int kind)
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

const char *editor_availability_item_name(int kind, int item)
{
    const struct NamedCommand *desc = editor_availability_desc(kind);
    if (desc == nullptr)
        return "";
    for (int i = 0; desc[i].name != nullptr; i++)
    {
        if (desc[i].num == item)
            return desc[i].name;
    }
    return "";
}

AvailabilityEntry *editor_availability_find(ManagedSetupValues &values, int kind, int player, int item)
{
    for (size_t i = 0; i < values.availability.size(); i++)
    {
        AvailabilityEntry &e = values.availability[i];
        if (e.kind == kind && e.player == player && e.item == item)
            return &e;
    }
    return nullptr;
}

std::string editor_script_extract_managed_region(const std::string &script_text)
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

std::string editor_script_replace_managed_region(const std::string &script_text, const std::string &new_body)
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

ManagedSetupValues editor_script_parse_managed_setup(const std::string &managed_body, int players)
{
    ManagedSetupValues values;
    values.generate_speed = 0;
    values.start_money.assign((size_t)((players > 0) ? players : 0), 0);
    values.max_creatures.assign((size_t)((players > 0) ? players : 0), 0);

    size_t line_start = 0;
    while (line_start <= managed_body.size())
    {
        size_t line_end = managed_body.find('\n', line_start);
        std::string line = trim(managed_body.substr(line_start,
            (line_end == std::string::npos) ? std::string::npos : line_end - line_start));
        line_start = (line_end == std::string::npos) ? managed_body.size() + 1 : line_end + 1;
        if (line.empty())
            continue;

        if (line.compare(0, 18, "SET_GENERATE_SPEED") == 0)
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
                int idx = parse_player_index(args[0]);
                if (idx >= 0 && idx < (int)values.start_money.size())
                    values.start_money[(size_t)idx] = atoi(args[1].c_str());
            }
        }
        else if (line.compare(0, 13, "MAX_CREATURES") == 0)
        {
            std::vector<std::string> args = split_args(line);
            if (args.size() >= 2)
            {
                int idx = parse_player_index(args[0]);
                if (idx >= 0 && idx < (int)values.max_creatures.size())
                    values.max_creatures[(size_t)idx] = atoi(args[1].c_str());
            }
        }
        else if (line.compare(0, 20, "ADD_CREATURE_TO_POOL") == 0)
        {
            std::vector<std::string> args = split_args(line);
            if (args.size() >= 2)
            {
                long kind = get_rid(creature_desc, args[0].c_str());
                if (kind > 0)
                    values.creature_pool.push_back(std::make_pair((ThingModel)kind, atoi(args[1].c_str())));
            }
        }
        else
        {
            for (int kind = 0; kind < AvailKind_Count; kind++)
            {
                size_t len = strlen(kAvailCommandNames[kind]);
                if (line.compare(0, len, kAvailCommandNames[kind]) != 0)
                    continue;
                std::vector<std::string> args = split_args(line);
                if (args.size() < 4)
                    break;
                int player = parse_availability_player(args[0]);
                long item = get_rid(editor_availability_desc(kind), args[1].c_str());
                if (player == -2 || item <= 0)
                    break;
                // Later line for the same (kind, player, item) wins, matching
                // the engine's own last-writer-wins execution order.
                AvailabilityEntry *existing = editor_availability_find(values, kind, player, (int)item);
                if (existing != nullptr)
                {
                    existing->a = atoi(args[2].c_str());
                    existing->b = atoi(args[3].c_str());
                }
                else
                {
                    AvailabilityEntry e = { kind, player, (int)item, atoi(args[2].c_str()), atoi(args[3].c_str()) };
                    values.availability.push_back(e);
                }
                break;
            }
        }
        // Anything else (blank, REM comment, unrecognized) -- skipped.
    }
    return values;
}

std::string editor_script_generate_managed_setup(const ManagedSetupValues &values, int players)
{
    char line[256];
    std::string body;

    // 0 means "not set" for speed/gold/max creatures -- emitting an explicit
    // zero for a level that never had one would silently override the
    // engine's own default (a level with no managed block yet opens with
    // all zeros), so those lines are simply omitted.
    if (values.generate_speed != 0)
    {
        snprintf(line, sizeof(line), "SET_GENERATE_SPEED(%d)\n", values.generate_speed);
        body += line;
    }

    for (int i = 0; i < players; i++)
    {
        int gold = (i < (int)values.start_money.size()) ? values.start_money[(size_t)i] : 0;
        if (gold == 0)
            continue;
        snprintf(line, sizeof(line), "START_MONEY(PLAYER%d,%d)\n", i, gold);
        body += line;
    }
    for (int i = 0; i < players; i++)
    {
        int max_creatures = (i < (int)values.max_creatures.size()) ? values.max_creatures[(size_t)i] : 0;
        if (max_creatures == 0)
            continue;
        snprintf(line, sizeof(line), "MAX_CREATURES(PLAYER%d,%d)\n", i, max_creatures);
        body += line;
    }
    for (size_t i = 0; i < values.creature_pool.size(); i++)
    {
        const char *name = creature_code_name(values.creature_pool[i].first);
        snprintf(line, sizeof(line), "ADD_CREATURE_TO_POOL(%s,%d)\n", name, values.creature_pool[i].second);
        body += line;
    }
    // One line per (kind, player, item) -- TRAP/DOOR_AVAILABLE's amount
    // accumulates rather than overwrites, so duplicates would stack.
    // ALL_PLAYERS lines first, then per-player, so a later PLAYERn line
    // overrides the ALL_PLAYERS baseline, matching execution order.
    for (int kind = 0; kind < AvailKind_Count; kind++)
    {
        for (int pass = 0; pass < 2; pass++)
        {
            for (size_t i = 0; i < values.availability.size(); i++)
            {
                const AvailabilityEntry &e = values.availability[i];
                if (e.kind != kind || ((e.player < 0) != (pass == 0)))
                    continue;
                const char *item_name = editor_availability_item_name(kind, e.item);
                if (item_name[0] == '\0')
                    continue;
                char player_name[24];
                if (e.player < 0)
                    snprintf(player_name, sizeof(player_name), "ALL_PLAYERS");
                else
                    snprintf(player_name, sizeof(player_name), "PLAYER%d", e.player);
                snprintf(line, sizeof(line), "%s(%s,%s,%d,%d)\n", kAvailCommandNames[kind], player_name, item_name, e.a, e.b);
                body += line;
            }
        }
    }
    return body;
}
/******************************************************************************/
