/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_script_commands.cpp
 *     Pure logic for the Script > Commands window.
 * @par Purpose:
 *     See editor_script_commands.h.
 * @par Comment:
 *     The classic command set, its grouping and the summaries come from
 *     chapter 5.2 of "Dungeon Keeper Editor Manual". Everything else in
 *     the engine's command table is KeeperFX's own and is grouped by hand
 *     below; a command missing from the table lands in ScrGroup_Other
 *     rather than being hidden, so a newly added engine command still shows
 *     up in the window.
 */
#include "pre_inc.h"
#include "editor_script_commands.h"
#include "editor_script_message.h"
#include "post_inc.h"

#include <cctype>
#include <cstring>
#include <map>
#include <vector>

namespace {

struct CommandInfo
{
    const char *name;
    int group;
    bool classic;
    const char *summary; // classic commands only
};

const CommandInfo kCommands[] = {
    // --- Flow control & conditions (manual 5.2.2 Miscellaneous) ---
    {"REM", ScrGroup_Flow, true, "Comment: the game ignores the rest of the line."},
    {"WIN_GAME", ScrGroup_Flow, true, "The player has won the level. Use inside an IF."},
    {"LOSE_GAME", ScrGroup_Flow, true, "The player has lost the level. Use inside an IF."},
    {"IF", ScrGroup_Flow, true, "Run the commands up to ENDIF while a variable satisfies a comparison."},
    {"IF_ACTION_POINT", ScrGroup_Flow, true, "True once a creature of the player has stepped into the action point."},
    {"IF_AVAILABLE", ScrGroup_Flow, true, "Compare whether a room, spell, trap or door is available to the player (0 or 1)."},
    {"ENDIF", ScrGroup_Flow, true, "Ends the commands belonging to the last IF."},
    {"RESET_ACTION_POINT", ScrGroup_Flow, true, "Lets a triggered action point trigger again."},
    {"NEXT_COMMAND_REUSABLE", ScrGroup_Flow, true, "The next action command may fire every time its IF is true, not just once."},
    {"NEXT_COMMAND_REUSEABLE", ScrGroup_Flow, true, "Spelling used in the original manual for NEXT_COMMAND_REUSABLE."},
    {"IF_CONTROLS", ScrGroup_Flow, false, nullptr},
    {"IF_SLAB_OWNER", ScrGroup_Flow, false, nullptr},
    {"IF_SLAB_TYPE", ScrGroup_Flow, false, nullptr},
    {"IF_ALLIED", ScrGroup_Flow, false, nullptr},
    {"TRIGGER_ACTION_POINT", ScrGroup_Flow, false, nullptr},
    {"COUNT_CREATURES_AT_ACTION_POINT", ScrGroup_Flow, false, nullptr},
    {"RUN_AFTER_VICTORY", ScrGroup_Flow, false, nullptr},
    {"LEVEL_VERSION", ScrGroup_Flow, false, nullptr},
    {"RUN_LUA_CODE", ScrGroup_Flow, false, nullptr},

    // --- Flags, timers & variables ---
    {"SET_FLAG", ScrGroup_Flags, true, "Give a flag (FLAG0-FLAG7) of a player a value from 0 to 255."},
    {"SET_TIMER", ScrGroup_Flags, true, "Start a timer (TIMER0-TIMER7); it counts up by 1 every game turn."},
    {"ADD_TO_FLAG", ScrGroup_Flags, false, nullptr},
    {"RANDOMISE_FLAG", ScrGroup_Flags, false, nullptr},
    {"RANDOMIZE_FLAG", ScrGroup_Flags, false, nullptr},
    {"COMPUTE_FLAG", ScrGroup_Flags, false, nullptr},
    {"ADD_TO_TIMER", ScrGroup_Flags, false, nullptr},
    {"SET_CAMPAIGN_FLAG", ScrGroup_Flags, false, nullptr},
    {"ADD_TO_CAMPAIGN_FLAG", ScrGroup_Flags, false, nullptr},
    {"EXPORT_VARIABLE", ScrGroup_Flags, false, nullptr},
    {"DISPLAY_TIMER", ScrGroup_Flags, false, nullptr},
    {"DISPLAY_VARIABLE", ScrGroup_Flags, false, nullptr},
    {"DISPLAY_VARIABLE_WITH_LABEL", ScrGroup_Flags, false, nullptr},
    {"DISPLAY_COUNTDOWN", ScrGroup_Flags, false, nullptr},
    {"HIDE_TIMER", ScrGroup_Flags, false, nullptr},
    {"HIDE_VARIABLE", ScrGroup_Flags, false, nullptr},
    {"ADD_BONUS_TIME", ScrGroup_Flags, false, nullptr},
    {"BONUS_LEVEL_TIME", ScrGroup_Flags, false, nullptr},

    // --- Level setup (manual 5.2.3) ---
    {"SET_GENERATE_SPEED", ScrGroup_Setup, true, "Game turns between new creatures arriving through the portals."},
    {"START_MONEY", ScrGroup_Setup, true, "Gold the player starts with."},
    {"MAX_CREATURES", ScrGroup_Setup, true, "Most creatures a player can have before portals stop admitting more."},
    {"ALLY_PLAYERS", ScrGroup_Setup, true, "Make two players allies from the start."},
    {"ADD_GOLD_TO_PLAYER", ScrGroup_Setup, false, nullptr},
    {"SET_PLAYER_COLOR", ScrGroup_Setup, false, nullptr},
    {"SET_PLAYER_COLOUR", ScrGroup_Setup, false, nullptr},
    {"SET_PLAYER_MODIFIER", ScrGroup_Setup, false, nullptr},
    {"ADD_TO_PLAYER_MODIFIER", ScrGroup_Setup, false, nullptr},
    {"SET_GAME_RULE", ScrGroup_Setup, false, nullptr},
    {"SET_MUSIC", ScrGroup_Setup, false, nullptr},
    {"SET_TEXTURE", ScrGroup_Setup, false, nullptr},
    {"SET_HAND_GRAPHIC", ScrGroup_Setup, false, nullptr},
    {"SET_NEXT_LEVEL", ScrGroup_Setup, false, nullptr},
    {"SHOW_BONUS_LEVEL", ScrGroup_Setup, false, nullptr},
    {"HIDE_BONUS_LEVEL", ScrGroup_Setup, false, nullptr},
    {"SET_LEVEL_ENSIGN", ScrGroup_Setup, false, nullptr},
    {"LOCK_POSSESSION", ScrGroup_Setup, false, nullptr},
    {"SET_HEART_HEALTH", ScrGroup_Setup, false, nullptr},
    {"ADD_HEART_HEALTH", ScrGroup_Setup, false, nullptr},
    {"HIDE_HERO_GATE", ScrGroup_Setup, false, nullptr},

    // --- Computer players ---
    {"COMPUTER_PLAYER", ScrGroup_Computer, true, "Assign a computer player to an enemy Dungeon Heart."},
    {"SET_COMPUTER_GLOBALS", ScrGroup_Computer, false, nullptr},
    {"SET_COMPUTER_CHECKS", ScrGroup_Computer, false, nullptr},
    {"SET_COMPUTER_EVENT", ScrGroup_Computer, false, nullptr},
    {"SET_COMPUTER_PROCESS", ScrGroup_Computer, false, nullptr},
    {"COMPUTER_DIG_TO_LOCATION", ScrGroup_Computer, false, nullptr},

    // --- Creatures, spells, traps & doors available (manual 5.2.4) ---
    {"ADD_CREATURE_TO_POOL", ScrGroup_Availability, true, "Add creatures of a kind to the pool all portals draw from."},
    {"CREATURE_AVAILABLE", ScrGroup_Availability, true, "Whether a creature can come through the player's portal, and whether it already does."},
    {"ROOM_AVAILABLE", ScrGroup_Availability, true, "Whether a room can be built by the player now / is researchable."},
    {"MAGIC_AVAILABLE", ScrGroup_Availability, true, "Whether a spell can be cast by the player now / is researchable."},
    {"DOOR_AVAILABLE", ScrGroup_Availability, true, "Whether the player can build a door, and how many are ready."},
    {"TRAP_AVAILABLE", ScrGroup_Availability, true, "Whether the player can build a trap, and how many are ready."},
    {"DEAD_CREATURES_RETURN_TO_POOL", ScrGroup_Availability, false, nullptr},
    {"CREATURE_ENTRANCE_LEVEL", ScrGroup_Availability, false, nullptr},

    // --- Research (manual 5.2.6) ---
    {"RESEARCH", ScrGroup_Research, true, "Set the research value of a room or spell for a player."},
    {"RESEARCH_ORDER", ScrGroup_Research, false, nullptr},

    // --- Manipulating creatures (manual 5.2.5) ---
    {"SET_CREATURE_MAX_LEVEL", ScrGroup_Creatures, true, "Highest experience level a kind of creature can train to."},
    {"SET_CREATURE_STRENGTH", ScrGroup_Creatures, true, "Strength of every creature of that kind on the level."},
    {"SET_CREATURE_HEALTH", ScrGroup_Creatures, true, "Health of every creature of that kind on the level."},
    {"SET_CREATURE_ARMOUR", ScrGroup_Creatures, true, "Armour of every creature of that kind on the level."},
    {"SET_CREATURE_FEAR_WOUNDED", ScrGroup_Creatures, false, nullptr},
    {"SET_CREATURE_FEAR_STRONGER", ScrGroup_Creatures, false, nullptr},
    {"SET_CREATURE_FEARSOME_FACTOR", ScrGroup_Creatures, false, nullptr},
    {"SET_CREATURE_PROPERTY", ScrGroup_Creatures, false, nullptr},
    {"SET_CREATURE_TENDENCIES", ScrGroup_Creatures, false, nullptr},
    {"SET_CREATURE_INSTANCE", ScrGroup_Creatures, false, nullptr},
    {"SET_INCREASE_ON_EXPERIENCE", ScrGroup_Creatures, false, nullptr},
    {"SET_HAND_RULE", ScrGroup_Creatures, false, nullptr},
    {"SET_DIGGER", ScrGroup_Creatures, false, nullptr},
    {"SET_SACRIFICE_RECIPE", ScrGroup_Creatures, false, nullptr},
    {"REMOVE_SACRIFICE_RECIPE", ScrGroup_Creatures, false, nullptr},
    {"SWAP_CREATURE", ScrGroup_Creatures, false, nullptr},
    {"KILL_CREATURE", ScrGroup_Creatures, false, nullptr},
    {"TRANSFER_CREATURE", ScrGroup_Creatures, false, nullptr},
    {"CHANGE_CREATURES_ANNOYANCE", ScrGroup_Creatures, false, nullptr},
    {"LEVEL_UP_CREATURE", ScrGroup_Creatures, false, nullptr},
    {"LEVEL_UP_PLAYERS_CREATURES", ScrGroup_Creatures, false, nullptr},
    {"CHANGE_CREATURE_OWNER", ScrGroup_Creatures, false, nullptr},
    {"MOVE_CREATURE", ScrGroup_Creatures, false, nullptr},

    // --- Adding creatures, parties & objects (manual 5.2.7) ---
    {"ADD_CREATURE_TO_LEVEL", ScrGroup_Spawn, true, "Add creatures at an action point (or hero gate)."},
    {"ADD_TUNNELLER_TO_LEVEL", ScrGroup_Spawn, true, "Add a Tunneller Dwarf that digs straight to a target."},
    {"CREATE_PARTY", ScrGroup_Spawn, true, "Declare a party name; fill it with ADD_TO_PARTY."},
    {"ADD_TO_PARTY", ScrGroup_Spawn, true, "Add a creature (level, gold, objective) to a declared party."},
    {"ADD_TUNNELLER_PARTY_TO_LEVEL", ScrGroup_Spawn, true, "Add a party led by a Tunneller Dwarf, digging to a target."},
    {"ADD_PARTY_TO_LEVEL", ScrGroup_Spawn, true, "Add a whole party at an action point."},
    {"DELETE_FROM_PARTY", ScrGroup_Spawn, false, nullptr},
    {"ADD_OBJECT_TO_LEVEL", ScrGroup_Spawn, false, nullptr},
    {"ADD_OBJECT_TO_LEVEL_AT_POS", ScrGroup_Spawn, false, nullptr},
    {"ADD_EFFECT_GENERATOR_TO_LEVEL", ScrGroup_Spawn, false, nullptr},
    {"PLACE_DOOR", ScrGroup_Spawn, false, nullptr},
    {"PLACE_TRAP", ScrGroup_Spawn, false, nullptr},
    {"SET_DOOR", ScrGroup_Spawn, false, nullptr},
    {"CREATE_EFFECT", ScrGroup_Spawn, false, nullptr},
    {"CREATE_EFFECT_AT_POS", ScrGroup_Spawn, false, nullptr},
    {"CREATE_EFFECTS_LINE", ScrGroup_Spawn, false, nullptr},

    // --- Objectives & messages (manual 5.2.8) ---
    {"QUICK_OBJECTIVE", ScrGroup_Objectives, true, "Show an objective (numbered 0-255) with your own text."},
    {"QUICK_INFORMATION", ScrGroup_Objectives, true, "Show an information pop-up (numbered 0-255) with your own text."},
    {"QUICK_OBJECTIVE_WITH_POS", ScrGroup_Objectives, false, nullptr},
    {"QUICK_INFORMATION_WITH_POS", ScrGroup_Objectives, false, nullptr},
    {"QUICK_PLAYER_OBJECTIVE", ScrGroup_Objectives, false, nullptr},
    {"QUICK_PLAYER_OBJECTIVE_WITH_POS", ScrGroup_Objectives, false, nullptr},
    {"QUICK_PLAYER_INFORMATION", ScrGroup_Objectives, false, nullptr},
    {"QUICK_PLAYER_INFORMATION_WITH_POS", ScrGroup_Objectives, false, nullptr},
    {"DISPLAY_OBJECTIVE", ScrGroup_Objectives, false, nullptr},
    {"DISPLAY_OBJECTIVE_WITH_POS", ScrGroup_Objectives, false, nullptr},
    {"DISPLAY_INFORMATION", ScrGroup_Objectives, false, nullptr},
    {"DISPLAY_INFORMATION_WITH_POS", ScrGroup_Objectives, false, nullptr},
    {"DISPLAY_PLAYER_OBJECTIVE", ScrGroup_Objectives, false, nullptr},
    {"DISPLAY_PLAYER_OBJECTIVE_WITH_POS", ScrGroup_Objectives, false, nullptr},
    {"DISPLAY_PLAYER_INFORMATION", ScrGroup_Objectives, false, nullptr},
    {"DISPLAY_PLAYER_INFORMATION_WITH_POS", ScrGroup_Objectives, false, nullptr},
    {"DISPLAY_MESSAGE", ScrGroup_Objectives, false, nullptr},
    {"QUICK_MESSAGE", ScrGroup_Objectives, false, nullptr},
    {"CLEAR_MESSAGE", ScrGroup_Objectives, false, nullptr},
    {"HEART_LOST_OBJECTIVE", ScrGroup_Objectives, false, nullptr},
    {"HEART_LOST_QUICK_OBJECTIVE", ScrGroup_Objectives, false, nullptr},
    {"PRINT", ScrGroup_Objectives, false, nullptr},
    {"MESSAGE", ScrGroup_Objectives, false, nullptr},
    {"PLAY_MESSAGE", ScrGroup_Objectives, false, nullptr},
    {"TUTORIAL_FLASH_BUTTON", ScrGroup_Objectives, false, nullptr},
    {"SET_BOX_TOOLTIP", ScrGroup_Objectives, false, nullptr},
    {"SET_BOX_TOOLTIP_ID", ScrGroup_Objectives, false, nullptr},

    // --- Map, slabs & powers ---
    {"REVEAL_MAP_RECT", ScrGroup_MapPowers, false, nullptr},
    {"CONCEAL_MAP_RECT", ScrGroup_MapPowers, false, nullptr},
    {"REVEAL_MAP_LOCATION", ScrGroup_MapPowers, false, nullptr},
    {"TAG_MAP_RECT", ScrGroup_MapPowers, false, nullptr},
    {"UNTAG_MAP_RECT", ScrGroup_MapPowers, false, nullptr},
    {"ZOOM_TO_LOCATION", ScrGroup_MapPowers, false, nullptr},
    {"CHANGE_SLAB_OWNER", ScrGroup_MapPowers, false, nullptr},
    {"CHANGE_SLAB_TYPE", ScrGroup_MapPowers, false, nullptr},
    {"CHANGE_SLAB_TEXTURE", ScrGroup_MapPowers, false, nullptr},
    {"USE_POWER", ScrGroup_MapPowers, false, nullptr},
    {"USE_POWER_AT_POS", ScrGroup_MapPowers, false, nullptr},
    {"USE_POWER_AT_LOCATION", ScrGroup_MapPowers, false, nullptr},
    {"USE_POWER_ON_CREATURE", ScrGroup_MapPowers, false, nullptr},
    {"USE_POWER_ON_PLAYERS_CREATURES", ScrGroup_MapPowers, false, nullptr},
    {"USE_SPELL_ON_CREATURE", ScrGroup_MapPowers, false, nullptr},
    {"USE_SPELL_ON_PLAYERS_CREATURES", ScrGroup_MapPowers, false, nullptr},
    {"USE_SPECIAL_INCREASE_LEVEL", ScrGroup_MapPowers, false, nullptr},
    {"USE_SPECIAL_MULTIPLY_CREATURES", ScrGroup_MapPowers, false, nullptr},
    {"USE_SPECIAL_MAKE_SAFE", ScrGroup_MapPowers, false, nullptr},
    {"USE_SPECIAL_LOCATE_HIDDEN_WORLD", ScrGroup_MapPowers, false, nullptr},
    {"USE_SPECIAL_TRANSFER_CREATURE", ScrGroup_MapPowers, false, nullptr},
    {"MAKE_SAFE", ScrGroup_MapPowers, false, nullptr},
    {"MAKE_UNSAFE", ScrGroup_MapPowers, false, nullptr},
    {"LOCATE_HIDDEN_WORLD", ScrGroup_MapPowers, false, nullptr},

    // --- KeeperFX configuration ---
    {"SET_ROOM_CONFIGURATION", ScrGroup_Config, false, nullptr},
    {"SET_TRAP_CONFIGURATION", ScrGroup_Config, false, nullptr},
    {"SET_DOOR_CONFIGURATION", ScrGroup_Config, false, nullptr},
    {"SET_OBJECT_CONFIGURATION", ScrGroup_Config, false, nullptr},
    {"SET_CREATURE_CONFIGURATION", ScrGroup_Config, false, nullptr},
    {"SET_POWER_CONFIGURATION", ScrGroup_Config, false, nullptr},
    {"SET_EFFECT_GENERATOR_CONFIGURATION", ScrGroup_Config, false, nullptr},
    {"NEW_TRAP_TYPE", ScrGroup_Config, false, nullptr},
    {"NEW_OBJECT_TYPE", ScrGroup_Config, false, nullptr},
    {"NEW_ROOM_TYPE", ScrGroup_Config, false, nullptr},
    {"NEW_CREATURE_TYPE", ScrGroup_Config, false, nullptr},
    {"COPY_CREATURE_TYPE", ScrGroup_Config, false, nullptr},
};

const char *const kGroupTitles[ScrGroup_Count] = {
    "Flow control & conditions",
    "Flags, timers & variables",
    "Level setup",
    "Computer players",
    "Creatures, spells, traps & doors",
    "Research",
    "Manipulating creatures",
    "Adding creatures, parties & objects",
    "Objectives & messages",
    "Map, slabs & powers",
    "KeeperFX configuration",
    "Other",
};

const CommandInfo *find_command(const std::string &name)
{
    static std::map<std::string, const CommandInfo *> index;
    if (index.empty())
        for (const CommandInfo &c : kCommands)
            index[c.name] = &c;
    auto it = index.find(name);
    return (it == index.end()) ? nullptr : it->second;
}

std::string trim(const std::string &s)
{
    size_t b = 0, e = s.size();
    while (b < e && std::isspace((unsigned char)s[b]))
        b++;
    while (e > b && std::isspace((unsigned char)s[e - 1]))
        e--;
    return s.substr(b, e - b);
}

const char *arg_name(char letter)
{
    switch (std::toupper((unsigned char)letter))
    {
        case 'A': return "text";
        case 'N': return "number";
        case 'C': return "creature";
        case 'P': return "player";
        case 'R': return "room";
        case 'L': return "location";
        case 'O': return "comparison";
        case 'S': return "slab";
        case 'B': return "0/1";
        default:  return "value";
    }
}

const char *arg_placeholder(char letter)
{
    switch (std::toupper((unsigned char)letter))
    {
        case 'A': return "NAME";
        case 'N': return "0";
        case 'C': return "CREATURE";
        case 'P': return "PLAYER0";
        case 'R': return "ROOM";
        case 'L': return "PLAYER0";
        case 'O': return "==";
        case 'S': return "SLAB";
        case 'B': return "1";
        default:  return "0";
    }
}

// Argument letters with the engine's padding and modifiers ('!' extended,
// '+' repeatable) stripped off: each remaining char is one argument.
std::string arg_letters(const std::string &args)
{
    std::string out;
    for (char c : args)
        if (std::isalpha((unsigned char)c))
            out += c;
    return out;
}

std::vector<std::string> split_lines(const std::string &text)
{
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= text.size())
    {
        size_t nl = text.find('\n', start);
        if (nl == std::string::npos)
        {
            lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, nl - start));
        start = nl + 1;
    }
    return lines;
}

