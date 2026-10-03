/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file lvl_script_lib.c
 *     collection of functions used by multiple files under lvl_script_*
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 * @author   KeeperFX Team
 */
/******************************************************************************/
#include "pre_inc.h"

#include "globals.h"
#include "config_creature.h"
#include "creature_states_pray.h"
#include "custom_sprites.h"
#include "sprites.h"
#include "dungeon_data.h"
#include "player_utils.h"
#include "lvl_filesdk1.h"
#include "lvl_script_lib.h"
#include "lvl_script_conditions.h"
#include "room_util.h"
#include "thing_corpses.h"
#include "thing_factory.h"
#include "thing_navigate.h"
#include "thing_physics.h"
#include "creature_instances.h"
#include "kfx_config_state.h"
#include "kfx_game_state.h"
#include "game_merge.h"
#include "lvl_script.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ScriptValue *allocate_script_value(void)
{
    if (kfx_game_state.script.values_num >= SCRIPT_VALUES_COUNT)
        return NULL;
    struct ScriptValue* value = &kfx_game_state.script.values[kfx_game_state.script.values_num];
    kfx_game_state.script.values_num++;
    return value;
}

/** A command's script value with three values (the original game's commands keep theirs this way): run at once at the
 *  top level, kept for later inside an IF (or after NEXT_COMMAND_REUSABLE). */
void command_add_value(uint64_t var_index, uint64_t plr_range_id, int64_t param1, int64_t param2, int64_t param3)
{
    ALLOCATE_SCRIPT_VALUE(var_index, plr_range_id);

    value->longs[0] = param1;
    value->longs[1] = param2;
    value->longs[2] = param3;

    if ((get_script_current_condition() == CONDITION_ALWAYS) && (next_command_reusable == 0))
    {
        script_process_value(var_index, plr_range_id, value);
        return;
    }
}

void command_init_value(struct ScriptValue* value, uint64_t var_index, uint64_t plr_range_id)
{
    set_flag_value(value->flags, TrgF_REUSABLE, next_command_reusable);
    clear_flag(value->flags, TrgF_DISABLED);
    value->valtype = var_index;
    value->plyr_range = plr_range_id;
    value->condit_idx = get_script_current_condition();
}

// For dynamic strings
int64_t script_strdup(const char *src)
{
    // TODO: add string deduplication to save space

    const int64_t offset = kfx_game_state.script.next_string_offset;
    const int64_t remaining_size = sizeof(kfx_game_state.script.strings) - offset;
    const int64_t string_size = strlen(src) + 1;
    if (string_size >= remaining_size)
    {
        return -1;
    }
    memcpy(&kfx_game_state.script.strings[offset], src, string_size);
    kfx_game_state.script.next_string_offset += string_size;
    return offset;
}

const char * script_strval(int64_t offset)
{
    if (offset >= sizeof(kfx_game_state.script.strings))
    {
        return NULL;
    }
    return &kfx_game_state.script.strings[offset];
}

