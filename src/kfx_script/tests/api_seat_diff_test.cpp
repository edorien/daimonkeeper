// kfx_script: api_seat_diff.c -- the "what changed since your last view" protocol of get_player_view (AI/LLM M6).
// Pure JSON-tree logic, so it is tested on hand-built trees shaped like the real view.
#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <string>
#include <json.h>
#include <json-dom.h>

#include "api_seat_diff.h"

namespace {

// Canonical text: object keys sorted, so the comparison does not depend on the dict's internal order.
std::string dump(const VALUE *v);
int dump_member(const VALUE *k, VALUE *val, void *ctx) {
    std::string *s = static_cast<std::string *>(ctx);
    if (s->back() != '{') s->push_back(',');
    s->append("\"").append(value_string(k)).append("\":").append(dump(val));
    return 0;
}
std::string dump(const VALUE *v) {
    switch (value_type(v)) {
    case VALUE_BOOL: return value_bool(v) ? "true" : "false";
    case VALUE_INT32: case VALUE_UINT32: case VALUE_INT64: case VALUE_UINT64: return std::to_string(value_int64(v));
    case VALUE_STRING: return std::string("\"") + value_string(v) + "\"";
    case VALUE_ARRAY: {
        std::string s = "[";
        for (size_t i = 0; i < value_array_size(v); i++) { if (i) s += ","; s += dump(value_array_get(v, i)); }
        return s + "]";
    }
    case VALUE_DICT: {
        std::string s = "{";
        value_dict_walk_sorted(v, dump_member, &s);
        return s + "}";
    }
    default: return "null";
    }
}
std::string canon(const char *text);

VALUE parse(const char *text) {
    VALUE v;
    JSON_INPUT_POS pos;
    REQUIRE(json_dom_parse(text, strlen(text), nullptr, 0, &v, &pos) == 0);
    return v;
}

std::string canon(const char *text) {
    VALUE v = parse(text);
    const std::string s = dump(&v);
    value_fini(&v);
    return s;
}

// The canonical text of the diff of two documents.
std::string diff_of(const char *a, const char *b) {
    VALUE pa = parse(a), pb = parse(b), out;
    value_init_dict(&out);
    api_seat_diff_values(&out, &pa, &pb);
    const std::string s = dump(&out);
    value_fini(&pa); value_fini(&pb); value_fini(&out);
    return s;
}

} // namespace

TEST_CASE("identical trees have an empty diff", "[kfx_script][seat_diff]") {
    CHECK(diff_of(R"({"turn":5,"own":{"gold":10,"rooms":[{"id":1,"slabs":3}]}})", R"({"turn":5,"own":{"gold":10,"rooms":[{"id":1,"slabs":3}]}})") == "{}");
}

TEST_CASE("a changed scalar shows under its path, nested dicts only where something changed", "[kfx_script][seat_diff]") {
    CHECK(diff_of(R"({"turn":5,"own":{"gold":10,"x":1}})", R"({"turn":6,"own":{"gold":10,"x":2}})") == canon(R"({"own":{"x":2},"turn":6})"));
}

TEST_CASE("a vanished key is listed, a new key is carried whole", "[kfx_script][seat_diff]") {
    CHECK(diff_of(R"({"a":1,"b":2})", R"({"a":1,"c":{"d":1}})") == canon(R"({"_removed":["b"],"c":{"d":1}})"));
}

TEST_CASE("arrays of objects with ids: added, removed by id, changed fields only", "[kfx_script][seat_diff]") {
    const char *a = R"({"cs":[{"id":1,"hp":10,"pos":[1,1]},{"id":2,"hp":5,"pos":[2,2]},{"id":3,"hp":7,"pos":[3,3]}]})";
    const char *b = R"({"cs":[{"id":1,"hp":9,"pos":[1,1]},{"id":3,"hp":7,"pos":[3,4]},{"id":4,"hp":1,"pos":[9,9]}]})";
    const std::string d = diff_of(a, b);
    CHECK(d.find(R"("added":[{"hp":1,"id":4,"pos":[9,9]}])") != std::string::npos);
    CHECK(d.find(R"("removed":[2])") != std::string::npos);
    CHECK(d.find(R"({"hp":9,"id":1})") != std::string::npos);
    CHECK(d.find(R"({"id":3,"pos":[3,4]})") != std::string::npos);
    CHECK(d.find(R"("hp":10)") == std::string::npos); // unchanged ids are absent
}

