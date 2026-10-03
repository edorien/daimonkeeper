/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file cfgc_schema_creature.cpp
 *     The curated schema of the creature files (plan 04 §4, plan 06 §4): the per-creature model files
 *     (`creatrs/<name>.cfg`, kind "creaturemodel") and `creature.cfg` (kind "creature"). Their loaders are
 *     hand-written, so there is no field table to reflect; the keys come from the exported key tables and
 *     each key's value shape from this table, checked against every shipped file by the schema coverage test.
 * @par Comment:
 *     Shape language of a key, one token per value: `N` number, `S` text, `E:reg` a name from a registry,
 *     `E:reg|NULL` the same with literal extras, `F:reg` / `F:table` a list of names (flags), `T:table` one
 *     name from a static table, `X*n` repeat a token n times, and a trailing `...` lets the last token repeat.
 */
#include "pre_inc.h"
#include "cfgc_schema.h"
#include "cfgc_schema_shapes.h"

#include <algorithm>
#include <cctype>
#include <cinttypes>
#include <cstdarg>
#include <cstring>
#include <map>
#include <sstream>

extern "C" {
#include "config.h"
#include "config_creature.h"
#include "config_crtrmodel.h"
extern const struct NamedCommand creaturetype_common_commands[];
extern const struct NamedCommand creaturetype_experience_commands[];
extern const struct NamedCommand creaturetype_instance_commands[];
extern const struct NamedCommand creaturetype_instance_properties[];
extern const struct NamedCommand creaturetype_job_commands[];
extern const struct NamedCommand creaturetype_job_assign[];
extern const struct NamedCommand creaturetype_job_properties[];
extern const struct NamedCommand creaturetype_angerjob_commands[];
extern const struct NamedCommand creaturetype_attackpref_commands[];
extern const struct NamedCommand instance_range_desc[];
extern const struct NamedCommand spell_effect_flags[];
}
#include "post_inc.h"

