// Catch2 coverage for cfgc_schema*.cpp (docs/refactor/editor/fx-plans/10-config-content-model-and-writers.md, W2).
#include <catch2/catch_test_macros.hpp>

#include "cfgc_content.h"
#include "cfgc_schema.h"
#include "cfgc_validate.h"
#include "cfgc_writer.h"
#include "kfx_content_test_paths.h" // KFX_CONTENT_TEST_REPO_ROOT

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace {

std::string lower(std::string s)
{
    for (char &c : s)
        c = (char)std::tolower((unsigned char)c);
    return s;
}

std::string slurp(const fs::path &p)
{
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

CfgFieldSpec number_field(const char *key, int64_t min, int64_t max)
{
    CfgFieldSpec f;
    f.key = key;
    CfgValueSpec p;
    p.kind = CfgKind_Number;
    p.min = min;
    p.max = max;
    f.parts.push_back(p);
    return f;
}

std::vector<CfgDiagnostic> check(const CfgFieldSpec &f, const std::string &text, const CfgNameSets *names = nullptr)
{
    std::vector<CfgDiagnostic> out;
    cfgc_validate_value(f, text, names, out);
    return out;
}

} // namespace

TEST_CASE("validate: numbers report what the loader would clamp", "[cfgc_schema]")
{
    const CfgFieldSpec f = number_field("Level", 0, 9);
    CHECK(check(f, "5").empty());
    CHECK(check(f, "-1").at(0).code == "number_range");
    CHECK(check(f, "10").at(0).code == "number_range");
    CHECK(check(f, "abc").at(0).code == "not_a_number");
    CHECK(check(f, "").at(0).code == "missing_value");
}

TEST_CASE("validate: names against static lists and registries", "[cfgc_schema]")
{
    CfgFieldSpec f;
    f.key = "Trigger";
    CfgValueSpec p;
    p.kind = CfgKind_Enum;
    p.enum_names = {"LINE_OF_SIGHT_90", "PRESSURE"};
    f.parts.push_back(p);
    CHECK(check(f, "pressure").empty()); // case-insensitive
    CHECK(check(f, "3").empty());        // the loader accepts the number
    CHECK(check(f, "NOPE").at(0).code == "unknown_name");

    CfgFieldSpec reg;
    reg.key = "Crate";
    CfgValueSpec rp;
    rp.kind = CfgKind_Enum;
    rp.enum_registry = "object";
    reg.parts.push_back(rp);
    CfgNameSets names;
    names.add("object", "Crate_Box");
    CHECK(check(reg, "crate_box", &names).empty());
    CHECK(check(reg, "gone", &names).at(0).code == "unknown_name");
    CHECK(check(reg, "gone", nullptr).empty()); // no registry supplied: not checkable
}

TEST_CASE("validate: flag lists, multi-part values, ignored keys", "[cfgc_schema]")
{
    CfgFieldSpec flags;
    flags.key = "Properties";
    CfgValueSpec fp;
    fp.kind = CfgKind_Flags;
    fp.enum_names = {"A", "B"};
    flags.parts.push_back(fp);
    CHECK(check(flags, "A B").empty());
    CHECK(check(flags, "none").empty());
    CHECK(check(flags, "A C").at(0).code == "unknown_name");

    CfgFieldSpec two = number_field("Size", 0, 255);
    two.parts.push_back(two.parts[0]);
    CHECK(check(two, "1 2").empty());
    CHECK(check(two, "1").at(0).code == "too_few_values");
    CHECK(check(two, "1 300").at(0).code == "number_range");

    CfgFieldSpec dead = number_field("BarrackTime", 0, 10);
    dead.state = CfgState_Ignored;
    dead.note = "unused";
    const auto d = check(dead, "5");
    REQUIRE(d.size() == 1);
    CHECK(d[0].code == "ignored_key");
    CHECK(d[0].severity == CfgSev_Info);
}

TEST_CASE("engine schema reflects the field tables", "[cfgc_schema]")
{
    const ConfigSchema schema = build_engine_schema();
    const CfgFileSchema *trapdoor = schema.find("trapdoor");
    REQUIRE(trapdoor != nullptr);
    const CfgSectionSpec *trap = trapdoor->find_section("trap");
    REQUIRE(trap != nullptr);
    CHECK(trap->numbered);

    const CfgFieldSpec *name = trap->find_field("name");
    REQUIRE(name != nullptr);
    CHECK(name->parts.at(0).kind == CfgKind_Text);

    const CfgFieldSpec *sym = trap->find_field("SymbolSprites");
    REQUIRE(sym != nullptr);
    CHECK(sym->parts.size() == 2); // big and medium icon
    CHECK(sym->parts[0].kind == CfgKind_Icon);

    const CfgFieldSpec *level = trap->find_field("ActivationLevel");
    REQUIRE(level != nullptr);
    CHECK(level->parts[0].min == 0);
    CHECK(level->parts[0].max == 9);

    const CfgFieldSpec *hidden = trap->find_field("Hidden");
    REQUIRE(hidden != nullptr);
    CHECK(hidden->parts[0].max == 1);

    const CfgFieldSpec *trigger = trap->find_field("TriggerType");
    REQUIRE(trigger != nullptr);
    CHECK(trigger->parts[0].kind == CfgKind_Enum);
    CHECK_FALSE(trigger->parts[0].enum_names.empty()); // static names are copied

    const CfgFieldSpec *crate = trap->find_field("Crate");
    REQUIRE(crate != nullptr);
    CHECK(crate->parts[0].enum_registry == "object"); // dynamic names are a registry, not a copy

    // rules.cfg's blocks come from ruleblocks[] (the set itself has no field list).
    const CfgFileSchema *rules = schema.find("rules");
    REQUIRE(rules != nullptr);
    CHECK(rules->find_section("game") != nullptr);
    CHECK(rules->find_section("health") != nullptr);
    // The slab set is now exported.
    const CfgFileSchema *terrain = schema.find("terrain");
    REQUIRE(terrain != nullptr);
    CHECK(terrain->find_section("slab") != nullptr);
}

// Schema coverage (plan 10 §7.4): every section and key in every shipped file is either known
// to the schema or on an explicit allow-list with a reason.
namespace {

struct Residue
{
    std::set<std::string> sections; // "kind:[name]"
    std::set<std::string> keys;     // "kind:[basename]key"
    size_t files = 0;
};

Residue measure(const ConfigSchema &schema)
{
    Residue r;
    std::set<std::string> creature_stems;
    std::error_code ec;
    for (fs::directory_iterator it(fs::path(KFX_CONTENT_TEST_REPO_ROOT) / "config" / "creatrs", ec), end; !ec && it != end;
         it.increment(ec))
        creature_stems.insert(lower(it->path().stem().string()));

    for (const char *sub : {"core_files", "config"})
        for (const auto &e : fs::recursive_directory_iterator(fs::path(KFX_CONTENT_TEST_REPO_ROOT) / sub))
        {
            if (!e.is_regular_file() || e.path().extension() != ".cfg")
                continue;
            const std::string stem = lower(e.path().stem().string());
            const std::string parent = lower(e.path().parent_path().filename().string());
            std::string base = stem;
            const bool level_file = stem.compare(0, 3, "map") == 0 && stem.find('.') != std::string::npos;
            if (level_file)
                base = stem.substr(stem.find('.') + 1);
            std::string kind;
            // A campaign or map pack file sits directly in campgns/, levels/ or multiplayer/ (not in a *_cfgs folder).
            if (!level_file && (parent == "campgns" || parent == "levels" || parent == "multiplayer") && e.path().parent_path().parent_path().filename() == "core_files")
                kind = "campaign";
            else if (base == "trapdoor" || base == "terrain" || base == "objects" || base == "rules" || base == "magic" || base == "creature")
                kind = base;
            else if (creature_stems.count(base) && (parent == "creatrs" || parent == "creatures"
                         || (parent.size() > 5 && parent.compare(parent.size() - 5, 5, "_crtr") == 0) || level_file))
                kind = "creaturemodel";
            if (kind.empty())
                continue;
            const CfgFileSchema *fs_ = schema.find(kind);
            REQUIRE(fs_ != nullptr);
            r.files++;
            const ConfigContent c = read_config_content(ConfigDocument::parse(slurp(e.path())), kind, level_file);
            for (const CfgContentSection &s : c.sections)
            {
                if (cfgc_is_banner_section(s.name))
                    continue;
                const CfgSectionSpec *spec = fs_->find_section(lower(s.basename));
                if (spec == nullptr)
                    spec = fs_->find_section(s.basename);
                if (spec == nullptr && s.index >= 0)
                    spec = fs_->find_section(s.name); // a name that merely ends in digits
                if (spec == nullptr)
                {
                    r.sections.insert(kind + ":[" + (s.index >= 0 ? s.basename + "N" : s.name) + "]");
                    continue;
                }
                for (const CfgField &f : s.fields)
                    if (spec->find_field(f.key) == nullptr)
                        r.keys.insert(kind + ":[" + spec->basename + "]" + lower(f.key));
            }
        }
    return r;
}

} // namespace

TEST_CASE("every section and key of the shipped files is in the schema", "[cfgc_schema][corpus]")
{
    const ConfigSchema schema = build_engine_schema();
    const Residue r = measure(schema);
    CHECK(r.files > 500);
    for (const std::string &s : r.sections)
        FAIL_CHECK("section not in schema: " << s);
    for (const std::string &k : r.keys)
        FAIL_CHECK("key not in schema: " << k);
}

TEST_CASE("ignored keys are recorded with a reason", "[cfgc_schema]")
{
    const ConfigSchema schema = build_engine_schema();
    size_t ignored = 0;
    for (const CfgFileSchema &f : schema.files)
        for (const CfgSectionSpec &s : f.sections)
            for (const CfgFieldSpec &k : s.fields)
                if (k.state == CfgState_Ignored)
                {
                    ignored++;
                    CHECK_FALSE(k.note.empty());
                }
    CHECK(ignored >= 25);

    const CfgSectionSpec *rooms = schema.find("rules")->find_section("rooms");
    REQUIRE(rooms != nullptr);
    const CfgFieldSpec *barrack = rooms->find_field("BarrackTime");
    REQUIRE(barrack != nullptr);
    CHECK(barrack->state == CfgState_Ignored);
}

TEST_CASE("banner sections are recognised", "[cfgc_schema]")
{
    CHECK(cfgc_is_banner_section("###### MELEE ######"));
    CHECK_FALSE(cfgc_is_banner_section("shot12"));
    CHECK_FALSE(cfgc_is_banner_section(""));
}

TEST_CASE("base files validate against the schema with their own names", "[cfgc_schema][corpus]")
{
    const ConfigSchema schema = build_engine_schema();
    const fs::path base = fs::path(KFX_CONTENT_TEST_REPO_ROOT) / "config" / "fxdata";
    struct Item { const char *kind; const char *file; };
    const Item items[] = {{"trapdoor", "trapdoor.cfg"}, {"terrain", "terrain.cfg"}, {"objects", "objects.cfg"},
        {"rules", "rules.cfg"}, {"magic", "magic.cfg"}};

    std::vector<std::pair<std::string, ConfigContent>> loaded;
    CfgNameSets names;
    for (const Item &it : items)
    {
        ConfigContent c = read_config_content(ConfigDocument::parse(slurp(base / it.file)), it.kind, false);
        cfgc_collect_names(c, names);
        loaded.emplace_back(it.kind, std::move(c));
    }
    {
        // creature.cfg keeps its creatures as one list.
        std::ifstream cf(base / "creature.cfg", std::ios::binary);
        std::stringstream css;
        css << cf.rdbuf();
        cfgc_collect_creature_names(read_config_content(ConfigDocument::parse(css.str()), "creature", false), names);
    }
    REQUIRE(names.find("creature") != nullptr);
    CHECK(names.find("creature")->count("HORNY") == 1);
    REQUIRE(names.find("object") != nullptr);
    CHECK(names.find("trap")->size() >= 5);
    CHECK(names.find("door")->size() >= 5);

    std::map<std::string, size_t> by_code;
    std::vector<std::string> samples;
    for (const auto &kc : loaded)
    {
        const CfgFileSchema *file = schema.find(kc.first);
        REQUIRE(file != nullptr);
        for (const CfgContentSection &s : kc.second.sections)
        {
            const CfgSectionSpec *spec = file->find_section(s.basename);
            if (spec == nullptr)
                continue;
            for (const CfgField &f : s.fields)
            {
                const CfgFieldSpec *fspec = spec->find_field(f.key);
                if (fspec == nullptr || fspec->state == CfgState_Ignored)
                    continue;
                for (const std::string &v : f.values)
                {
                    std::vector<CfgDiagnostic> d;
                    cfgc_validate_value(*fspec, v, &names, d);
                    for (const CfgDiagnostic &x : d)
                    {
                        by_code[x.code]++;
                        if (samples.size() < 40)
                            samples.push_back(kc.first + ":[" + s.name + "] " + x.message + "  (value '" + v + "')");
                    }
                }
            }
        }
    }
    // The shipped base files are the reference: they must produce no warning at all.
    for (const std::string &s : samples)
        FAIL_CHECK(s);
    CHECK(by_code.empty());
}

TEST_CASE("keys that write the same struct field are recorded as aliases", "[cfgc_schema]")
{
    const ConfigSchema schema = build_engine_schema();
    const CfgSectionSpec *obj = schema.find("objects")->find_section("object");
    REQUIRE(obj != nullptr);
    const CfgFieldSpec *yz = obj->find_field("Size_YZ");
    REQUIRE(yz != nullptr);
    CHECK(yz->alias_of == "SIZE_Z"); // both write size_z; Size_Z is the spelling the base files use
    const CfgFieldSpec *z = obj->find_field("Size_Z");
    REQUIRE(z != nullptr);
    CHECK(z->alias_of.empty());
}

TEST_CASE("sacrifice recipes: result and victims are described and checked", "[cfgc_schema]")
{
    const ConfigSchema schema = build_engine_schema();
    const CfgSectionSpec *sac = schema.find("rules")->find_section("sacrifices");
    REQUIRE(sac != nullptr);
    const CfgFieldSpec *mk = sac->find_field("MkCreature");
    REQUIRE(mk != nullptr);
    CHECK(mk->repeat_last);
    REQUIRE(mk->parts.size() == 2);
    CHECK(mk->parts[0].enum_registry == "creature");
    CHECK(sac->find_field("PosSpellAll")->parts[0].enum_registry == "spell");
    const CfgFieldSpec *fn = sac->find_field("PosUniqFunc");
    REQUIRE(fn != nullptr);
    CHECK_FALSE(fn->parts[0].enum_names.empty());

    CfgNameSets names;
    for (const char *c : {"HORNY", "TROLL", "SPIDER", "IMP"})
        names.add("creature", c);
    auto codes = [&](const CfgFieldSpec &f, const std::string &v) {
        std::vector<CfgDiagnostic> d;
        cfgc_validate_value(f, v, &names, d);
        std::string out;
        for (const CfgDiagnostic &x : d)
            out += x.code + ",";
        return out;
    };
    CHECK(codes(*mk, "HORNY TROLL SPIDER TROLL").empty());
    CHECK(codes(*mk, "HORNY").find("too_few_values") != std::string::npos);        // a recipe needs a victim
    CHECK(codes(*mk, "HORNY TROLL NOPE TROLL") == "unknown_name,");             // every victim is checked
    CHECK(codes(*fn, "CHEAPER_IMPS IMP").empty());
    CHECK(codes(*fn, "NOT_A_FUNCTION IMP") == "unknown_name,");
}

TEST_CASE("a value's position is the row order, not the table's argnum", "[cfgc_schema]")
{
    const ConfigSchema schema = build_engine_schema();
    const CfgFieldSpec *slab = schema.find("trapdoor")->find_section("door")->find_field("SlabKind");
    REQUIRE(slab != nullptr);
    CHECK(slab->parts.size() == 2); // "SlabKind = DOOR_WOODEN DOOR_WOODEN2": both rows say argnum 0
    CHECK(slab->parts[0].enum_registry == "slab");
    CHECK(slab->parts[1].enum_registry == "slab");
    // A multi-value key written correctly is unchanged.
    CHECK(schema.find("trapdoor")->find_section("trap")->find_field("SymbolSprites")->parts.size() == 2);
}

TEST_CASE("base creature files validate against the curated creature schema", "[cfgc_schema][corpus]")
{
    const ConfigSchema schema = build_engine_schema();
    const fs::path base = fs::path(KFX_CONTENT_TEST_REPO_ROOT) / "config" / "fxdata";
    auto read = [](const fs::path &p) {
        std::ifstream f(p, std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    };
    // The names the creature files refer to: every registry of the base files.
    CfgNameSets names;
    const struct { const char *kind; const char *file; } files[] = {{"trapdoor", "trapdoor.cfg"}, {"terrain", "terrain.cfg"},
        {"objects", "objects.cfg"}, {"magic", "magic.cfg"}, {"creature", "creature.cfg"}};
    for (const auto &f : files)
    {
        const ConfigContent c = read_config_content(ConfigDocument::parse(read(base / f.file)), f.kind, false);
        cfgc_collect_names(c, names);
        cfgc_collect_creature_names(c, names);
    }
    REQUIRE(names.find("instance") != nullptr);
    REQUIRE(names.find("creaturejob") != nullptr);
    CHECK(names.find("creature")->size() > 30);

    size_t checked = 0;
    auto check = [&](const CfgFileSchema *fs_, const fs::path &p) {
        const auto d = cfgc_validate_document(ConfigDocument::parse(read(p)), fs_, &names);
        for (const CfgDiagnostic &x : d)
            if (x.severity >= CfgSev_Warning)
                FAIL_CHECK(p.filename().string() << ":" << x.line << " " << x.code << ": " << x.message);
        checked++;
    };
    check(schema.find("creature"), base / "creature.cfg");
    for (const auto &e : fs::directory_iterator(fs::path(KFX_CONTENT_TEST_REPO_ROOT) / "config" / "creatrs"))
        if (e.path().extension() == ".cfg")
            check(schema.find("creaturemodel"), e.path());
    CHECK(checked > 30);
}

TEST_CASE("shipped campaign and pack files validate and write back unchanged", "[cfgc_schema][corpus]")
{
    const ConfigSchema schema = build_engine_schema();
    const auto writer = cfgc_make_writer(schema, "campaign");
    REQUIRE(writer != nullptr);
    const CfgNameSets names;
    size_t checked = 0;
    for (const char *sub : {"campgns", "levels", "multiplayer"})
        for (const auto &e : fs::directory_iterator(fs::path(KFX_CONTENT_TEST_REPO_ROOT) / "core_files" / sub))
        {
            if (!e.is_regular_file() || e.path().extension() != ".cfg")
                continue;
            const std::string text = slurp(e.path());
            const ConfigDocument doc = ConfigDocument::parse(text);
            for (const CfgDiagnostic &x : cfgc_validate_document(doc, schema.find("campaign"), &names))
                // A few shipped files leave NAME_TEXT_ID / HIGH_SCORES empty; the loader skips them.
                if (x.severity >= CfgSev_Warning && x.code != "missing_value")
                    FAIL_CHECK(e.path().filename().string() << ":" << x.line << " " << x.code << ": " << x.message);

            // Setting every single-line key to the value it already has must not touch the file.
            ChangeSet cs;
            for (const CfgContentSection &s : read_config_content(doc, "campaign", false).sections)
            {
                for (const CfgField &f : s.fields)
                    if (f.values.size() == 1) // a key repeated in a block is left alone
                        cs.set(s.index >= 0 ? s.basename + std::to_string(s.index) : s.name, f.key, f.values[0]);
            }
            ConfigDocument copy = doc;
            writer->apply(copy, cs, nullptr);
            const std::string out = copy.serialize();
            size_t at = 0;
            while (at < out.size() && at < text.size() && out[at] == text[at])
                at++;
            INFO(e.path().filename().string() << " differs at byte " << at << ": was '" << text.substr(at, 50) << "' now '" << out.substr(at, 50) << "'");
            CHECK(out == text);
            checked++;
        }
    CHECK(checked >= 30);
}