struct Thing *script_process_new_object(ThingModel tngmodel, MapSubtlCoord stl_x, MapSubtlCoord stl_y, int64_t arg, PlayerNumber plyr_idx, int64_t move_angle)
{
    struct Coord3d pos;
    pos.x.val = subtile_coord_center(stl_x);
    pos.y.val = subtile_coord_center(stl_y);
    pos.z.val = get_floor_height_at(&pos);
    struct Thing* thing = create_object(&pos, tngmodel, plyr_idx, -1);
    if (thing_is_invalid(thing))
    {
        ERRORLOG("Couldn't create %s at location %" PRId64 ", %" PRId64,thing_class_and_model_name(TCls_Object, tngmodel),(int64_t)(stl_x), (int64_t)(stl_y));
        return INVALID_THING;
    }
    thing->move_angle_xy = move_angle;
    if (thing_is_dungeon_heart(thing))
    {
        struct Dungeon* dungeon = get_dungeon(thing->owner);
        if (dungeon->backup_heart_idx == 0)
        {
            dungeon->backup_heart_idx = thing->index;
        } else
        {
            struct Thing* backup = thing_get(dungeon->backup_heart_idx);
            if (!thing_is_dungeon_heart(backup))
            {
                ERRORLOG("%s had invalid backup heart %s", player_code_name(plyr_idx), thing_model_name(backup));
                dungeon->backup_heart_idx = thing->index;
            }
        }
    }
    // Try to move thing out of the solid wall if it's inside one
    if (thing_in_wall_at(thing, &thing->mappos))
    {
        if (!move_creature_to_nearest_valid_position(thing)) {
            ERRORLOG("The %s was created in wall, removing",thing_model_name(thing));
            destroy_object(thing);
            return INVALID_THING;
        }
    }
    if (thing_is_special_box(thing) && !thing_is_hardcoded_special_box(thing))
    {
        thing->custom_box.box_kind = (unsigned char)arg;
    }
    switch (tngmodel)
    {
        case ObjMdl_GoldChest:
        case ObjMdl_GoldPot:
        case ObjMdl_Goldl:
        case ObjMdl_GoldBag:
            thing->valuable.gold_stored = arg;
            break;
        default:
            struct ObjectConfigStats* objst = get_object_model_stats(tngmodel);
            if (objst->genre == OCtg_GoldHoard)
            {
                if (arg > 0)
                {
                    thing->valuable.gold_stored = arg;
                }
                check_and_asimilate_thing_by_room(thing);
            }
    }
    return thing;
}

struct Thing* script_process_new_effectgen(ThingModel tngmodel, TbMapLocation location, int64_t range)
{
    struct Coord3d pos;
    const unsigned char tngclass = TCls_EffectGen;
    if (!get_coords_at_location(&pos, location, false))
    {
        ERRORLOG("Couldn't find location %" PRId64 " to create %s", (int64_t)location, thing_class_and_model_name(tngclass, tngmodel));
        return INVALID_THING;
    }
    SlabCodedCoords place_slbnum = get_slab_number(subtile_slab(pos.x.stl.num), subtile_slab(pos.y.stl.num));
    struct Thing* thing = create_thing(&pos, tngclass, tngmodel, kfx_config_state.neutral_player_num, place_slbnum);
    if (thing_is_invalid(thing))
    {
        ERRORLOG("Couldn't create %s at location %" PRId64, thing_class_and_model_name(tngclass, tngmodel), (int64_t)location);
        return INVALID_THING;
    }
    thing->effect_generator.range = range;
    thing->mappos.z.val = get_thing_height_at(thing, &thing->mappos);

    // Try to move thing out of the solid wall if it's inside one
    if (thing_in_wall_at(thing, &thing->mappos))
    {
        if (!move_creature_to_nearest_valid_position(thing)) {
            ERRORLOG("The %s was created in wall, removing", thing_model_name(thing));
            delete_thing_structure(thing, 0);
            return INVALID_THING;
        }
    }
    return thing;
}

struct Thing* script_process_new_corpse(ThingModel tngmodel, MapSubtlCoord stl_x, MapSubtlCoord stl_y, PlayerNumber plyr_idx, CrtrExpLevel exp_level, TbBool dying)
{
    struct Coord3d pos;
    pos.x.val = subtile_coord_center(stl_x);
    pos.y.val = subtile_coord_center(stl_y);
    pos.z.val = get_floor_height_at(&pos);

    int64_t crpscondition = DCrSt_LongDead;
    if (dying)
    {
        crpscondition = DCrSt_Dying;
    }

    struct Thing* thing = create_dead_creature(&pos, tngmodel, crpscondition, plyr_idx, exp_level);
    if (thing_is_invalid(thing))
    {
        ERRORLOG("Couldn't create %s at location %" PRId64 ", %" PRId64, thing_class_and_model_name(TCls_DeadCreature, tngmodel), (int64_t)(stl_x), (int64_t)(stl_y));
        return INVALID_THING;
    }
    
