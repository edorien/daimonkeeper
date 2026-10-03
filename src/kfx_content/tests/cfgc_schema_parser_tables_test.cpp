// kfx_content: the editor schema, the config parsers' NamedField tables and the
// level scripts' key tables agree (refactor pass 3, S04).
//
// The creature model, creature.cfg and magic.cfg spell/special blocks are
// NamedField tables, and the editor schema reads its keys, value kinds and
// bounds from them. A row whose parse function the schema can't read (a key
// with rules of its own) needs a hand-kept shape in cfgc_schema_creature.cpp;
// these tests keep those shapes to exactly such rows. Level scripts'
// SET_CREATURE_CONFIGURATION still names creature model keys through the
// creatmodel_*_commands tables, which must list the same keys.
#include <catch2/catch_test_macros.hpp>

#include "cfgc_schema.h"
#include "cfgc_schema_shapes.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>
#include <string>

extern "C" {
#include "config.h"
#include "config_creature.h"
#include "config_crtrmodel.h"
#include "config_magic.h"
}

namespace {

std::string upper(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::toupper(c); });
    return s;
}

struct Block
{
    const char *file;
    const char *section;
    const struct NamedField *table;
    const struct NamedCommand *script_keys; // the key table SET_CREATURE_CONFIGURATION uses, if any
};

const Block kBlocks[] = {
    {"creaturemodel", "attributes", creaturemodel_attributes_named_fields, creatmodel_attributes_commands},
    {"creaturemodel", "attraction", creaturemodel_attraction_named_fields, creatmodel_attraction_commands},
    {"creaturemodel", "annoyance", creaturemodel_annoyance_named_fields, creatmodel_annoyance_commands},
    {"creaturemodel", "senses", creaturemodel_senses_named_fields, creatmodel_senses_commands},
    {"creaturemodel", "appearance", creaturemodel_appearance_named_fields, creatmodel_appearance_commands},
    {"creaturemodel", "experience", creaturemodel_experience_named_fields, creatmodel_experience_commands},
    {"creaturemodel", "jobs", creaturemodel_jobs_named_fields, creatmodel_jobs_commands},
    {"creaturemodel", "sprites", creaturemodel_sprites_named_fields, creature_graphics_desc},
    {"creaturemodel", "sounds", creaturemodel_sounds_named_fields, creatmodel_sounds_commands},
    {"creature", "common", creaturetype_common_named_fields, nullptr},
    {"creature", "experience", creaturetype_experience_named_fields, nullptr},
    {"creature", "instance", creaturetype_instance_named_fields, nullptr},
    {"creature", "job", creaturetype_job_named_fields, nullptr},
    {"creature", "angerjob", creaturetype_angerjob_named_fields, nullptr},
    {"creature", "attackpref", creaturetype_attackpref_named_fields, nullptr},
    {"magic", "spell", magic_spell_named_fields, nullptr},
    {"magic", "special", magic_special_named_fields, nullptr},
};

/** A row the schema can't read and that isn't a key the loader ignores: its value grammar needs a shape. */
bool needs_shape(const struct NamedField &row)
{
    return cfgc_table_row_kind(row) == CfgKind_Custom && row.parse_func != value_ignored;
}

} // namespace

TEST_CASE("level scripts' creature model key tables list the keys the parser tables read", "[cfgc_schema]")
{
    for (const Block &b : kBlocks)
    {
        if (b.script_keys == nullptr)
            continue;
        INFO(b.file << " [" << b.section << "]");
        std::set<std::string> table_keys, script_keys;
        for (const struct NamedField *f = b.table; f->name != nullptr; f++)
            table_keys.insert(upper(f->name));
        for (const struct NamedCommand *c = b.script_keys; c->name != nullptr; c++)
            script_keys.insert(upper(c->name));
        std::ostringstream only_table, only_script;
        for (const std::string &k : table_keys)
            if (script_keys.count(k) == 0)
                only_table << " " << k;
        for (const std::string &k : script_keys)
            if (table_keys.count(k) == 0)
                only_script << " " << k;
        INFO("read by the parser, unknown to scripts:" << only_table.str());
        INFO("known to scripts, not read by the parser:" << only_script.str());
        CHECK(table_keys == script_keys);
    }
}

TEST_CASE("the editor schema has every key of the parser tables, and a value shape for each", "[cfgc_schema]")
{
    const ConfigSchema schema = build_engine_schema();
    for (const Block &b : kBlocks)
    {
        const CfgFileSchema *file = schema.find(b.file);
        REQUIRE(file != nullptr);
        const CfgSectionSpec *sec = file->find_section(b.section);
        REQUIRE(sec != nullptr);
        for (const struct NamedField *f = b.table; f->name != nullptr; f++)
        {
            INFO(b.file << " [" << b.section << "] " << f->name);
            const CfgFieldSpec *field = sec->find_field(f->name);
            REQUIRE(field != nullptr);
            REQUIRE_FALSE(field->parts.empty());
            for (const CfgValueSpec &p : field->parts)
                CHECK(p.kind != CfgKind_Unspecified);
            // A key with rules of its own needs a hand-kept shape; one the schema reads from its row must not have one.
            CHECK(cfgc_creature_shape_override(b.file, b.section, f->name) == needs_shape(*f));
        }
    }
}

TEST_CASE("every hand-kept creature and magic shape names a key of the parser tables", "[cfgc_schema]")
{
    // A shape for a key no table has (renamed or dropped) would do nothing.
    const std::vector<CfgShapeOverride> overrides = cfgc_creature_shape_overrides();
    CHECK(overrides.size() > 30);
    for (const CfgShapeOverride &o : overrides)
    {
        bool in_table = false;
        for (const Block &b : kBlocks)
            if (o.kind == b.file && o.section == b.section)
                for (const struct NamedField *f = b.table; f->name != nullptr; f++)
                    in_table = in_table || upper(f->name) == upper(o.key);
        INFO(o.kind << " [" << o.section << "] " << o.key);
        CHECK(in_table);
    }
}
