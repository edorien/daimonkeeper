/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file config_rules.h
 *     Header file for config_rules.c.
 * @par Purpose:
 *     Various game configuration options support.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     25 May 2009 - 31 Jul 2009
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_CFGRULES_H
#define DK_CFGRULES_H

#include "globals.h"
#include "bflib_basics.h"

#include "config.h"

#define MAX_SACRIFICE_VICTIMS 6
#define MAX_SACRIFICE_RECIPES 100

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
enum SacrificeAction {
    SacA_None = 0,
    SacA_MkCreature,
    SacA_MkGoodHero,
    SacA_NegSpellAll,
    SacA_PosSpellAll,
    SacA_NegUniqFunc,
    SacA_PosUniqFunc,
    SacA_CustomReward,
    SacA_CustomPunish,
};

enum UniqueFunctions {
    UnqF_None = 0,
    UnqF_MkAllAngry,
    UnqF_MkAllVerAngry,
    UnqF_ComplResrch,
    UnqF_ComplManufc,
    UnqF_KillChickns,
    UnqF_CheaperImp,
    UnqF_CostlierImp,
    UnqF_MkAllHappy,
};

enum SacrificeReturn {
    SacR_AngryWarn    = -1,
    SacR_DontCare     =  0,
    SacR_Pleased      =  1,
    SacR_Awarded      =  2,
    SacR_Punished     =  3,
};

struct SacrificeRecipe {
    ThingModel victims[MAX_SACRIFICE_VICTIMS];
    int64_t action;
    int64_t param;
};

struct GameRulesConfig {
    GoldAmount pot_of_gold_holds;
    GoldAmount chest_gold_hold;
    GoldAmount gold_pile_value;
    GoldAmount gold_pile_maximum;
    GoldAmount gold_per_hoard;
    GoldAmount bag_gold_hold;
    int64_t food_life_out_of_hatchery;
    HitPoints boulder_reduce_health_wall;
    HitPoints boulder_reduce_health_slap;
    HitPoints boulder_reduce_health_room;
    GameTurnDelta pay_day_gap;
    uint64_t dungeon_heart_heal_time;
    HitPoints dungeon_heart_heal_health;
    uint64_t hero_door_wait_time;
    uint64_t classic_bugs_flags;
    int64_t door_sale_percent;
    int64_t room_sale_percent;
    int64_t trap_sale_percent;
    uint64_t pay_day_speed;
    TbBool allies_share_vision;
    TbBool allies_share_drop;
    TbBool allies_share_cta;
    TbBool allies_share_disease;
    TbBool winner_tortures_loser;
    TbBool display_portal_limit;
    unsigned char max_things_in_hand;
    int64_t torture_payday;
    int64_t torture_training_cost;
    int64_t torture_scavenging_cost;
    uint64_t easter_egg_speech_chance;
    uint64_t easter_egg_speech_interval;
    int64_t global_ambient_light;
    int64_t thing_minimum_illumination;
    /* Colours (0..255 per channel; 0 0 0 = white) of the engine's built-in lights, used by the Vulkan
     * renderer's per-pixel lighting only: the light carried by a possessed creature, by hero creatures, and by the player's cursor. */
    int64_t possession_light_r, possession_light_g, possession_light_b;
    int64_t hero_light_r, hero_light_g, hero_light_b;
    int64_t cursor_light_r, cursor_light_g, cursor_light_b;
    TbBool light_enabled;
    int64_t creatures_count;
};

struct ComputerRulesConfig {
    int64_t disease_to_temple_pct;
};

struct CreatureRulesConfig {
    unsigned char recovery_frequency;
    int64_t body_remains_for;
    uint64_t flee_zone_radius;
    GameTurnDelta game_turns_in_flee;
    int64_t game_turns_unconscious;
    HitPoints critical_health_permil;
    unsigned char stun_enemy_chance_evil;
    unsigned char stun_enemy_chance_good;
    unsigned char stun_without_prison_chance;
    GameTurnDelta instance_delay_on_drop;
};