    // Try to move thing out of the solid wall if it's inside one
    if (thing_in_wall_at(thing, &thing->mappos))
    {
        if (!move_creature_to_nearest_valid_position(thing))
        {
            ERRORLOG("The %s was created in wall, removing", thing_model_name(thing));
            destroy_thing(thing);
            return INVALID_THING;
        }
    }
    return thing;
}

TbBool script_new_creature_type(const char *name)
{
    if (kfx_config_state.conf.crtr_conf.model_count >= CREATURE_TYPES_MAX)
    {
        SCRPTERRLOG("Cannot increase creature type count for creature type '%s', already at maximum %" PRId64 " types.", name, (int64_t)(CREATURE_TYPES_MAX));
        return false;
    }
    for (int64_t j = 0; j < (kfx_config_state.conf.crtr_conf.model_count - 1); j++)
    {
        if (strcmp(creature_desc[j].name, name) == 0)
        {
            SCRPTERRLOG("Trying to add creature type that already exists: %s", name);
            return false;
        }
    }
    int64_t i = kfx_config_state.conf.crtr_conf.model_count;
    kfx_config_state.conf.crtr_conf.model_count++;
    snprintf(kfx_config_state.conf.crtr_conf.model[i].name, COMMAND_WORD_LEN, "%s", name);
    creature_desc[i - 1].name = kfx_config_state.conf.crtr_conf.model[i].name;
    creature_desc[i - 1].num = i;
    
    if (load_default_creaturemodel_config(i, 0))
    {
        SCRPTLOG("Adding creature type %s and increasing creature types to %" PRId64, creature_code_name(i), (int64_t)(kfx_config_state.conf.crtr_conf.model_count - 1));
        return true;
    }
    else
    {
        SCRPTERRLOG("Failed to load config for creature '%s'(%" PRId64 ").", kfx_config_state.conf.crtr_conf.model[i].name, (int64_t)(i));
    }
    return false;
}

TbBool script_copy_creature_type(ThingModel source_id, const char* name)
{
    if (kfx_config_state.conf.crtr_conf.model_count >= CREATURE_TYPES_MAX)
    {
        SCRPTERRLOG("Cannot increase creature type count for creature type '%s', already at maximum %" PRId64 " types.", name, (int64_t)(CREATURE_TYPES_MAX));
        return false;
    }
    for (int64_t j = 0; j < (kfx_config_state.conf.crtr_conf.model_count - 1); j++)
    {
        if (strcmp(creature_desc[j].name, name) == 0)
        {
            SCRPTERRLOG("Trying to add creature type that already exists: %s", name);
            return false;
        }
    }
    int64_t i = kfx_config_state.conf.crtr_conf.model_count;
    kfx_config_state.conf.crtr_conf.model_count++;
    

//    init_creature_model_stats(i);
    kfx_config_state.conf.crtr_conf.model[i] = kfx_config_state.conf.crtr_conf.model[source_id];
    snprintf(kfx_config_state.conf.crtr_conf.model[i].name, COMMAND_WORD_LEN, "%s", name);
    creature_desc[i - 1].name = kfx_config_state.conf.crtr_conf.model[i].name;
    creature_desc[i - 1].num = i;
    for (int64_t k = 0; k < CREATURE_GRAPHICS_INSTANCES; k++)
    {
        kfx_config_state.conf.crtr_conf.creature_graphics[i][k] = kfx_config_state.conf.crtr_conf.creature_graphics[source_id][k];
    }
    kfx_config_state.conf.crtr_conf.creature_sounds[i] = kfx_config_state.conf.crtr_conf.creature_sounds[source_id];

    return true;
}

TbBool variable_is_settable(int64_t var_type)
{
    switch (var_type)
    {
    case SVar_FLAG:
    case SVar_CAMPAIGN_FLAG:
    case SVar_BOX_ACTIVATED:
    case SVar_TRAP_ACTIVATED:
    case SVar_SACRIFICED:
    case SVar_REWARDED:
    case SVar_MONEY:
    case SVar_HEART_HEALTH:
        return true;
    default:
        return false;
    }
}

