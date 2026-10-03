// kfx_script: serialising an API reply (api_json_out.h, refactor pass 3 S06). Every reply and event api.c sends
// goes through api_json_serialise(); these check the bytes and the size limit (pass 3 finding F4: replies longer
// than a fixed 1 KB / 4 KB stack buffer used to be dropped).
#include <catch2/catch_test_macros.hpp>

#include "api_json_out.h"

#include <cstdlib>
#include <string>

namespace {

struct Reply {
    VALUE root;
    explicit Reply(const std::string &error)
    {
        value_init_dict(&root);
        value_init_int32(value_dict_add(&root, "ack"), 7);
        value_init_bool(value_dict_add(&root, "success"), false);
        value_init_string(value_dict_add(&root, "error"), error.c_str());
    }
    ~Reply() { value_fini(&root); }
};

std::string serialise(const VALUE *root, size_t max_len, bool *ok = nullptr)
{
    size_t len = 12345;
    char *msg = api_json_serialise(root, max_len, &len);
    if (ok) *ok = (msg != nullptr);
    if (msg == nullptr) {
        CHECK(len == 0);
        return std::string();
    }
    std::string s(msg, len);
    CHECK(msg[len] == '\0');
    std::free(msg);
    return s;
}

} // namespace

TEST_CASE("api_json_serialise: a small reply is minimised JSON ending in a newline", "[kfx_script][api_json_out]")
{
    Reply r("BAD_ACTION");
    const std::string s = serialise(&r.root, 1024 * 1024);
    CHECK(s.back() == '\n');
    CHECK(s.find(' ') == std::string::npos);
    CHECK(s.find("\"error\":\"BAD_ACTION\"") != std::string::npos);
    CHECK(s.find("\"ack\":7") != std::string::npos);
    CHECK(s.front() == '{');
}

TEST_CASE("api_json_serialise: a reply past the old 1 KB and 4 KB buffers is sent whole", "[kfx_script][api_json_out]")
{
    for (size_t n : {2000u, 5000u, 70000u}) {
        Reply r(std::string(n, 'x'));
        bool ok = false;
        const std::string s = serialise(&r.root, 1024 * 1024, &ok);
        CHECK(ok);
        CHECK(s.size() > n);
        CHECK(s.find(std::string(n, 'x')) != std::string::npos);
        CHECK(s.back() == '\n');
    }
}

TEST_CASE("api_json_serialise: a reply longer than the limit gives NULL", "[kfx_script][api_json_out]")
{
    Reply r(std::string(2 * 1024 * 1024, 'y'));
    bool ok = true;
    serialise(&r.root, 1024 * 1024, &ok);
    CHECK_FALSE(ok);
}

TEST_CASE("api_json_serialise: the limit counts the newline", "[kfx_script][api_json_out]")
{
    Reply r("z");
    const std::string whole = serialise(&r.root, 1024 * 1024);
    bool ok = false;
    CHECK(serialise(&r.root, whole.size(), &ok) == whole);
    CHECK(ok);
    serialise(&r.root, whole.size() - 1, &ok);
    CHECK_FALSE(ok);
}