std::string leading_whitespace(const std::string &line)
{
    size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
        i++;
    return line.substr(0, i);
}

bool starts_with_word(const std::string &s, const char *word)
{
    size_t n = std::strlen(word);
    if (s.compare(0, n, word) != 0)
        return false;
    return s.size() == n || !(std::isalnum((unsigned char)s[n]) || s[n] == '_');
}

bool is_if_line(const std::string &trimmed)
{
    // IF(...) and IF_ACTION_POINT(...) etc, but not ENDIF.
    return trimmed.compare(0, 2, "IF") == 0
        && (trimmed.size() == 2 || trimmed[2] == '(' || trimmed[2] == '_' || std::isspace((unsigned char)trimmed[2]));
}

std::string dedent(const std::string &indent)
{
    if (indent.empty())
        return indent;
    if (indent.back() == '\t')
        return indent.substr(0, indent.size() - 1);
    size_t drop = indent.size() < 4 ? indent.size() : 4;
    return indent.substr(0, indent.size() - drop);
}

} // namespace

const char *editor_script_group_title(int group)
{
    return ((group >= 0) && (group < ScrGroup_Count)) ? kGroupTitles[group] : kGroupTitles[ScrGroup_Other];
}

int editor_script_command_group(const std::string &name)
{
    const CommandInfo *c = find_command(name);
    return c ? c->group : ScrGroup_Other;
}

