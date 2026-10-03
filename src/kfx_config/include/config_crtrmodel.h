/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file config_crtrmodel.h
 *     Header file for config_crtrmodel.c.
 * @par Purpose:
 *     Support of configuration files for specific creatures.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     25 May 2009 - 04 Jul 2009
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_CFGCRMODL_H
#define DK_CFGCRMODL_H

#include "globals.h"
#include "bflib_basics.h"

#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
TbBool load_default_creaturemodel_config(ThingModel crmodel, int64_t flags);
/** Parses one creature model file into model crtr_model (the loader calls it for the base file and each override). */
TbBool load_creaturemodel_config_file(int64_t crtr_model, const char *fname, int64_t flags);
TbBool swap_creature(ThingModel ncrt_id, ThingModel crtr_id);
TbBool make_all_creatures_free();
TbBool change_max_health_of_creature_kind(ThingModel crmodel, HitPoints new_max);
extern const struct NamedCommand creatmodel_properties_commands[];
struct CreatureModelConfig;
/** A creature property: what it switches, model flags, immunity flags or a TbBool field (an offset; -1: none). */
struct CreatureProperty {
    int64_t num;
    uint64_t model_flags;
    uint64_t immunity_flags;
    ptrdiff_t field;
};
/** A creature property (a creatmodel_properties_commands number), or NULL. */
const struct CreatureProperty *creature_property_get(int64_t property);
/** Switches a property: a flag is set for a value of 1 or more and cleared otherwise; a field takes the value.
 *  False if there's no such property. */
TbBool creature_property_set(struct CreatureModelConfig *crconf, int64_t property, int64_t val);
/** The creature model files' blocks as NamedField tables (refactor pass 3, S04). */
/** A creature model file's plain number (pass 3 finding F10; the CREATURE_STATS_WRAP classic bug keeps KeeperFX's atoi() rules). */
int64_t value_creature_number(const struct NamedField* named_field, const char* value_text, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags);
extern const struct NamedField creaturemodel_attributes_named_fields[];
extern const struct NamedField creaturemodel_attraction_named_fields[];
extern const struct NamedField creaturemodel_annoyance_named_fields[];
extern const struct NamedField creaturemodel_senses_named_fields[];
extern const struct NamedField creaturemodel_appearance_named_fields[];
extern const struct NamedField creaturemodel_experience_named_fields[];
extern const struct NamedField creaturemodel_jobs_named_fields[];
extern const struct NamedField creaturemodel_sprites_named_fields[];
extern const struct NamedField creaturemodel_sounds_named_fields[];
/** The set the model rows above assign into: one CreatureModelConfig per creature model index. */
extern const struct NamedFieldSet creaturemodel_named_fields_set;
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
