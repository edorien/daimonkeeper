/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file lvl_script_cmd_powers.c
 *     Level script commands: keeper powers and specials, spells on creatures.
 * @par Purpose:
 *     Their check and process functions; lvl_script_commands.c has the command tables that name them
 *     (refactor pass 5, S09: split out of it by command family).
 * @author   KeeperFX Team
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "creature_states_pray.h"
#include "lvl_script_conditions.h"
#include "lvl_script_lib.h"
#include "power_specials.h"
#include "magic_powers.h"
#include "player_availability.h"
#include "list_walk.h"
#include "lvl_script_commands_private.h"
#include "lvl_script_commands_private.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

void special_transfer_creature_process(struct ScriptContext* context)
{
    if (my_player_number == context->player_idx)
    {
        struct Thing *heartng = get_player_soul_container(context->player_idx);
        struct PlayerInfo* player = get_my_player();
        start_transfer_creature(player, heartng);
    }
}

void special_transfer_creature_check(const struct ScriptLine* scline)
{
    command_add_value(Cmd_USE_SPECIAL_TRANSFER_CREATURE, scline->np[0],0,0,0);
}

/** The USE_SPELL_ON_*CREATURE* checks' spell (mag_id, -1: unknown) and level: a spell of a keeper power has its level
 *  warned and clamped (creatures cast from level 1 to 10); then made 0-based. False (after the error) for an unknown
 *  spell. */
static TbBool spell_kind_and_level(const char *mag_name, int64_t mag_id, CrtrExpLevel *spell_level)
{
    if (mag_id == -1)
    {
        SCRPTERRLOG("Invalid spell: %s", mag_name);
        return false;
    }
    struct SpellConfig* spconf = get_spell_config(mag_id);
    if (spconf->linked_power) // Only check for spells linked to a keeper power.
    {
        if (*spell_level < 1)
        {
            SCRPTWRNLOG("Spell %s level too low: %" PRId64 ", setting to 1.", mag_name, (int64_t)(*spell_level));
            *spell_level = 1;
        }
        if (*spell_level > (MAGIC_OVERCHARGE_LEVELS + 1)) // Creatures cast spells from level 1 to 10.
        {
            SCRPTWRNLOG("Spell %s level too high: %" PRId64 ", setting to %" PRId64 ".", mag_name, (int64_t)(*spell_level), (int64_t)((MAGIC_OVERCHARGE_LEVELS + 1)));
            *spell_level = MAGIC_OVERCHARGE_LEVELS + 1; // as the warning says (P5-F36: was one less)
        }
    }
    (*spell_level)--;
    return true;
}