void set_variable(int64_t player_idx, int64_t var_type, int64_t var_idx, int64_t new_val)
{
    struct Dungeon *dungeon = get_dungeon(player_idx);
    struct Coord3d pos = {0};

    switch (var_type)
    {
    case SVar_FLAG:
        set_script_flag(player_idx, var_idx, new_val);
        break;
    case SVar_CAMPAIGN_FLAG:
        intralvl.campaign_flags[player_idx][var_idx] = new_val;
        break;
    case SVar_BOX_ACTIVATED:
        dungeon->box_info.activated[var_idx] = saturate_set_unsigned(new_val, 16);
        break;
    case SVar_TRAP_ACTIVATED:
        dungeon->trap_info.activated[var_idx] = saturate_set_unsigned(new_val, 16);
        break;
    case SVar_SACRIFICED:
        dungeon->creature_sacrifice[var_idx] = saturate_set_unsigned(new_val, 8);
        if (find_temple_pool(player_idx, &pos))
        {
            process_sacrifice_creature(&pos, var_idx, player_idx, false);
        }
        break;
    case SVar_REWARDED:
        dungeon->creature_awarded[var_idx] = new_val;
        break;
    case SVar_MONEY:
    {
        // the difference as off-map gold, or taken from the treasury, as Lua's add_gold does
        if (dungeon_invalid(dungeon))
            break;
        const GoldAmount delta = new_val - dungeon->total_money_owned;
        if (delta > 0)
            player_add_offmap_gold(player_idx, delta);
        else if (delta < 0)
            take_money_from_dungeon(player_idx, -delta, 0);
        break;
    }
    case SVar_HEART_HEALTH:
    {
        // the value as written (a heart set below 0 is destroyed on its next turn)
        struct Thing *heart = get_player_soul_container(player_idx);
        if (thing_is_dungeon_heart(heart))
            heart->health = new_val;
        break;
    }
    default:
        WARNLOG("Unexpected type:%" PRId64,(int64_t)var_type);
    }
}

int64_t parse_criteria(const char *criteria)
{
    char c;
    int64_t arg;

    int64_t ret = get_id(creature_select_criteria_desc, criteria);
    if (ret == -1)
    {
        if (2 == sscanf(criteria, "AT_ACTION_POINT[%" SCNd64 "%c", &arg, &c) && (c == ']'))
        {
            ActionPointId loc = action_point_number_to_index(arg);
            if (loc == -1)
            {
                SCRPTERRLOG("Unknown action point at criteria, '%s'", criteria);
                return -1;
            }
            ret = (CSelCrit_NearAP) | (loc << 4);
        }
    }
    return ret;
}

#define get_players_range_single(plr_range_id) get_players_range_single_f(plr_range_id, __func__, text_line_number)
int64_t get_players_range_single_f(int64_t plr_range_id, const char *func_name, int64_t ln_num)
{
    if (plr_range_id < 0) {
        return -1;
    }
    if (plr_range_id == ALL_PLAYERS) {
        return -3;
    }
    if (plr_range_id < PLAYERS_COUNT)
    {
        return plr_range_id;
    }
    return -2;
}

int64_t get_chat_icon_sprite_idx_from_id(int64_t id, char type)
{
    switch (type)
    {
        case MsgType_Player:
            if (player_is_roaming(id))
                return GPS_plyrsym_symbol_player_red_std_b;

            if (id == kfx_config_state.neutral_player_num)
                return ((get_gameturn() >> 1) & 3)
                    + GPS_plyrsym_symbol_player_red_std_b;

            return player_has_heart(id)
                ? GPS_plyrsym_symbol_player_red_std_b
                : GPS_plyrsym_symbol_player_red_dead;

        case MsgType_Creature:
            return get_creature_model_graphics(id, CGI_HandSymbol);

        case MsgType_CreatureSpell:
            return get_spell_config(id)->medsym_sprite_idx;

        case MsgType_Room:
            return get_room_kind_stats(id)->medsym_sprite_idx;

        case MsgType_KeeperSpell:
            return get_power_model_stats(id)->medsym_sprite_idx;

        case MsgType_Query:
            return id + GPS_plyrsym_symbol_room_yellow_std_a;

        case MsgType_CreatureInstance:
            return creature_instance_info_get(id)->symbol_spridx;

        case MsgType_Custom:
            return id;

        case MsgType_Blank:
        default:
            return -1;
    }
}

