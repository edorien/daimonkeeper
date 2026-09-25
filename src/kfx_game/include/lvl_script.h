/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file lvl_script.h
 *     Header file for lvl_script.c.
 * @par Purpose:
 *     Level script commands support.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   KeeperFX Team
 * @date     12 Feb 2009 - 24 Feb 2009
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_LVLSCRIPT_H
#define DK_LVLSCRIPT_H

#include "globals.h"
#include "bflib_basics.h"

#include "config.h"
#include "config_rules.h"
#include "creature_groups.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
#define PARTY_TRIGGERS_COUNT     256
#define CREATURE_PARTYS_COUNT    256
#define CONDITIONS_COUNT         512
#define TUNNELLER_TRIGGERS_COUNT 256
#define SCRIPT_VALUES_COUNT      2048
#define WIN_CONDITIONS_COUNT      12
#define DISPLAY_VARIABLES_LIMIT   7

#define CONDITION_ALWAYS (CONDITIONS_COUNT)

#define SENSIBLE_GOLD 99999999

enum ScriptOperator {
    SOpr_SET = 1,
    SOpr_INCREASE,
    SOpr_DECREASE,
    SOpr_MULTIPLY,
};

enum {
    CurrentPlayer = 15
};

/******************************************************************************/
#pragma pack(1)

struct Condition;
struct ScriptLine;
struct ScriptValue;
struct PartyTrigger;

struct ScriptContext
{
    int64_t player_idx;

    union {
      struct ScriptValue *value;
      struct PartyTrigger *pr_trig;
    };
};

struct TunnellerTrigger {
  unsigned char flags;
  int64_t condit_idx;
  unsigned char plyr_idx;
  uint64_t location;
  uint64_t heading; // originally was 'target'
  int64_t carried_gold;
  CrtrExpLevel exp_level;
  char party_id;
};

struct PartyTrigger {
  unsigned char flags;
  int64_t condit_idx;
  char creatr_id;
  union
  {
      unsigned char plyr_idx;
      char party_id; // for add_to_party
  };
  union
  {
      TbMapLocation location;
      uint64_t countdown;
  };
  char spawn_type;
  CrtrExpLevel exp_level;
  int64_t carried_gold;
  union
  {
      int64_t ncopies;
      unsigned char objectv;
      PlayerNumber target;
  };
};

struct ScriptValue {
  unsigned char flags;
  int64_t condit_idx;
  unsigned char valtype;
  unsigned char plyr_range;
  union
  {
    struct
    {
        char action;
        char param;
        char victims[MAX_SACRIFICE_VICTIMS];
    } sac;
    unsigned char bytes[32];
    char chars[32];
    // The members below are a packed 32-byte overlay, NOT independent integers: fixed 16/32-bit widths on
    // purpose (an explicit exception to "all integers are 64-bit"). 32-bit on every platform. The layout of this union -- which byte ranges each command's fields
    // occupy (e.g. an index in longs[0] with a player in chars[4], a chat-icon type in chars[6]) -- was
    // written for the original 32-bit Windows build, where `long` is 4 bytes. With a plain `long`, 64-bit
    // Linux made longs[0] eight bytes wide and those neighbouring fields landed inside it: a QUICK_MESSAGE
    // with icon None (type 6) turned message index 2 into 2 | (6 << 48) and the game crashed in
    // message_add() (biervampir's faction boxes). Do not widen these.
    int16_t shorts[16];
    uint16_t ushorts[16];
    int32_t longs[8];
    long long longlongs[4];
    uint32_t ulongs[8];
    unsigned long long ulonglongs[4];
  };
};

struct Condition {
  int64_t condit_idx;
  unsigned char status;
  unsigned char plyr_range;
  unsigned char variabl_type;
  int64_t variabl_idx;
  unsigned char operation;
  uint64_t rvalue;
  unsigned char plyr_range_right;
  unsigned char variabl_type_right;
  int64_t variabl_idx_right;
  TbBool use_second_variable;
};