void use_spell_on_creature_check(const struct ScriptLine* scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, scline->np[0]);
    int64_t crtr_id = parse_creature_name(scline->tp[1]);
    if (crtr_id == CREATURE_NONE)
    {
        SCRPTERRLOG("Unknown creature, '%s'", scline->tp[1]);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    int64_t select_id = parse_criteria(scline->tp[2]);
    if (select_id == -1) {
        SCRPTERRLOG("Unknown select criteria, '%s'", scline->tp[2]);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    const char* mag_name = scline->tp[3];
    int64_t mag_id = get_rid(spell_desc, mag_name);
    CrtrExpLevel spell_level = scline->np[4];
    if (!spell_kind_and_level(mag_name, mag_id, &spell_level))
    {
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    value->shorts[1] = crtr_id;
    value->shorts[2] = select_id;
    value->shorts[3] = mag_id;
    value->shorts[4] = spell_level;
    PROCESS_SCRIPT_VALUE(scline->command);
}
void use_spell_on_creature_process(struct ScriptContext* context)
{
    ThingModel crmodel = context->value->shorts[1];
    int64_t select_id = context->value->shorts[2];
    SpellKind spell_idx = context->value->shorts[3];
    CrtrExpLevel overchrg = context->value->shorts[4];
    script_use_spell_on_creature_with_criteria(context->player_idx, crmodel, select_id, spell_idx, overchrg);
}

void use_spell_on_players_creatures_check(const struct ScriptLine *scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, scline->np[0]);
    int64_t crtr_id = parse_creature_name(scline->tp[1]);
    if (crtr_id == CREATURE_NONE)
    {
        SCRPTERRLOG("Unknown creature, '%s'", scline->tp[1]);
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    const char *mag_name = scline->tp[2];
    int64_t mag_id = get_rid(spell_desc, mag_name);
    CrtrExpLevel spell_level = scline->np[3];
    if (!spell_kind_and_level(mag_name, mag_id, &spell_level))
    {
        DEALLOCATE_SCRIPT_VALUE
        return;
    }
    value->shorts[1] = crtr_id;
    value->shorts[2] = mag_id;
    value->shorts[3] = spell_level;
    PROCESS_SCRIPT_VALUE(scline->command);
}

void use_spell_on_players_creatures_process(struct ScriptContext *context)
{
    int64_t crmodel = context->value->shorts[1];
    int64_t spell_idx = context->value->shorts[2];
    CrtrExpLevel overchrg = context->value->shorts[3];
    apply_spell_effect_to_players_creatures(context->player_idx, crmodel, spell_idx, overchrg);
}

void use_power_on_players_creatures_check(const struct ScriptLine* scline)
{
    ALLOCATE_SCRIPT_VALUE(scline->command, scline->np[0]);
    int64_t crtr_id = parse_creature_name(scline->tp[1]);
    PlayerNumber caster_player = scline->np[2];
    const char* pwr_name = scline->tp[3];
    int64_t pwr_id = get_rid(power_desc, pwr_name);
    KeepPwrLevel power_level = scline->np[4];
    int64_t free = scline->np[5];
    if (free == -1)
    {
        free = get_id(is_free_desc, scline->tp[5]);
        if (free == -1)
        {
            SCRPTERRLOG("Unknown free value '%s' not recognized", scline->tp[5]);
            DEALLOCATE_SCRIPT_VALUE
            return;
        }
    }

    if (crtr_id == CREATURE_NONE)
    {
        SCRPTERRLOG("Unknown creature, '%s'", scline->tp[1]);
        DEALLOCATE_SCRIPT_VALUE
    }
    if (pwr_id == -1)
    {
        SCRPTERRLOG("Invalid power: %s", pwr_name);
        DEALLOCATE_SCRIPT_VALUE
    }
    switch (pwr_id)
    {
    case PwrK_HEALCRTR:
    case PwrK_SPEEDCRTR:
    case PwrK_PROTECT:
    case PwrK_REBOUND:
    case PwrK_CONCEAL:
    case PwrK_DISEASE:
    case PwrK_CHICKEN:
    case PwrK_FREEZE:
    case PwrK_SLOW:
    case PwrK_FLIGHT:
    case PwrK_VISION:
    case PwrK_CALL2ARMS:
    case PwrK_LIGHTNING:
    case PwrK_CAVEIN:
    case PwrK_SIGHT:
    case PwrK_TIMEBOMB:
        if ((power_level < 1) || (power_level > MAGIC_OVERCHARGE_LEVELS))
        {
            SCRPTERRLOG("Power %s level %" PRId64 " out of range. Acceptible values are %" PRId64 "~%" PRId64, pwr_name, (int64_t)(power_level), (int64_t)(1), (int64_t)(MAGIC_OVERCHARGE_LEVELS));
            DEALLOCATE_SCRIPT_VALUE
        }
        power_level--; // transform human 1~9 range into computer 0~8 range
        break;
    case PwrK_SLAP:
    case PwrK_MKDIGGER:
        break;
    default:
        SCRPTERRLOG("Power not supported for this command: %s", power_code_name(pwr_id));
        DEALLOCATE_SCRIPT_VALUE
    }
    value->shorts[1] = crtr_id;
    value->shorts[2] = pwr_id;
    value->shorts[3] = power_level;
    value->shorts[4] = caster_player;
    value->shorts[5] = free;
    PROCESS_SCRIPT_VALUE(scline->command);
}

/**
 * Casts a keeper power on all creatures of a specific model, or positions of all creatures depending on the power.
 * @param crmodel The creature model to target, accepts wildcards.
 * @param pwr_idx The ID of the Keeper Power.
 * @param overchrg The overcharge level of the keeperpower. Is ignored when not applicable.
 * @param caster The player number of the player who is made to cast the spell.
 * @param free If gold is used when casting the spell. It will fail to cast if it is not free and money is not available.
 */
void cast_power_on_players_creatures(PlayerNumber plyr_idx, ThingModel crmodel, int64_t pwr_idx, KeepPwrLevel overchrg, PlayerNumber caster, TbBool free)
{
    SYNCDBG(8, "Starting");
    struct Dungeon* dungeon = get_players_num_dungeon(plyr_idx);
    FOR_EACH_THING(thing, thing_walk_players_creatures_of_model(dungeon, plyr_idx, crmodel))
    {
        if (creature_matches_model(thing, crmodel))
        {
            script_use_power_on_creature(thing, pwr_idx, overchrg, caster, free);
        }
    }
    SYNCDBG(19, "Finished");
}

void use_power_on_players_creatures_process(struct ScriptContext* context)
{
    int64_t crmodel = context->value->shorts[1];
    int64_t pwr_idx = context->value->shorts[2];
    KeepPwrLevel overchrg = context->value->shorts[3];
    PlayerNumber caster = context->value->shorts[4];
    TbBool free = context->value->shorts[5];
    cast_power_on_players_creatures(context->player_idx, crmodel, pwr_idx, overchrg, caster, free);
}

void magic_available_check(const struct ScriptLine *scline)
{
    int64_t mag_id = get_rid(power_desc, scline->tp[1]);
    if (mag_id == -1)
    {
      SCRPTERRLOG("Unknown magic, '%s'", scline->tp[1]);
      return;
    }
    command_add_value(scline->command, scline->np[0], mag_id, scline->np[2], scline->np[3]);
}

void magic_available_process(struct ScriptContext *context)
{
    const struct ScriptValue *value = context->value;
    if (!set_power_available(context->player_idx, value->longs[0], value->longs[1], value->longs[2])) {
        WARNLOG("Setting power %s availability for player %" PRId64 " failed.",power_code_name(value->longs[0]),(int64_t)context->player_idx);
    }
}

/** The USE_POWER* commands' power level (warned and clamped to 1..MAGIC_OVERCHARGE_LEVELS, then made 0-based) and
 *  power kind; false (after the error) for an unknown power. */
static TbBool use_power_level_and_kind(const char *magname, KeepPwrLevel *power_level, int64_t *mag_id)
{
  if (*power_level < 1)
  {
    SCRPTWRNLOG("Spell %s level too low: %" PRId64 ", setting to 1.", magname, (int64_t)(*power_level));
    *power_level = 1;
  }
  if (*power_level > MAGIC_OVERCHARGE_LEVELS)
  {
    SCRPTWRNLOG("Spell %s level too high: %" PRId64 ", setting to %" PRId64 ".", magname, (int64_t)(*power_level), (int64_t)(MAGIC_OVERCHARGE_LEVELS));
    *power_level = MAGIC_OVERCHARGE_LEVELS;
  }
  (*power_level)--;
  *mag_id = get_rid(power_desc, magname);
  if (*mag_id == -1)
  {
    SCRPTERRLOG("Unknown magic, '%s'", magname);
    return false;
  }
  return true;
}

/** The USE_POWER* commands' "free" argument (0, 1 or a name of is_free_desc); false (after the error) otherwise. */
static TbBool use_power_free(const char *freestring, char *free)
{
    if (parameter_is_number(freestring))
    {
        *free = atoi(freestring);
    }
    else
    {
        *free = get_id(is_free_desc, freestring);
    }
    if ((*free < 0) || (*free > 1))
    {
        SCRPTERRLOG("Unknown free value '%s' not recognized", freestring);
        return false;
    }
    return true;
}

void use_power_on_creature_check(const struct ScriptLine *scline)
{
    int64_t plr_range_id = scline->np[0];
    const char *crtr_name = scline->tp[1];
    const char *criteria = scline->tp[2];
    int64_t caster_plyr_idx = scline->np[3];
    const char *magname = scline->tp[4];
    KeepPwrLevel power_level = scline->np[5];
    const char *freestring = scline->tp[6];
  SCRIPTDBG(11, "Starting");
  int64_t mag_id;
  if (!use_power_level_and_kind(magname, &power_level, &mag_id))
    return;
  ThingModel crtr_id = parse_creature_name(crtr_name);
  if (crtr_id == CREATURE_NONE) {
    SCRPTERRLOG("Unknown creature, '%s'", crtr_name);
    return;
  }
  int64_t select_id = parse_criteria(criteria);
  if (select_id == -1) {
    SCRPTERRLOG("Unknown select criteria, '%s'", criteria);
    return;
  }
  char free;
  if (!use_power_free(freestring, &free))
      return;

  // encode params: free, magic, caster, level -> into 4xbyte: FMCL
  int64_t fmcl_bytes;
  {
      signed char f = free, m = mag_id, c = caster_plyr_idx, lvl = power_level;
      fmcl_bytes = (f << 24) | (m << 16) | (c << 8) | lvl;
  }
  command_add_value(scline->command, plr_range_id, crtr_id, select_id, fmcl_bytes);
}

void use_power_at_pos_check(const struct ScriptLine *scline)
{
    int64_t plr_range_id = scline->np[0];
    int64_t stl_x = scline->np[1];
    int64_t stl_y = scline->np[2];
    const char *magname = scline->tp[3];
    KeepPwrLevel power_level = scline->np[4];
    const char *freestring = scline->tp[5];
  SCRIPTDBG(11, "Starting");
  int64_t mag_id;
  if (!use_power_level_and_kind(magname, &power_level, &mag_id))
    return;
  char free;
  if (!use_power_free(freestring, &free))
      return;

  // encode params: free, magic, level -> into 3xbyte: FML
  int64_t fml_bytes;
  {
      signed char f = free, m = mag_id, lvl = power_level;
      fml_bytes = (f << 16) | (m << 8) | lvl;
  }
  command_add_value(scline->command, plr_range_id, stl_x, stl_y, fml_bytes);
}

void use_power_at_location_check(const struct ScriptLine *scline)
{
    int64_t plr_range_id = scline->np[0];
    const char *locname = scline->tp[1];
    const char *magname = scline->tp[2];
    KeepPwrLevel power_level = scline->np[3];
    const char *freestring = scline->tp[4];
  SCRIPTDBG(11, "Starting");
  int64_t mag_id;
  if (!use_power_level_and_kind(magname, &power_level, &mag_id))
    return;

  TbMapLocation location;
  if (!get_map_location_id(locname, &location))
  {
    SCRPTWRNLOG("Use power script command at invalid location: %s", locname);
    return;
  }
  char free;
  if (!use_power_free(freestring, &free))
      return;

  // encode params: free, magic, level -> into 3xbyte: FML
  int64_t fml_bytes;
  {
      signed char f = free, m = mag_id, lvl = power_level;
      fml_bytes = (f << 16) | (m << 8) | lvl;
  }
  command_add_value(scline->command, plr_range_id, location, fml_bytes, 0);
}

void use_power_check(const struct ScriptLine *scline)
{
    int64_t plr_range_id = scline->np[0];
    const char *magname = scline->tp[1];
    const char *freestring = scline->tp[2];
    SCRIPTDBG(11, "Starting");
    int64_t mag_id = get_rid(power_desc, magname);
    if (mag_id == -1)
    {
        SCRPTERRLOG("Unknown magic, '%s'", magname);
        return;
    }
    char free;
    if (!use_power_free(freestring, &free))
        return;
    command_add_value(scline->command, plr_range_id, mag_id, free, 0);
}

void use_special_increase_level_check(const struct ScriptLine *scline)
{
    int64_t plr_range_id = scline->np[0];
    int64_t count = scline->np[1];
    if (count == 0)
    {
        SCRPTWRNLOG("Invalid count: %" PRId64 ", setting to 1.", (int64_t)(count));
        count = 1;
    }

    if (count > 9)
    {
        SCRPTWRNLOG("Count too high: %" PRId64 ", setting to 9.", (int64_t)(count));
        count = 9;
    }

    if (count < -9)
    {
        SCRPTWRNLOG("Count too low: %" PRId64 ", setting to -9.", (int64_t)(count));
        count = -9;
    }
    command_add_value(scline->command, plr_range_id, count, 0, 0);
}

void use_special_multiply_creatures_check(const struct ScriptLine *scline)
{
    int64_t plr_range_id = scline->np[0];
    int64_t count = scline->np[1];
    if (count < 1)
    {
        SCRPTWRNLOG("Invalid count: %" PRId64 ", setting to 1.", (int64_t)(count));
        count = 1;
    }

    if (count > 9)
    {
        SCRPTWRNLOG("Count too high: %" PRId64 ", setting to 9.", (int64_t)(count));
        count = 9;
    }
    command_add_value(scline->command, plr_range_id, count, 0, 0);
}

void make_safe_check(const struct ScriptLine *scline)
{
    int64_t plr_range_id = scline->np[0];
    command_add_value(scline->command, plr_range_id, 0, 0, 0);
}

void locate_hidden_world_check(const struct ScriptLine *scline)
{
    command_add_value(scline->command, 0, 0, 0, 0);
}

void use_power_on_creature_process(struct ScriptContext *context)
{
    const struct ScriptValue *value = context->value;
    script_use_power_on_creature_matching_criterion(context->player_idx, value->longs[0], value->longs[1], value->longs[2]);
}

void use_power_at_pos_process(struct ScriptContext *context)
{
    const struct ScriptValue *value = context->value;
    script_use_power_at_pos(context->player_idx, value->longs[0], value->longs[1], value->longs[2]);
}

void use_power_at_location_process(struct ScriptContext *context)
{
    const struct ScriptValue *value = context->value;
    script_use_power_at_location(context->player_idx, value->longs[0], value->longs[1]);
}

void use_power_process(struct ScriptContext *context)
{
    const struct ScriptValue *value = context->value;
    script_use_power(context->player_idx, value->longs[0], value->longs[1]);
}

void use_special_increase_level_process(struct ScriptContext *context)
{
    script_use_special_increase_level(context->player_idx, context->value->longs[0]);
}

void use_special_multiply_creatures_process(struct ScriptContext *context)
{
    for (int64_t count = 0; count < context->value->longs[0]; count++)
    {
      script_use_special_multiply_creatures(context->player_idx);
    }
}

void make_safe_process(struct ScriptContext *context)
{
    script_make_safe(context->player_idx);
}

void locate_hidden_world_process(struct ScriptContext *context)
{
    if (!legacy_value_first_player(context))
        return;
    script_locate_hidden_world();
}

void make_unsafe_check(const struct ScriptLine *scline)
{
    int64_t plr_range_id = scline->np[0];
    command_add_value(scline->command, plr_range_id, 0, 0, 0);
}

void make_unsafe_process(struct ScriptContext *context)
{
    script_make_unsafe(context->player_idx);
}

/******************************************************************************/
#ifdef __cplusplus
}
#endif