void script_display_variable(PlayerNumber plyr_idx, unsigned char value_type, int64_t value_id, int64_t target,
    unsigned char target_type, TbBool include_icon, int64_t icon_idx)
{
    for (int64_t i = DISPLAY_VARIABLES_LIMIT - 1; i > 0; i--)
        kfx_game_state.script_variables[i] = kfx_game_state.script_variables[i-1];
    struct ScriptVariable *scvar = &kfx_game_state.script_variables[0];
    memset(scvar, 0, sizeof(*scvar));
    scvar->variable_player = plyr_idx;
    scvar->value_type = value_type;
    scvar->value_id = (unsigned char)value_id;
    scvar->variable_target = target;
    scvar->variable_target_type = target_type;
    scvar->include_icon = include_icon;
    scvar->icon_idx = icon_idx;
    scvar->is_active = true;
    if (kfx_game_state.active_script_var_count < DISPLAY_VARIABLES_LIMIT)
        kfx_game_state.active_script_var_count++;
    kfx_game_state.flags_gui |= GGUI_Variable;
}

void script_hide_variable(PlayerNumber plyr_idx, int64_t value_type, int64_t value_id)
{
    for (int64_t i = 0; i < kfx_game_state.active_script_var_count; i++)
    {
        const struct ScriptVariable *scvar = &kfx_game_state.script_variables[i];
        if ((plyr_idx != -1) && (scvar->variable_player != plyr_idx))
            continue;
        if ((value_type != -1) && ((scvar->value_type != value_type) || (scvar->value_id != value_id)))
            continue;
        for (int64_t j = i; j < kfx_game_state.active_script_var_count - 1; j++)
            kfx_game_state.script_variables[j] = kfx_game_state.script_variables[j+1];
        kfx_game_state.active_script_var_count--;
        memset(&kfx_game_state.script_variables[kfx_game_state.active_script_var_count], 0, sizeof(struct ScriptVariable));
        if (value_type != -1)
            break; // a named variable: its newest entry
        i--;
    }
    if (kfx_game_state.active_script_var_count == 0)
        kfx_game_state.flags_gui &= ~GGUI_Variable;
}

int64_t get_chat_icon_sprite_idx(const char* txt)
{
    int64_t id = 0;
    char type = 0;

    get_chat_icon_from_value(txt, &id, &type);

    return get_chat_icon_sprite_idx_from_id(id, type);
}