struct MagicRulesConfig {
    GameTurnDelta hold_audience_time;
    GameTurnDelta armageddon_teleport_your_time_gap;
    GameTurnDelta armageddon_teleport_enemy_time_gap;
    GameTurnDelta armageddon_count_down;
    GameTurnDelta armageddon_duration;
    unsigned char disease_transfer_percentage;
    unsigned char disease_lose_percentage_health;
    unsigned char disease_lose_health_time;
    MapSubtlDelta min_distance_for_teleport;
    int64_t collapse_dungeon_damage;
    GameTurnDelta turns_per_collapse_dngn_dmg;
    int64_t friendly_fight_area_damage_percent;
    int64_t friendly_fight_area_range_percent;
    TbBool armageddon_teleport_neutrals;
    int64_t weight_calculate_push;
    TbBool allow_instant_charge_up;
};

struct RoomRulesConfig {
    GameTurnDelta scavenge_cost_frequency;
    uint64_t temple_scavenge_protection_turns;
    GameTurnDelta train_cost_frequency;
    unsigned char ghost_convert_chance;
    int64_t default_generate_speed;
    uint64_t default_max_crtrs_gen_entrance;
    GameTurnDelta food_generation_speed;
    unsigned char prison_skeleton_chance;
    unsigned char bodies_for_vampire;
    int64_t graveyard_convert_time;
    int64_t barrack_max_party_size;
    CrtrExpLevel training_room_max_level;
    TbBool scavenge_good_allowed;
    TbBool scavenge_neutral_allowed;
    uint64_t time_between_prison_break;
    unsigned char prison_break_chance;
    unsigned char torture_death_chance;
    unsigned char torture_convert_chance;
    uint64_t time_in_prison_without_break;
    int64_t train_efficiency;
    int64_t work_efficiency;
    int64_t scavenge_efficiency;
    int64_t research_efficiency;
};
struct WorkersRulesConfig {
    unsigned char hits_per_slab;
    uint64_t default_imp_dig_damage;
    uint64_t default_imp_dig_own_damage;
    int64_t digger_work_experience;
    int64_t drag_to_lair;
};

struct HealthRulesConfig {
    HitPoints hunger_health_loss;
    int64_t turns_per_hunger_health_loss;
    HitPoints food_health_gain;
    HitPoints torture_health_loss;
    int64_t turns_per_torture_health_loss;
};

struct SacrificesRulesConfig {
    struct SacrificeRecipe sacrifice_recipes[MAX_SACRIFICE_RECIPES];
    /** The creature model used for determining amount of sacrifices which decrease digger cost. */
    ThingModel cheaper_diggers_sacrifice_model;
};
struct RulesConfig {
    struct GameRulesConfig gameplay;
    struct ComputerRulesConfig computer;
    struct CreatureRulesConfig creature;
    struct MagicRulesConfig magic;
    struct RoomRulesConfig rooms;
    struct WorkersRulesConfig workers;
    struct HealthRulesConfig health;
    struct SacrificesRulesConfig sacrifices;
};
/******************************************************************************/
extern const struct ConfigFileData keeper_rules_file_data;
extern const struct NamedCommand research_desc[];
extern const struct NamedCommand rules_game_classicbugs_commands[];
/******************************************************************************/
int64_t get_research_id(int64_t item_type, const char *trg_name, const char *func_name);
struct SacrificeRecipe *get_unused_sacrifice_recipe_slot(void);
// Moved from kfx_game's game_merge.h (stage 13.3, docs/refactor/
// stage-13-enforce-and-document.md) -- only ever reads
// kfx_config_state.conf.rules[0].gameplay.classic_bugs_flags, a
// misclassified function with no real kfx_game coupling.
TbBool emulate_integer_overflow(int64_t nbits);

const char *player_code_name(PlayerNumber plyr_idx);
int sac_compare_fn(const void* ptr_a, const void* ptr_b);
// Had real external linkage but no header declaration at all -- added,
// the usual "add the missing declaration" fix.
void clear_sacrifice_recipes(void);
TbBool add_sacrifice_victim(struct SacrificeRecipe *sac, ThingModel crtr_idx);

extern const struct NamedCommand rules_sacrifices_commands[];
extern const struct NamedCommand rules_research_commands[];
extern const struct NamedCommand sacrifice_unique_desc[];

extern const struct NamedField* ruleblocks[8];

extern const struct NamedFieldSet rules_named_fields_set;

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