// struct ScriptFxLine moved to kfx_sim_state.h (stage 10,
// docs/refactor/stage-10-kfx-frontend.md) -- exclusively used by
// kfx_sim's thing_effects.c alongside the fx_lines[]/active_fx_lines
// fields it embeds.

struct LevelScript {
    struct TunnellerTrigger tunneller_triggers[TUNNELLER_TRIGGERS_COUNT];
    uint64_t tunneller_triggers_num;
    struct PartyTrigger party_triggers[PARTY_TRIGGERS_COUNT];
    uint64_t party_triggers_num;
    struct ScriptValue values[SCRIPT_VALUES_COUNT];
    uint64_t values_num;
    struct Condition conditions[CONDITIONS_COUNT];
    uint64_t conditions_num;
    struct Party creature_partys[CREATURE_PARTYS_COUNT];
    uint64_t creature_partys_num;
    int64_t win_conditions[WIN_CONDITIONS_COUNT];
    uint64_t win_conditions_num;
    int64_t lose_conditions[WIN_CONDITIONS_COUNT];
    uint64_t lose_conditions_num;

    // Store strings used at level here
    char strings[8192];
    int64_t next_string_offset;
};

struct ScriptVariable{
    unsigned char value_type;
    unsigned char value_id;
    PlayerNumber variable_player;
    int64_t variable_target;
    unsigned char variable_target_type;
    TbBool include_icon;
    int64_t icon_idx;
    TbBool is_active;
};

struct ScriptVariableDetails{
    int64_t value;
    const char* label;
    int64_t icon_idx;
    int64_t x_offset;
    int64_t y_offset;
};

/******************************************************************************/
extern unsigned char next_command_reusable;


#pragma pack()
/******************************************************************************/

// player_desc moved to kfx_config's config_players.h -- see its doc
// comment there.
/******************************************************************************/
int64_t clear_script(void);
int64_t load_script(int64_t lvl_num);
TbBool script_scan_line(char *line,TbBool preloaded, int64_t file_version);
TbBool preload_script(int64_t lvnum);
/** Numbers written in a script: LbStrToI32()/LbAtoI32() (bflib_basics.h), which clamp to the int32 range.
 *  The script language was defined on the 32-bit build, where strtol() saturates at +-2^31 (3000000000 reads
 *  as 2147483647); a 64-bit strtol would keep the value and the store into a 32-bit ScriptValue slot would
 *  wrap it (3000000000 -> -1294967296). Same script, same meaning on every platform. */
int64_t script_strtol(const char *text, char **endptr, int64_t base);
int64_t script_atol(const char *text);
/******************************************************************************/

struct ScriptVariableDetails get_condition_details(PlayerNumber plyr_idx, unsigned char valtype, int64_t validx);
int64_t get_condition_value(PlayerNumber plyr_idx, unsigned char valtype, int64_t validx);
const char* get_condition_label(PlayerNumber plyr_idx, unsigned char valtype, int64_t validx);
void process_level_script(void);
/******************************************************************************/

// Moved from kfx_sim's creature_groups.c/.h (stage 13.4, docs/refactor/
// stage-13-enforce-and-document.md) -- level-script party-definition CRUD,
// not simulation logic; every caller was already kfx_game/kfx_script.
TbBool create_party(const char *prtname);
int64_t get_party_index_of_name(const char *prtname);
TbBool add_member_to_party(int64_t party_id, int64_t crtr_model, CrtrExpLevel exp_level, int64_t carried_gold, int64_t objctv_id, int64_t countdown, PlayerNumber target);
TbBool delete_member_from_party(int64_t party_id, int64_t crtr_model, CrtrExpLevel exp_level);
struct Thing *script_process_new_tunneller_party(PlayerNumber plyr_idx, int64_t prty_id, TbMapLocation location, TbMapLocation heading, CrtrExpLevel exp_level, uint64_t carried_gold);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