void get_chat_icon_from_value(const char* txt, int64_t* id, char* type)
{
    int64_t idx;
    if (strcasecmp(txt, "None") == 0)
    {
        *id = 0;
        *type = MsgType_Blank;
        return;
    }
    else if (strcasecmp(txt, "Kills") == 0)
    {
        *id = 1;
        *type = MsgType_Query;
        return;
    }
    else if (strcasecmp(txt, "Strength") == 0)
    {
        *id = 2;
        *type = MsgType_Query;
        return;
    }
    else if (strcasecmp(txt, "Gold") == 0)
    {
        *id = 3;
        *type = MsgType_Query;
        return;
    }
    else if (strcasecmp(txt, "Wage") == 0)
    {
        *id = 4;
        *type = MsgType_Query;
        return;
    }
    else if (strcasecmp(txt, "Armour") == 0)
    {
        *id = 5;
        *type = MsgType_Query;
        return;
    }
    else if (strcasecmp(txt, "Time") == 0)
    {
        *id = 6;
        *type = MsgType_Query;
        return;
    }
    else if (strcasecmp(txt, "Dexterity") == 0)
    {
        *id = 7;
        *type = MsgType_Query;
        return;
    }
    else if (strcasecmp(txt, "Defence") == 0)
    {
        *id = 8;
        *type = MsgType_Query;
        return;
    }
    else if (strcasecmp(txt, "Luck") == 0)
    {
        *id = 9;
        *type = MsgType_Query;
        return;
    }
    else if (strcasecmp(txt, "Blood") == 0)
    {
        *id = 10;
        *type = MsgType_Query;
        return;
    }
    else
    {
        idx = get_id(player_desc, txt);
    }
    if (idx == -1)
    {
        idx = get_id(cmpgn_human_player_options, txt);
        if (idx == -1)
        {
            idx = get_id(creature_desc, txt);
            if (idx != -1)
            {
                *id = idx;
                *type = MsgType_Creature;
            }
            else
            {
                idx = get_id(spell_desc, txt);
                if (idx != -1)
                {
                    *id = idx;
                    *type = MsgType_CreatureSpell;
                }
                else
                {
                    idx = get_id(room_desc, txt);
                    if (idx != -1)
                    {
                        *id = idx;
                        *type = MsgType_Room;
                    }
                    else
                    {
                        idx = get_id(power_desc, txt);
                        if (idx != -1)
                        {
                            *id = idx;
                            *type = MsgType_KeeperSpell;
                        }
                        else
                        {
                            idx = get_id(instance_desc, txt);
                            if (idx != -1)
                            {
                                *id = idx;
                                *type = MsgType_CreatureInstance;
                            }
                            else
                            {
                                *id = get_icon_id(txt);
                                *type = MsgType_Custom;
                            }
                        }
                    }
                }
            }
        }
    }
    else
    {
        *id = idx;
        *type = MsgType_Player;
    }
}

#define get_player_id(plrname, plr_range_id) get_player_id_f(plrname, plr_range_id, __func__, text_line_number)
TbBool get_player_id_f(const char *plrname, int64_t *plr_range_id, const char *func_name, int64_t ln_num)
{
    *plr_range_id = get_rid(player_desc, plrname);
    if (*plr_range_id == -1)
    {
      *plr_range_id = get_rid(cmpgn_human_player_options, plrname);
      if (*plr_range_id == -1)
      {
        ERRORMSG("%s(line %" PRIu64 "): Invalid player name, '%s'",func_name,(uint64_t)(ln_num), plrname);
        return false;
      }
    }
    return true;
}

/**
 * Returns hero objective, and also optionally checks the player name between brackets.
 * @param target gets filled with player number, or -1.
 * @return Hero Objective ID
 */
PlayerNumber get_objective_id_with_potential_target(const char* locname, PlayerNumber* target)
{
    char before_bracket[COMMAND_WORD_LEN];
    char player_string[COMMAND_WORD_LEN];
    const char* bracket = strchr(locname, '[');

    if (bracket == NULL) {
        strncpy(before_bracket, locname, sizeof(before_bracket) - 1);
        before_bracket[sizeof(before_bracket) - 1] = '\0';
        return get_rid(hero_objective_desc, before_bracket);
    }

    // Extract text before '['
    size_t len = min((size_t)(bracket - locname), sizeof(before_bracket) - 1);
    strncpy(before_bracket, locname, len);
    before_bracket[len] = '\0';

    // Extract text inside the brackets
    const char* start = bracket + 1;
    const char* end = strchr(start, ']');

    if (end != NULL) {
        size_t string_length = min((size_t)(end - start), sizeof(player_string) - 1);
        strncpy(player_string, start, string_length);
        player_string[string_length] = '\0';

        PlayerNumber plyr_idx = get_rid(player_desc, player_string);
        if (plyr_idx < 0)
            plyr_idx = get_rid(cmpgn_human_player_options, player_string);
        *target = plyr_idx;
    }
    return get_rid(hero_objective_desc, before_bracket);
}

#ifdef __cplusplus
}
#endif
