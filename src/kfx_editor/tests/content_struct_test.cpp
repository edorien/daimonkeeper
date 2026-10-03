// Catch2 coverage for content_struct.cpp (plan 03 F4 logic).
#include <catch2/catch_test_macros.hpp>

#include "content_struct.h"
#include "content_target.h"
#include "content_names.h"
#include "content_trapdoor.h"
#include "content_spells.h"
#include "content_creature.h"
#include "content_rooms.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

void spit(const fs::path &p, const std::string &s)
{
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f << s;
}

std::string slurp(const fs::path &p)
{
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

struct Tree
{
    fs::path root;
    Tree()
    {
        // per process: ctest runs each case as its own process, in parallel
        root = fs::temp_directory_path() / ("kfx_content_struct_test_" + std::to_string(getpid()));
        fs::remove_all(root);
        spit(root / "fxdata" / "rules.cfg",
            "[game]\n; pay\nPayDayGap = 10000\nPayDaySpeed = 100\n[research]\nResearch = MAGIC A 1\nResearch = MAGIC B 2\n");
        spit(root / "camp" / "rules.cfg", "[game]\nPayDaySpeed = 150\n");
    }
    ~Tree() { fs::remove_all(root); }
    ConfigTarget target() const
    {
        ContentCampaign c;
        c.cfg_dir = (root / "camp").string();
        c.levels_dir = (root / "lvls").string();
        c.crtr_dir = (root / "campcrtr").string();
        return content_target_make((root / "fxdata").string(), (root / "creatrs").string(), &c, 4);
    }
};

} // namespace

TEST_CASE("field views: source layer, default, beneath", "[content_struct]")
{
    Tree t;
    StructuredSession s;
    REQUIRE(s.open(t.target(), "rules", "rules.cfg", CfgLayer_Level));
    CHECK(s.writable());

    FieldView gap = s.value_of("game", "PayDayGap");
    CHECK(gap.text == "10000");
    CHECK(gap.is_set);
    CHECK(gap.source == CfgLayer_Base);
    CHECK_FALSE(gap.overridden_here);

    FieldView speed = s.value_of("game", "paydayspeed");
    CHECK(speed.text == "150");
    CHECK(speed.source == CfgLayer_Campaign);

    FieldView unset = s.value_of("game", "GoldPileMaximum"); // in no layer: the loader's default
    CHECK_FALSE(unset.is_set);
    CHECK_FALSE(unset.text.empty());
}

TEST_CASE("edits are pending until applied; equal-to-inherited edits vanish", "[content_struct]")
{
    Tree t;
    StructuredSession s;
    REQUIRE(s.open(t.target(), "rules", "rules.cfg", CfgLayer_Level));

    s.set("game", "PayDaySpeed", "200");
    CHECK(s.dirty());
    FieldView v = s.value_of("game", "PayDaySpeed");
    CHECK(v.pending);
    CHECK(v.text == "200");
    CHECK(v.overridden_here);
    CHECK(v.source == CfgLayer_Level);
    CHECK(v.has_beneath);
    CHECK(v.beneath == "150");

    s.set("game", "PayDaySpeed", "150"); // the inherited value: not an override
    CHECK_FALSE(s.dirty());
    s.set("game", "PayDayGap", "10000");
    CHECK_FALSE(s.dirty());
}

TEST_CASE("apply writes the level file and reloads", "[content_struct]")
{
    Tree t;
    StructuredSession s;
    REQUIRE(s.open(t.target(), "rules", "rules.cfg", CfgLayer_Level));
    s.set("game", "PayDaySpeed", "200");
    s.set("game", "PayDayGap", "5000");
    const auto diags = s.diagnostics();
    CHECK(diags.empty());
    std::string err;
    size_t warnings = 9;
    REQUIRE(s.apply(&err, &warnings));
    CHECK(warnings == 0);
    CHECK_FALSE(s.dirty());
    const std::string text = slurp(t.root / "lvls" / "map00004.rules.cfg");
    CHECK(text.find("PayDaySpeed = 200") != std::string::npos);
    CHECK(text.find("PayDayGap = 5000") != std::string::npos);
    CHECK(text.find("written by the map editor") != std::string::npos);

    FieldView v = s.value_of("game", "PayDaySpeed");
    CHECK(v.text == "200");
    CHECK(v.overridden_here);
    CHECK_FALSE(v.pending);

    // Reset removes the key; the last one deletes the generated file.
    s.reset("game", "PayDaySpeed");
    s.reset("game", "PayDayGap");
    CHECK(s.value_of("game", "PayDaySpeed").text == "150");
    REQUIRE(s.apply(&err));
    CHECK_FALSE(fs::exists(t.root / "lvls" / "map00004.rules.cfg"));
    CHECK(s.value_of("game", "PayDaySpeed").source == CfgLayer_Campaign);
}

