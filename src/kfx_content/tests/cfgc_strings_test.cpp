// Catch2 coverage for cfgc_strings.cpp (docs/refactor/editor/fx-plans/09-text-strings-editor.md X1).
#include <catch2/catch_test_macros.hpp>

#include "cfgc_strings.h"
#include "kfx_content_test_paths.h" // KFX_CONTENT_TEST_REPO_ROOT

#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

namespace fs = std::filesystem;

namespace {

std::string slurp(const fs::path &p)
{
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string nuls(std::initializer_list<const char *> entries)
{
    std::string out;
    for (const char *e : entries)
    {
        out += e;
        out += '\0';
    }
    return out;
}

} // namespace

TEST_CASE("strings file: entries are NUL-terminated and the ordinal is the id", "[cfgc_strings]")
{
    const StringsFile f = StringsFile::parse(nuls({"", "one", "", "three"}));
    REQUIRE(f.size() == 4);
    CHECK(f.raw(0).empty());
    CHECK(f.raw(1) == "one");
    CHECK(f.is_empty(2));
    CHECK(f.raw(3) == "three");
    CHECK(f.raw(99).empty());
}

TEST_CASE("strings file: any bytes round trip", "[cfgc_strings]")
{
    for (const std::string &s : {std::string(), std::string("\0", 1), std::string("abc"), std::string("abc\0", 4),
             std::string("\0\0\0", 3), std::string("a\0b", 3), std::string("x\r\ny\0z", 6)})
        CHECK(StringsFile::parse(s).serialize() == s);
}

TEST_CASE("strings file: editing, growing and trimming", "[cfgc_strings]")
{
    StringsFile f = StringsFile::parse(nuls({"", "one"}));
    f.set_raw(4, "four");
    CHECK(f.size() == 5);
    CHECK(f.serialize() == std::string("\0one\0\0\0four\0", 12));
    f.set_raw(4, "");
    f.trim_trailing_empty();
    CHECK(f.size() == 2); // the trailing empty entries went, "one" stayed
    // A file that ends without a NUL keeps that until it is written.
    StringsFile g = StringsFile::parse("abc");
    CHECK(g.serialize() == "abc");
    g.set_raw(1, "d");
    CHECK(g.serialize() == std::string("abc\0d\0", 6));
}

TEST_CASE("strings file: what is written is at least 16 bytes", "[cfgc_strings]")
{
    StringsFile f = StringsFile::parse(nuls({"", "hi"}));
    const std::string out = f.serialize_for_write();
    CHECK(out.size() == 16);
    CHECK(out.compare(0, 4, std::string("\0hi\0", 4)) == 0);
    CHECK(StringsFile::parse(out).raw(1) == "hi"); // the padding is empty entries
}

TEST_CASE("code page: decode and encode agree on what the game shows", "[cfgc_strings]")
{
    CHECK(cfgc_strings_decode("Hello\r\nworld") == "Hello\r\nworld");
    CHECK(cfgc_strings_decode("caf\x82") == "caf\xC3\xA9"); // byte 0x82 is e acute
    const StringsEncodeResult r = cfgc_strings_encode("caf\xC3\xA9 ok");
    CHECK(r.ok);
    CHECK(r.bytes == "caf\x82 ok");
    // A character the page cannot hold is reported and left out.
    const StringsEncodeResult bad = cfgc_strings_encode("a\xE4\xB8\xAD" "b\xE4\xB8\xAD");
    CHECK_FALSE(bad.ok);
    CHECK(bad.bytes == "ab");
    REQUIRE(bad.unrepresentable.size() == 1);
    CHECK(bad.unrepresentable[0] == 0x4E2D);
    // Every byte the page maps survives decode then encode.
    for (int b = 1; b < 256; b++)
    {
        const std::string raw(1, (char)b);
        const std::string utf = cfgc_strings_decode(raw);
        if (utf == "?" && b != '?')
            continue; // unmapped byte: decodes as '?', the one lossy case
        CHECK(cfgc_strings_encode(utf).bytes == raw);
    }
}

TEST_CASE("stack: the highest layer with a non-empty entry wins", "[cfgc_strings]")
{
    StringsStack s;
    s.layers[StringLayer_Base] = StringsFile::parse(nuls({"", "base one", "base two", "base three"}));
    s.layers[StringLayer_Campaign] = StringsFile::parse(nuls({"", "camp one", "", ""}));
    s.layers[StringLayer_Level] = StringsFile::parse(nuls({"", "", "", "level three"}));
    s.present[StringLayer_Base] = s.present[StringLayer_Campaign] = s.present[StringLayer_Level] = true;
    auto e1 = s.effective(1);
    CHECK(e1.raw == "camp one");
    CHECK(e1.source == StringLayer_Campaign);
    REQUIRE(e1.has_beneath);
    CHECK(e1.beneath_raw == "base one");
    auto e2 = s.effective(2);
    CHECK(e2.raw == "base two"); // empty entries inherit
    CHECK(e2.source == StringLayer_Base);
    CHECK_FALSE(e2.has_beneath);
    auto e3 = s.effective(3);
    CHECK(e3.source == StringLayer_Level);
    CHECK(e3.beneath_source == StringLayer_Base);
    CHECK_FALSE(s.effective(0).found);
    CHECK(s.id_count() == 4);
    s.present[StringLayer_Level] = false;
    CHECK(s.effective(3).source == StringLayer_Base);
}

TEST_CASE("every shipped language string file round trips byte for byte", "[cfgc_strings][corpus]")
{
    const std::regex name("(gtext_[a-z]{3}|text_[a-z]{3}|map[0-9]{5}\\.[a-z]{3})\\.dat", std::regex::icase);
    size_t files = 0, entries = 0;
    for (const char *sub : {"core_files", "config"})
        for (const auto &e : fs::recursive_directory_iterator(fs::path(KFX_CONTENT_TEST_REPO_ROOT) / sub))
        {
            if (!e.is_regular_file() || !std::regex_match(e.path().filename().string(), name))
                continue;
            const std::string bytes = slurp(e.path());
            const StringsFile f = StringsFile::parse(bytes);
            REQUIRE(f.serialize() == bytes);
            files++;
            entries += f.size();
        }
    CHECK(files > 30);
    CHECK(entries > 1000);
}

TEST_CASE("language codes", "[cfgc_strings]")
{
    CHECK(cfgc_lang_is_legacy_codepage("eng"));
    CHECK(cfgc_lang_is_legacy_codepage("rus"));
    CHECK_FALSE(cfgc_lang_is_legacy_codepage("jpn"));
    CHECK_FALSE(cfgc_lang_is_legacy_codepage("chi"));
    CHECK(cfgc_string_languages().size() >= 17);
}