TEST_CASE("arrays without ids are matched by content", "[kfx_script][seat_diff]") {
    const std::string d = diff_of(R"({"marks":[[1,1],[2,2],[3,3]],"powers":["A","B"]})", R"({"marks":[[2,2],[3,3],[4,4]],"powers":["A","B","C"]})");
    CHECK(d == canon(R"({"marks":{"added":[[4,4]],"removed":[[1,1]]},"powers":{"added":["C"]}})"));
}

TEST_CASE("the map's rows become runs of changed cells and runs of newly revealed slabs", "[kfx_script][seat_diff]") {
    // 4 x 3 slabs, two characters each.
    const char *a = R"({"map":{"width":4,"rows":["........","..a1a1..","........"]}})";
    const char *b = R"({"map":{"width":4,"rows":["....b0..","..a1c1d1","........"]}})";
    const std::string d = diff_of(a, b);
    // row 0: slab 2 was hidden and is now revealed and changed; row 1: slab 2 changed (a->c), slab 3 hidden->revealed.
    CHECK(d.find(R"("changes":[{"cells":"b0","x":2,"y":0},{"cells":"c1d1","x":2,"y":1}])") != std::string::npos);
    CHECK(d.find(R"("revealed":[{"len":1,"x":2,"y":0},{"len":1,"x":3,"y":1}])") != std::string::npos);
    CHECK(d.find("rows") == std::string::npos);
}

TEST_CASE("a map that changed size is carried whole", "[kfx_script][seat_diff]") {
    const std::string d = diff_of(R"({"map":{"rows":["....","...."]}})", R"({"map":{"rows":["......","......"]}})");
    CHECK(d.find(R"("rows")") != std::string::npos);
}

TEST_CASE("finish_view: full first, then a diff against the previous view, resync on a mismatch", "[kfx_script][seat_diff]") {
    api_seat_diff_reset();
    auto make = [](int turn, int gold) {
        VALUE v; value_init_dict(&v);
        value_init_int64(value_dict_add(&v, "turn"), turn);
        VALUE *own = value_dict_add(&v, "own"); value_init_dict(own);
        value_init_int64(value_dict_add(own, "gold"), gold);
        value_init_int64(value_dict_add(own, "stable"), 7);
        return v;
    };
    VALUE v1 = make(10, 100);
    api_seat_finish_view(&v1, 2, true, 0);
    CHECK(std::string(value_string(value_dict_get(&v1, "mode"))) == "full");
    CHECK(std::string(value_string(value_dict_get(&v1, "reason"))) == "NO_BASE");
    const int64_t id1 = value_int64(value_dict_get(&v1, "view_id"));

    VALUE v2 = make(20, 90);
    api_seat_finish_view(&v2, 2, true, id1);
    CHECK(std::string(value_string(value_dict_get(&v2, "mode"))) == "diff");
    CHECK(value_int64(value_dict_get(&v2, "base")) == id1);
    CHECK(value_int64(value_dict_get(&v2, "turn")) == 20);
    CHECK(value_int64(value_dict_get(value_dict_get(&v2, "own"), "gold")) == 90);
    CHECK(value_dict_get(value_dict_get(&v2, "own"), "stable") == nullptr);
    const int64_t id2 = value_int64(value_dict_get(&v2, "view_id"));
    CHECK(id2 > id1);

    VALUE v3 = make(30, 90);
    api_seat_finish_view(&v3, 2, true, id1); // asks for a diff against a view that is no longer the baseline
    CHECK(std::string(value_string(value_dict_get(&v3, "mode"))) == "full");
    CHECK(std::string(value_string(value_dict_get(&v3, "reason"))) == "BASE_MISMATCH");
    CHECK(value_dict_get(&v3, "own") != nullptr);

    VALUE v4 = make(5, 90); // time went backwards (a load): full
    api_seat_finish_view(&v4, 2, true, value_int64(value_dict_get(&v3, "view_id")));
    CHECK(std::string(value_string(value_dict_get(&v4, "reason"))) == "TURN_WENT_BACKWARDS");

    VALUE v5 = make(40, 90); // no `since`: full, no reason
    api_seat_finish_view(&v5, 2, false, 0);
    CHECK(std::string(value_string(value_dict_get(&v5, "mode"))) == "full");
    CHECK(value_dict_get(&v5, "reason") == nullptr);
    for (VALUE *v : { &v1, &v2, &v3, &v4, &v5 }) value_fini(v);
    api_seat_diff_reset();
}