bool editor_script_command_is_classic(const std::string &name)
{
    const CommandInfo *c = find_command(name);
    return c && c->classic;
}

bool editor_script_command_is_flow(const std::string &name)
{
    if (name.compare(0, 2, "IF") == 0 && (name.size() == 2 || name[2] == '_'))
        return true;
    return name == "ENDIF" || name == "WIN_GAME" || name == "LOSE_GAME" || name == "REM"
        || name == "NEXT_COMMAND_REUSABLE" || name == "NEXT_COMMAND_REUSEABLE";
}

const char *editor_script_command_summary(const std::string &name)
{
    const CommandInfo *c = find_command(name);
    return (c && c->summary) ? c->summary : "";
}

std::string editor_script_command_signature(const std::string &name, const std::string &args)
{
    std::string letters = arg_letters(args);
    if (letters.empty())
        return name;
    std::string out = name + "(";
    for (size_t i = 0; i < letters.size(); i++)
    {
        if (i)
            out += ", ";
        bool optional = std::islower((unsigned char)letters[i]) != 0;
        out += optional ? "[" : "";
        out += arg_name(letters[i]);
        out += optional ? "]" : "";
    }
    if (args.find('+') != std::string::npos)
        out += ", ...";
    out += ")";
    return out;
}

std::string editor_script_command_template(const std::string &name, const std::string &args)
{
    std::string letters = arg_letters(args);
    std::string required;
    for (char c : letters)
    {
        if (std::islower((unsigned char)c))
            break; // optional arguments are always trailing
        if (!required.empty())
            required += ",";
        required += arg_placeholder(c);
    }
    if (required.empty())
        return name;
    return name + "(" + required + ")";
}