TEST_CASE("reset of a key this layer does not set is not an edit", "[content_struct]")
{
    Tree t;
    StructuredSession s;
    REQUIRE(s.open(t.target(), "rules", "rules.cfg", CfgLayer_Level));
    s.reset("game", "PayDayGap");
    CHECK_FALSE(s.dirty());
}

TEST_CASE("diagnostics report what the loader would clamp", "[content_struct]")
{
    Tree t;
    StructuredSession s;
    REQUIRE(s.open(t.target(), "rules", "rules.cfg", CfgLayer_Level));
    s.set("game", "DisplayPortalLimit", "7"); // 0 or 1
    const auto d = s.diagnostics();
    REQUIRE(d.size() == 1);
    CHECK(d[0].code == "number_range");
    CHECK(d[0].key == "DISPLAYPORTALLIMIT");
}

TEST_CASE("list blocks: replace, reset back to the layer beneath", "[content_struct]")
{
    Tree t;
    StructuredSession s;
    REQUIRE(s.open(t.target(), "rules", "rules.cfg", CfgLayer_Level));
    ListView base = s.list("research", "Research");
    CHECK(base.lines.size() == 2);
    CHECK(base.source == CfgLayer_Base);

    s.set_list("research", "Research", {"ROOM X 5"});
    ListView v = s.list("research", "Research");
    CHECK(v.pending);
    CHECK(v.overridden_here);
    CHECK(v.lines == std::vector<std::string>{"ROOM X 5"});
    std::string err;
    REQUIRE(s.apply(&err));
    CHECK(slurp(t.root / "lvls" / "map00004.rules.cfg").find("Research = ROOM X 5") != std::string::npos);

    s.reset_list("research", "Research");
    CHECK(s.list("research", "Research").lines.size() == 2); // shows the base list again
    REQUIRE(s.apply(&err));
    CHECK_FALSE(fs::exists(t.root / "lvls" / "map00004.rules.cfg"));
}

TEST_CASE("base is read only and a missing layer cannot be opened", "[content_struct]")
{
    Tree t;
    StructuredSession s;
    REQUIRE(s.open(t.target(), "rules", "rules.cfg", CfgLayer_Base));
    CHECK_FALSE(s.writable());
    s.set("game", "PayDayGap", "1");
    CHECK_FALSE(s.dirty());
    ConfigTarget none = content_target_make((t.root / "fxdata").string(), "", nullptr, -1);
    StructuredSession n;
    CHECK_FALSE(n.open(none, "rules", "rules.cfg", CfgLayer_Level));
    CHECK_FALSE(n.open(t.target(), "nosuchkind", "x.cfg", CfgLayer_Level));
}

TEST_CASE("names: creatures, spells and objects from every file of the target", "[content_struct]")
{
    Tree t;
    spit(t.root / "fxdata" / "creature.cfg", "[common]\nCreatures = IMP TROLL\n");
    spit(t.root / "fxdata" / "magic.cfg", "[spell1]\nName = SPELL_HEAL\n[spell2]\nName = SPELL_SLOW\n");
    spit(t.root / "camp" / "creature.cfg", "[common]\nCreatures = IMP TROLL ORC\n");
    const CfgNameSets base = content_collect_names(t.target(), CfgLayer_Base);
    CHECK(content_registry_list(base, "creature") == std::vector<std::string>{"IMP", "TROLL"});
    CHECK(content_registry_list(base, "spell") == std::vector<std::string>{"SPELL_HEAL", "SPELL_SLOW"});
    const CfgNameSets camp = content_collect_names(t.target(), CfgLayer_Campaign);
    CHECK(content_registry_list(camp, "creature").size() == 3); // the campaign's list adds ORC
    CHECK(content_registry_list(base, "nosuch").empty());

    StructuredSession s;
    REQUIRE(s.open(t.target(), "rules", "rules.cfg", CfgLayer_Level));
    CHECK(content_registry_list(s.names(), "creature").size() == 3);
    // A recipe naming a creature nobody defines is flagged.
    s.set_list("sacrifices", "MkCreature", {"IMP NOPE"});
    const auto d = s.diagnostics();
    REQUIRE(d.size() == 1);
    CHECK(d[0].code == "unknown_name");
}

