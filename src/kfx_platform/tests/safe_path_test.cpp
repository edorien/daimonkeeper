// Which file names from content (a script, a campaign file) may be opened: plain relative paths only (safe_path.h).
#include <catch2/catch_test_macros.hpp>

#include "safe_path.h"

#include <string>

TEST_CASE("plain relative paths are accepted", "[kfx_platform][safe_path]") {
    for (const char *ok : {"a.wav", "campgns/mymod/sounds/happy.wav", "campgns\\mymod\\happy.wav", "./a.wav", "a/./b",
                           "sounds/my sound.wav", "dots.in.name.wav", "a/b/c/d/e.txt", "a..b/c", "...x/y", "console.wav2/x",
                           "com10.wav", "lpt0", "aux2", "a-b_c"})
    {
        INFO(ok);
        CHECK(path_is_plain_relative(ok));
    }
}

TEST_CASE("paths that leave the game's folders are refused", "[kfx_platform][safe_path]") {
    for (const char *bad : {"", "/etc/passwd", "\\windows\\system32", "//server/share/x", "..", "../x", "a/../b", "a/..", "..\\x",
                            "a\\..\\..\\x", "C:\\x.wav", "C:x.wav", "c:/x", "a/b:stream", "x.wav:Zone.Identifier",
                            "a//b", "a/", "a\\", "/", "...", ".. ", "a/... /b", "a/.. ./b"})
    {
        INFO(bad);
        CHECK_FALSE(path_is_plain_relative(bad));
    }
    CHECK_FALSE(path_is_plain_relative(nullptr));
}

TEST_CASE("windows device names and odd characters are refused", "[kfx_platform][safe_path]") {
    for (const char *bad : {"nul", "NUL.wav", "con", "a/con.txt", "a/PRN", "aux.x.y", "com1", "COM9.wav", "lpt1", "a/LPT3.dat", "nul .wav",
                            "a*b", "a?b", "a<b", "a>b", "a|b", "a\"b", "a\tb", "a\nb", "a\x7f"})
    {
        INFO(bad);
        CHECK_FALSE(path_is_plain_relative(bad));
    }
}

TEST_CASE("a path of 1024 bytes or more is refused", "[kfx_platform][safe_path]") {
    CHECK(path_is_plain_relative(std::string(1023, 'a').c_str()));
    CHECK_FALSE(path_is_plain_relative(std::string(1024, 'a').c_str()));
}

TEST_CASE("path_cannot_escape accepts what a folder name or a search pattern can be", "[kfx_platform][safe_path]") {
    for (const char *ok : {"", "a.wav", "campgns/mymod", "campgns/mymod/", "*.cfg", "map?????.zip", "a//b", "./a", "a/./b", "..a", "a..",
                           "a..b/c", "dots.in.name", "sounds/my sound.wav", "com10.wav", "lpt0", "mods/x y"})
    {
        INFO(ok);
        CHECK(path_cannot_escape(ok));
    }
    CHECK(path_cannot_escape(nullptr));
}

TEST_CASE("path_cannot_escape refuses anything that could leave the folder it is joined to", "[kfx_platform][safe_path]") {
    for (const char *bad : {"/etc/passwd", "\\windows\\system32", "//server/share", "..", "../x", "a/../b", "a/..", "..\\x", "a\\..\\..\\x",
                            "C:\\x", "C:x", "c:/x", "a/b:stream", "x:Zone.Identifier", "...", ".. ", "a/... /b", "a/.. ./b", "a/ /b",
                            "nul", "a/con.txt", "COM1.wav", "a\tb", "a\x7f"})
    {
        INFO(bad);
        CHECK_FALSE(path_cannot_escape(bad));
    }
    CHECK_FALSE(path_cannot_escape(std::string(1024, 'a').c_str()));
    CHECK(path_cannot_escape(std::string(1023, 'a').c_str()));
}