ScriptInsertResult editor_script_insert_command(const std::string &script_text, size_t cursor_line, const std::string &command)
{
    const std::string eol = (script_text.find("\r\n") != std::string::npos) ? "\r\n" : "\n";
    std::vector<std::string> lines = split_lines(script_text);
    if (cursor_line >= lines.size())
        cursor_line = lines.size() - 1;

    size_t safe = editor_script_safe_insert_line(script_text, cursor_line);
    bool moved_out_of_region = (safe != cursor_line);
    size_t ref = moved_out_of_region ? safe - 1 : cursor_line; // the line we insert after
    if (ref >= lines.size())
        ref = lines.size() - 1;

    ScriptInsertResult result;
    std::string trimmed_ref = trim(lines[ref]);
    if (!moved_out_of_region && trimmed_ref.empty())
    {
        // Blank line: fill it in place, keeping whatever indentation it has.
        std::string indent = leading_whitespace(lines[ref]);
        lines[ref] = indent + command;
        result.line = ref;
    }
    else
    {
        std::string indent = moved_out_of_region ? std::string() : leading_whitespace(lines[ref]);
        if (!moved_out_of_region)
        {
            if (is_if_line(trimmed_ref) && !starts_with_word(command, "ENDIF"))
                indent += "\t";
            else if (starts_with_word(command, "ENDIF") && !is_if_line(trimmed_ref))
                indent = dedent(indent);
        }
        lines.insert(lines.begin() + (ref + 1), indent + command);
        result.line = ref + 1;
    }

    // Lines of a CRLF script keep their '\r' (split on '\n' only); lines we
    // touched or added need one too, except the very last (no terminator).
    if (eol == "\r\n")
    {
        for (size_t i = 0; i + 1 < lines.size(); i++)
            if (lines[i].empty() || lines[i].back() != '\r')
                lines[i] += '\r';
    }
    std::string out;
    for (size_t i = 0; i < lines.size(); i++)
    {
        out += lines[i];
        if (i + 1 < lines.size())
            out += '\n';
    }
    result.text = out;
    return result;
}