/******************************************************************************/
namespace {

std::vector<std::string> table_names(const std::string &name);

const struct NamedCommand *table_by_name(const std::string &name)
{
    if (name == "creature_props") return creatmodel_properties_commands;
    if (name == "deathkind") return creature_deathkind_desc;
    if (name == "inst_props") return creaturetype_instance_properties;
    if (name == "job_assign") return creaturetype_job_assign;
    if (name == "job_props") return creaturetype_job_properties;
    if (name == "instance_range") return instance_range_desc;
    if (name == "graphics") return creature_graphics_desc;
    if (name == "spell_effect") return spell_effect_flags;
    return nullptr;
}

std::vector<std::string> table_names(const std::string &name)
{
    std::vector<std::string> out;
    for (const struct NamedCommand *t = table_by_name(name); t != nullptr && t->name != nullptr; t++)
        out.push_back(t->name);
    return out;
}

struct Shape
{
    const char *section;
    const char *key;
    const char *shape;
};

const Shape kModelShapes[] = {
    {"attributes", "Armour", "N"}, {"attributes", "AttackPreference", "E:attackpref"}, {"attributes", "BaseSpeed", "N"},
    {"attributes", "DamageToBoulder", "N"}, {"attributes", "Defence", "N"}, {"attributes", "Dexterity", "N"},
    {"attributes", "FearsomeFactor", "N"}, {"attributes", "FearStronger", "N"}, {"attributes", "FearWounded", "N"},
    {"attributes", "GoldHold", "N"}, {"attributes", "HealRequirement", "N"}, {"attributes", "Health", "N"},
    {"attributes", "HealThreshold", "N"}, {"attributes", "HostileTowards", "E:creature|NULL ..."},
    {"attributes", "HungerFill", "N"}, {"attributes", "HungerRate", "N"}, {"attributes", "HurtByLava", "N"},
    {"attributes", "LairObject", "E:object|NULL"}, {"attributes", "LairSize", "N"}, {"attributes", "Luck", "N"},
    {"attributes", "NameTextID", "N"}, {"attributes", "Pay", "N"}, {"attributes", "PrisonKind", "E:creature|NULL"},
    {"attributes", "Properties", "F:creature_props"}, {"attributes", "Recovery", "N"}, {"attributes", "Size", "N N"},
    {"attributes", "SlapsToKill", "N"}, {"attributes", "SpellImmunity", "F:spell_effect"}, {"attributes", "Strength", "N"},
    {"attributes", "ThingSize", "N N"}, {"attributes", "TokingRecovery", "N"}, {"attributes", "TortureKind", "E:creature|NULL"},

    {"attraction", "BaseEntranceScore", "N"}, {"attraction", "EntranceRoom", "E:room|NULL E:room|NULL E:room|NULL"},
    {"attraction", "RoomSlabsRequired", "N N N"}, {"attraction", "ScavengeRequirement", "N"}, {"attraction", "TortureTime", "N"},

    {"annoyance", "AngerJobs", "F:angerjob"}, {"annoyance", "AnnoyLevel", "N"}, {"annoyance", "EatFood", "N"},
    {"annoyance", "GoingPostal", "N"}, {"annoyance", "GotWage", "N"}, {"annoyance", "InHand", "N"}, {"annoyance", "InTemple", "N"},
    {"annoyance", "InTorture", "N"}, {"annoyance", "JobStress", "N"}, {"annoyance", "LairEnemy", "F:creature|NULL"},
    {"annoyance", "NoHatchery", "N"}, {"annoyance", "NoLair", "N"}, {"annoyance", "NoSalary", "N"},
    {"annoyance", "OthersLeaving", "N"}, {"annoyance", "Queue", "N"}, {"annoyance", "Slapped", "N"}, {"annoyance", "Sleeping", "N"},
    {"annoyance", "StandingOnDeadEnemy", "N"}, {"annoyance", "StandingOnDeadFriend", "N"}, {"annoyance", "Sulking", "N"},
    {"annoyance", "Untrained", "N N"}, {"annoyance", "WillNotDoJob", "N"}, {"annoyance", "WinBattle", "N"}, {"annoyance", "WokenUp", "N"},

    {"experience", "ExperienceForHitting", "N"}, {"experience", "GrowUp", "N E:creature|NULL N"},
    {"experience", "LevelsTrainValues", "N*9"}, {"experience", "Powers", "E:instance|NULL*10"},
    {"experience", "PowersLevelRequired", "N*10"}, {"experience", "Rebirth", "N"}, {"experience", "SleepExperience", "text"},

    {"jobs", "ManufactureValue", "N"}, {"jobs", "NotDoJobs", "F:creaturejob"}, {"jobs", "PartnerTraining", "N"},
    {"jobs", "PrimaryJobs", "F:creaturejob"}, {"jobs", "ResearchValue", "N"}, {"jobs", "ScavengerCost", "N"},
    {"jobs", "ScavengeValue", "N"}, {"jobs", "SecondaryJobs", "F:creaturejob"}, {"jobs", "StressfulJobs", "F:creaturejob"},
    {"jobs", "TrainingCost", "N"}, {"jobs", "TrainingValue", "N"},

    {"senses", "EyeEffect", "S"}, {"senses", "EyeHeight", "N"}, {"senses", "FieldOfView", "N"}, {"senses", "Hearing", "N"},
    {"senses", "MaxAngleChange", "N"},

    {"appearance", "CorpseVanishEffect", "N"}, {"appearance", "FixedAnimSpeed", "N"}, {"appearance", "FootstepPitch", "N"},
    {"appearance", "NaturalDeathKind", "T:deathkind"}, {"appearance", "PickupOffset", "N N"}, {"appearance", "PossessSwipeIndex", "N"},
    {"appearance", "ShotOrigin", "N N N"}, {"appearance", "StatusOffset", "text"}, {"appearance", "TransparencyFlags", "N"},
    {"appearance", "VisualRange", "N"}, {"appearance", "WalkingAnimSpeed", "N"},

    {"sounds", "Die", "N N"}, {"sounds", "Drop", "N N"}, {"sounds", "Fight", "N N"}, {"sounds", "Foot", "N N"},
    {"sounds", "Hang", "N N"}, {"sounds", "Happy", "N N"}, {"sounds", "Hit", "N N"}, {"sounds", "Piss", "N N"},
    {"sounds", "Sad", "N N"}, {"sounds", "Slap", "N N"}, {"sounds", "Torture", "N N"},
};

// magic.cfg keys with a known grammar (the rest of the spell and special keys are still inferred).
const Shape kMagicShapes[] = {
    {"spell", "SpellFlags", "F:spell_effect"},
    {"spell", "CleanseFlags", "F:spell_effect"},
};

const Shape kCreatureShapes[] = {
    {"common", "Creatures", "text"}, {"common", "JobsCount", "N"}, {"common", "AngerJobsCount", "N"},
    {"common", "AttackPreferencesCount", "N"}, {"common", "SpriteSize", "N"},

    {"experience", "PayIncreaseOnExp", "N"}, {"experience", "SpellDamageIncreaseOnExp", "N"}, {"experience", "RangeIncreaseOnExp", "N"},
    {"experience", "JobValueIncreaseOnExp", "N"}, {"experience", "HealthIncreaseOnExp", "N"}, {"experience", "StrengthIncreaseOnExp", "N"},
    {"experience", "DexterityIncreaseOnExp", "N"}, {"experience", "DefenseIncreaseOnExp", "N"}, {"experience", "LoyaltyIncreaseOnExp", "N"},
    {"experience", "ArmourIncreaseOnExp", "N"}, {"experience", "SizeIncreaseOnExp", "N"}, {"experience", "ExpForHittingIncreaseOnExp", "N"},
    {"experience", "TrainingCostIncreaseOnExp", "N"}, {"experience", "ScavengingCostIncreaseOnExp", "N"},

    {"instance", "Name", "S"}, {"instance", "Time", "N"}, {"instance", "ActionTime", "N"}, {"instance", "ResetTime", "N"},
    {"instance", "FPTime", "N"}, {"instance", "FPActionTime", "N"}, {"instance", "FPResetTime", "N"}, {"instance", "ForceVisibility", "N"},
    {"instance", "TooltipTextID", "N"}, {"instance", "SymbolSprites", "S"}, {"instance", "Graphics", "T:graphics"},
    {"instance", "Function", "text"}, {"instance", "RangeMin", "R:instance_range"}, {"instance", "RangeMax", "R:instance_range"},
    {"instance", "Properties", "F:inst_props"}, {"instance", "FPInstantCast", "N"}, {"instance", "PrimaryTarget", "N"},
    {"instance", "ValidateSourceFunc", "text"}, {"instance", "ValidateTargetFunc", "text"}, {"instance", "SearchTargetsFunc", "text"},
    {"instance", "PostalPriority", "N"}, {"instance", "NoAnimationLoop", "N"}, {"instance", "FPAllowSelfCastWhileFrozen", "N"},
    {"instance", "FPAllowSelfCastWhenChicken", "N"},

    {"job", "Name", "S"}, {"job", "RelatedRoomRole", "S"}, {"job", "RelatedEvent", "S"}, {"job", "Assign", "F:job_assign"},
    {"job", "InitialState", "S"}, {"job", "ContinueState", "S"}, {"job", "PlayerFunctions", "text"}, {"job", "CoordsFunctions", "text"},
    {"job", "Properties", "F:job_props"},

    {"angerjob", "Name", "S"}, {"attackpref", "Name", "S"},
};

void add_keys(CfgFileSchema &file, const char *section, bool numbered, const struct NamedCommand *table)
{
    file.sections.push_back(CfgSectionSpec());
    CfgSectionSpec &s = file.sections.back();
    s.basename = section;
    s.numbered = numbered;
    for (const struct NamedCommand *c = table; c != nullptr && c->name != nullptr; c++)
    {
        CfgFieldSpec f;
        f.key = c->name;
        CfgValueSpec p;
        p.kind = CfgKind_Unspecified;
        f.parts.push_back(p);
        s.fields.push_back(std::move(f));
    }
}

void apply_shapes(CfgFileSchema &file, const Shape *shapes, size_t count)
{
    for (size_t i = 0; i < count; i++)
    {
        CfgSectionSpec *sec = file.find_section(shapes[i].section);
        CfgFieldSpec *f = sec != nullptr ? sec->find_field(shapes[i].key) : nullptr;
        if (f == nullptr && sec != nullptr)
        {
            sec->fields.push_back(CfgFieldSpec());
            f = &sec->fields.back();
            f->key = shapes[i].key;
        }
        if (f != nullptr)
            cfgc_apply_shape(*f, shapes[i].shape, table_names);
    }
}

} // namespace

void describe_creature_schemas(ConfigSchema &schema)
{
    if (CfgFileSchema *model = schema.find("creaturemodel"))
        apply_shapes(*model, kModelShapes, sizeof(kModelShapes) / sizeof(kModelShapes[0]));

    if (CfgFileSchema *magic = schema.find("magic"))
        apply_shapes(*magic, kMagicShapes, sizeof(kMagicShapes) / sizeof(kMagicShapes[0]));

    CfgFileSchema f;
    f.kind = "creature";
    f.file_name = "creature.cfg";
    add_keys(f, "common", false, creaturetype_common_commands);
    add_keys(f, "experience", false, creaturetype_experience_commands);
    add_keys(f, "instance", true, creaturetype_instance_commands);
    add_keys(f, "job", true, creaturetype_job_commands);
    add_keys(f, "angerjob", true, creaturetype_angerjob_commands);
    add_keys(f, "attackpref", true, creaturetype_attackpref_commands);
    apply_shapes(f, kCreatureShapes, sizeof(kCreatureShapes) / sizeof(kCreatureShapes[0]));
    schema.files.push_back(std::move(f));
}