TEST_CASE("trap and door keys are grouped into the form's tabs", "[content_struct]")
{
    CHECK(trapdoor_group_of(false, "ManufactureRequired") == "Build");
    CHECK(trapdoor_group_of(false, "CRATE") == "Build");
    CHECK(trapdoor_group_of(false, "TriggerType") == "Behaviour");
    CHECK(trapdoor_group_of(false, "Health") == "Behaviour");
    CHECK(trapdoor_group_of(true, "SlabKind") == "Behaviour");
    CHECK(trapdoor_group_of(true, "OpenSpeed") == "Behaviour");
    CHECK(trapdoor_group_of(false, "OpenSpeed") == "Advanced"); // not a trap key
    CHECK(trapdoor_group_of(false, "PlaceOnBridge") == "Placement");
    CHECK(trapdoor_group_of(false, "PlaceSound") == "Look & sound");
    CHECK(trapdoor_group_of(false, "AnimationID") == "Advanced");
    CHECK(trapdoor_group_of(false, "LightRadius") == "Advanced");
    // Every key of the trap and door blocks lands in one of the listed tabs.
    const ConfigSchema schema = build_engine_schema();
    const std::vector<std::string> &groups = trapdoor_groups();
    for (const char *block : {"trap", "door"})
        for (const CfgFieldSpec &f : schema.find("trapdoor")->find_section(block)->fields)
        {
            const std::string g = trapdoor_group_of(std::string(block) == "door", f.key);
            CHECK(std::find(groups.begin(), groups.end(), g) != groups.end());
        }
}

TEST_CASE("blocks of one kind are listed by number and marked when this layer has them", "[content_struct]")
{
    Tree t;
    spit(t.root / "fxdata" / "trapdoor.cfg", "[trap0]\nName = A\n[trap1]\nName = B\nHealth = 5\n[door1]\nName = D\n");
    spit(t.root / "camp" / "trapdoor.cfg", "[trap1]\nHealth = 9\n");
    StructuredSession s;
    REQUIRE(s.open(t.target(), "trapdoor", "trapdoor.cfg", CfgLayer_Campaign));
    CHECK(s.section_ids("trap") == std::vector<std::string>{"trap0", "trap1"});
    CHECK(s.section_ids("door") == std::vector<std::string>{"door1"});
    CHECK(s.section_touched("trap1"));
    CHECK_FALSE(s.section_touched("trap0"));
    s.set("trap0", "Health", "77");
    CHECK(s.section_touched("trap0")); // a pending edit counts
}

TEST_CASE("magic keys are grouped into the spell editor's tabs", "[content_struct]")
{
    CHECK(spells_group_of("power", "Cost") == "Cost and strength");
    CHECK(spells_group_of("power", "castability") == "Casting");
    CHECK(spells_group_of("power", "PanelTabIndex") == "Look and sound");
    CHECK(spells_group_of("spell", "ShotModel") == "Effect");
    CHECK(spells_group_of("spell", "AuraDuration") == "Duration and aura");
    CHECK(spells_group_of("shot", "Damage") == "Damage");
    CHECK(spells_group_of("shot", "HitType") == "Hit rules");
    CHECK(spells_group_of("shot", "Size_XY") == "Size and physics");
    CHECK(spells_group_of("shot", "SomethingNew") == "Advanced");
    CHECK(spells_group_of("special", "Value") == "Special");
    // Every key of the four block kinds lands in one of that kind's tabs.
    const ConfigSchema schema = build_engine_schema();
    for (const char *block : {"power", "spell", "shot", "special"})
    {
        const std::vector<std::string> groups = spells_groups(std::string(block));
        REQUIRE_FALSE(groups.empty());
        const CfgSectionSpec *sec = schema.find("magic")->find_section(block);
        REQUIRE(sec != nullptr);
        for (const CfgFieldSpec &f : sec->fields)
            CHECK(std::find(groups.begin(), groups.end(), spells_group_of(block, f.key)) != groups.end());
    }
    // The per-level arrays are described as arrays of numbers.
    const CfgSectionSpec *power = schema.find("magic")->find_section("power");
    CHECK(power->find_field("Cost")->parts.size() == 9);
    CHECK(power->find_field("Power")->parts.size() == 10);
}

TEST_CASE("ability (instance) keys are grouped and the creature files are described", "[content_struct]")
{
    CHECK(spells_group_of("instance", "Time") == "Timing");
    CHECK(spells_group_of("instance", "FPResetTime") == "First person");
    CHECK(spells_group_of("instance", "RangeMax") == "Targeting");
    CHECK(spells_group_of("instance", "Graphics") == "Look and sound");
    CHECK(spells_group_of("instance", "Function") == "Advanced");
    const ConfigSchema schema = build_engine_schema();
    const CfgFileSchema *creature = schema.find("creature");
    REQUIRE(creature != nullptr);
    const CfgSectionSpec *inst = creature->find_section("instance");
    REQUIRE(inst != nullptr);
    const std::vector<std::string> groups = spells_groups("instance");
    for (const CfgFieldSpec &f : inst->fields)
        CHECK(std::find(groups.begin(), groups.end(), spells_group_of("instance", f.key)) != groups.end());
    // The model files: shapes of the multi-value keys.
    const CfgFileSchema *model = schema.find("creaturemodel");
    CHECK(model->find_section("experience")->find_field("Powers")->parts.size() == 10);
    CHECK(model->find_section("experience")->find_field("LevelsTrainValues")->parts.size() == 9);
    CHECK(model->find_section("attraction")->find_field("EntranceRoom")->parts.size() == 3);
    CHECK(model->find_section("attributes")->find_field("Size")->parts.size() == 2);
    CHECK(model->find_section("attributes")->find_field("Properties")->parts[0].kind == CfgKind_Flags);
}

TEST_CASE("experience scaling is the engine's formula", "[content_struct]")
{
    CHECK(creature_level_value(300, 35, 0) == 300);
    CHECK(creature_level_value(300, 35, 1) == 405);  // 300 + 35% of 300
    CHECK(creature_level_value(300, 35, 9) == 300 + (35 * 300 * 9) / 100);
    CHECK(creature_level_value(101, 35, 1) == 101 + 35); // integer division, as the engine
    CHECK(creature_level_value(0, 35, 5) == 0);
}

TEST_CASE("a creature model file is edited on its own layer file", "[content_struct]")
{
    Tree t;
    spit(t.root / "creatrs" / "imp.cfg", "[attributes]\nHealth = 100\nStrength = 20\n");
    spit(t.root / "campcrtr" / "imp.cfg", "[attributes]\nHealth = 150\n");
    StructuredSession s;
    REQUIRE(s.open(t.target(), "creaturemodel", "imp.cfg", CfgLayer_Level, true));
    CHECK(s.value_of("attributes", "Health").text == "150");
    CHECK(s.value_of("attributes", "Health").source == CfgLayer_Campaign);
    s.set("attributes", "Health", "175");
    s.set("attributes", "Properties", "BLEEDS FLYING");
    CHECK(s.diagnostics().empty());
    std::string err;
    REQUIRE(s.apply(&err));
    const std::string text = slurp(t.root / "lvls" / "map00004.imp.cfg");
    CHECK(text.find("Health = 175") != std::string::npos);
    CHECK(text.find("Properties = BLEEDS FLYING") != std::string::npos);
    CHECK(text.find("Creature Configuration") != std::string::npos); // the generated header
    // An unknown property is flagged.
    s.set("attributes", "Properties", "BLEEDS NOT_A_PROPERTY");
    const auto d = s.diagnostics();
    REQUIRE(d.size() == 1);
    CHECK(d[0].code == "unknown_name");
}

TEST_CASE("terrain keys are grouped into the room editor's tabs", "[content_struct]")
{
    CHECK(rooms_group_of("room", "Cost") == "Build");
    CHECK(rooms_group_of("room", "SlabAssign") == "Build");
    CHECK(rooms_group_of("room", "TotalCapacity") == "Capacity");
    CHECK(rooms_group_of("room", "Roles") == "Roles");
    CHECK(rooms_group_of("room", "SymbolSprites") == "Look and sound");
    CHECK(rooms_group_of("room", "CreatureCreation") == "Advanced");
    CHECK(rooms_group_of("slab", "GoldHeld") == "Digging");
    CHECK(rooms_group_of("slab", "IsOwnable") == "Ownership");
    CHECK(rooms_group_of("slab", "BlockFlags") == "Block flags");
    CHECK(rooms_group_of("block_health", "DIRT") == "Health");
    const ConfigSchema schema = build_engine_schema();
    for (const char *block : {"room", "slab", "block_health"})
    {
        const std::vector<std::string> groups = rooms_groups(block);
        REQUIRE_FALSE(groups.empty());
        const CfgSectionSpec *sec = schema.find("terrain")->find_section(block);
        REQUIRE(sec != nullptr);
        for (const CfgFieldSpec &f : sec->fields)
            CHECK(std::find(groups.begin(), groups.end(), rooms_group_of(block, f.key)) != groups.end());
    }
}
