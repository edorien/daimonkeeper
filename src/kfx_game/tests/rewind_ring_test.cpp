// kfx_game: the replay rewind's storage (rewind_ring.h, upstream #5450 re-implemented): the record ring's
// allocation, eviction of the oldest records and wrapping at the end of the buffer, and the XOR page diffs walked
// both ways along a chain of snapshots.
#include <catch2/catch_test_macros.hpp>

#include "rewind_ring.h"

#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

namespace {

struct Ring {
    RewindRing r;
    explicit Ring(size_t cap) { REQUIRE(rewind_ring_init(&r, cap)); }
    ~Ring() { rewind_ring_free(&r); }
};

// Fills a pushed record with its own number, to see it come back intact.
unsigned char *push_tagged(RewindRing *r, size_t len, unsigned char tag)
{
    unsigned char *p = rewind_ring_push(r, len);
    if (p != nullptr)
        std::memset(p, tag, len);
    return p;
}

bool record_is(const RewindRing *r, size_t i, size_t len, unsigned char tag)
{
    size_t got = 0;
    const unsigned char *p = rewind_ring_record(r, i, &got);
    if ((p == nullptr) || (got != len))
        return false;
    for (size_t k = 0; k < len; k++)
        if (p[k] != tag)
            return false;
    return true;
}

// No two records may overlap, and each must lie inside the buffer.
bool records_disjoint(const RewindRing *r)
{
    for (size_t i = 0; i < r->count; i++) {
        size_t li = 0;
        const unsigned char *pi = rewind_ring_record(r, i, &li);
        if ((pi < r->buf) || (pi + li > r->buf + r->cap))
            return false;
        for (size_t j = i + 1; j < r->count; j++) {
            size_t lj = 0;
            const unsigned char *pj = rewind_ring_record(r, j, &lj);
            if ((pi < pj + lj) && (pj < pi + li))
                return false;
        }
    }
    return true;
}

} // namespace

TEST_CASE("rewind ring: records come back oldest to newest, aligned", "[kfx_game][rewind_ring]") {
    Ring ring(1000);
    REQUIRE(push_tagged(&ring.r, 100, 1) != nullptr);
    REQUIRE(push_tagged(&ring.r, 13, 2) != nullptr);
    REQUIRE(push_tagged(&ring.r, 200, 3) != nullptr);
    REQUIRE(ring.r.count == 3);
    CHECK(record_is(&ring.r, 0, 100, 1));
    CHECK(record_is(&ring.r, 1, 13, 2));
    CHECK(record_is(&ring.r, 2, 200, 3));
    for (size_t i = 0; i < ring.r.count; i++)
        CHECK(((uintptr_t)rewind_ring_record(&ring.r, i, nullptr) - (uintptr_t)ring.r.buf) % REWIND_RING_ALIGN == 0);
    CHECK(ring.r.used == 104 + 16 + 200);
    CHECK(rewind_ring_record(&ring.r, 3, nullptr) == nullptr);
}

TEST_CASE("rewind ring: a full ring evicts its oldest records, never more than it needs", "[kfx_game][rewind_ring]") {
    Ring ring(1000);
    for (unsigned char t = 1; t <= 4; t++)
        REQUIRE(push_tagged(&ring.r, 240, t) != nullptr); // 960 bytes
    REQUIRE(ring.r.count == 4);
    // 240 more don't fit after the head (40 left) nor before the tail (0): the oldest goes, and the record wraps to 0
    REQUIRE(push_tagged(&ring.r, 240, 5) != nullptr);
    CHECK(ring.r.count == 4);
    CHECK(record_is(&ring.r, 0, 240, 2));
    CHECK(record_is(&ring.r, 3, 240, 5));
    CHECK(rewind_ring_record(&ring.r, 3, nullptr) == ring.r.buf);
    CHECK(records_disjoint(&ring.r));
    // wrapped: the only room is between the newest and the oldest, so each new record evicts one more
    REQUIRE(push_tagged(&ring.r, 240, 6) != nullptr);
    CHECK(ring.r.count == 4);
    CHECK(record_is(&ring.r, 0, 240, 3));
    CHECK(record_is(&ring.r, 3, 240, 6));
    CHECK(records_disjoint(&ring.r));
    // a big record evicts as many as it takes
    REQUIRE(push_tagged(&ring.r, 700, 7) != nullptr);
    CHECK(records_disjoint(&ring.r));
    CHECK(record_is(&ring.r, ring.r.count - 1, 700, 7));
    CHECK(ring.r.count <= 2);
}

TEST_CASE("rewind ring: a record larger than the ring is refused without evicting anything", "[kfx_game][rewind_ring]") {
    Ring ring(1000);
    REQUIRE(push_tagged(&ring.r, 300, 1) != nullptr);
    CHECK(rewind_ring_push(&ring.r, 1001) == nullptr);
    CHECK(rewind_ring_push(&ring.r, 0) == nullptr);
    CHECK(ring.r.count == 1);
    CHECK(record_is(&ring.r, 0, 300, 1));
    // the whole ring is fine: everything else goes
    REQUIRE(push_tagged(&ring.r, 1000, 2) != nullptr);
    CHECK(ring.r.count == 1);
    CHECK(record_is(&ring.r, 0, 1000, 2));
}

TEST_CASE("rewind ring: dropping and clearing", "[kfx_game][rewind_ring]") {
    Ring ring(1000);
    REQUIRE(push_tagged(&ring.r, 10, 1) != nullptr);
    REQUIRE(push_tagged(&ring.r, 10, 2) != nullptr);
    rewind_ring_drop_oldest(&ring.r);
    CHECK(ring.r.count == 1);
    CHECK(record_is(&ring.r, 0, 10, 2));
    CHECK(ring.r.used == 16);
    rewind_ring_clear(&ring.r);
    CHECK(ring.r.count == 0);
    CHECK(ring.r.used == 0);
    rewind_ring_drop_oldest(&ring.r); // nothing to drop: no harm
    REQUIRE(push_tagged(&ring.r, 10, 3) != nullptr);
    CHECK(rewind_ring_record(&ring.r, 0, nullptr) == ring.r.buf);
}

TEST_CASE("rewind ring: thousands of mixed records keep the newest intact and never overlap", "[kfx_game][rewind_ring]") {
    Ring ring(64 * 1024);
    std::mt19937 rng(5450);
    std::vector<std::pair<size_t, unsigned char>> pushed;
    for (int n = 0; n < 5000; n++) {
        const size_t len = 1 + rng() % 9000;
        const unsigned char tag = (unsigned char)(n & 0xFF);
        REQUIRE(push_tagged(&ring.r, len, tag) != nullptr);
        pushed.emplace_back(len, tag);
        REQUIRE(ring.r.count >= 1);
        REQUIRE(ring.r.count <= pushed.size());
        // the records kept are the newest ones pushed, in order
        const size_t first = pushed.size() - ring.r.count;
        for (size_t i = 0; i < ring.r.count; i++)
            REQUIRE(record_is(&ring.r, i, pushed[first + i].first, pushed[first + i].second));
        REQUIRE(records_disjoint(&ring.r));
        size_t used = 0;
        for (size_t i = 0; i < ring.r.count; i++)
            used += (pushed[first + i].first + REWIND_RING_ALIGN - 1) / REWIND_RING_ALIGN * REWIND_RING_ALIGN;
        REQUIRE(ring.r.used == used);
        REQUIRE(ring.r.used <= ring.r.cap);
    }
}

namespace {

std::vector<unsigned char> encode(std::vector<unsigned char> &snap, const std::vector<unsigned char> &live, size_t len, size_t off)
{
    RewindBuf buf = {};
    REQUIRE(rewind_diff_encode(&buf, snap.data() + off, live.data(), len, off));
    std::vector<unsigned char> out(buf.data, buf.data + buf.len);
    rewind_buf_free(&buf);
    return out;
}

} // namespace

TEST_CASE("rewind diff: equal ranges give an empty diff", "[kfx_game][rewind_ring]") {
    std::vector<unsigned char> snap(3 * REWIND_PAGE_SIZE, 7);
    std::vector<unsigned char> live = snap;
    CHECK(encode(snap, live, snap.size(), 0).empty());
}

TEST_CASE("rewind diff: one changed byte costs one page header and two masks", "[kfx_game][rewind_ring]") {
    std::vector<unsigned char> snap(4 * REWIND_PAGE_SIZE, 0);
    std::vector<unsigned char> live = snap;
    live[2 * REWIND_PAGE_SIZE + 130] = 0x5A;
    const std::vector<unsigned char> old = snap;
    const std::vector<unsigned char> diff = encode(snap, live, snap.size(), 0);
    CHECK(diff.size() == 4 + 8 + 1 + 1 + 1);
    CHECK(snap == live); // the snapshot now holds the live bytes
    REQUIRE(rewind_diff_apply(snap.data(), snap.size(), diff.data(), diff.size()));
    CHECK(snap == old);
}

TEST_CASE("rewind diff: round trip both ways, a piece at an offset with a partial last page", "[kfx_game][rewind_ring]") {
    std::mt19937 rng(5470);
    const size_t off = 3 * REWIND_PAGE_SIZE;
    const size_t len = 5 * REWIND_PAGE_SIZE + 1234; // the piece's last page is partial: zero past its end
    std::vector<unsigned char> snap(off + rewind_page_round(len), 0);
    for (size_t i = 0; i < len; i++)
        snap[off + i] = (unsigned char)rng();
    std::vector<unsigned char> live(snap.begin() + off, snap.begin() + off + len);
    // sparse changes, a run of changes, and the piece's very last byte
    for (int k = 0; k < 50; k++)
        live[rng() % len] ^= (unsigned char)(1 + rng() % 255);
    for (size_t i = 1000; i < 1500; i++)
        live[i] = (unsigned char)rng();
    live[len - 1] ^= 0xFF;
    const std::vector<unsigned char> before = snap;
    const std::vector<unsigned char> diff = encode(snap, live, len, off);
    REQUIRE(!diff.empty());
    CHECK(std::memcmp(snap.data() + off, live.data(), len) == 0);
    for (size_t i = off + len; i < snap.size(); i++)
        REQUIRE(snap[i] == 0); // the padding stays zero
    std::vector<unsigned char> after = snap;
    // XOR: applying the diff goes back, and applying it again comes forward
    REQUIRE(rewind_diff_apply(snap.data(), snap.size(), diff.data(), diff.size()));
    CHECK(snap == before);
    REQUIRE(rewind_diff_apply(snap.data(), snap.size(), diff.data(), diff.size()));
    CHECK(snap == after);
}

TEST_CASE("rewind diff: a chain of keyframes walks back and forth", "[kfx_game][rewind_ring]") {
    std::mt19937 rng(20);
    const size_t len = 16 * REWIND_PAGE_SIZE;
    std::vector<unsigned char> live(len);
    for (auto &b : live)
        b = (unsigned char)rng();
    std::vector<unsigned char> snap = live; // keyframe 0, taken whole
    std::vector<std::vector<unsigned char>> states = {live};
    std::vector<std::vector<unsigned char>> diffs = {{}};
    for (int kf = 1; kf < 12; kf++) {
        for (int k = 0; k < 200; k++)
            live[rng() % len] = (unsigned char)rng();
        diffs.push_back(encode(snap, live, len, 0));
        states.push_back(live);
    }
    // the snapshot is at the newest keyframe: back to each older one by the newer ones' diffs...
    for (int kf = 11; kf > 0; kf--) {
        REQUIRE(rewind_diff_apply(snap.data(), snap.size(), diffs[kf].data(), diffs[kf].size()));
        REQUIRE(snap == states[kf - 1]);
    }
    // ...and forward again by the same diffs
    for (int kf = 1; kf < 12; kf++) {
        REQUIRE(rewind_diff_apply(snap.data(), snap.size(), diffs[kf].data(), diffs[kf].size()));
        REQUIRE(snap == states[kf]);
    }
}

TEST_CASE("rewind diff: malformed diffs are refused", "[kfx_game][rewind_ring]") {
    std::vector<unsigned char> snap(2 * REWIND_PAGE_SIZE, 0);
    std::vector<unsigned char> live = snap;
    live[10] = 1;
    live[REWIND_PAGE_SIZE + 4000] = 2;
    std::vector<unsigned char> diff = encode(snap, live, snap.size(), 0);
    // cut short anywhere: refused (the bytes before the cut may have been applied)
    for (size_t cut = 1; cut < diff.size(); cut++) {
        std::vector<unsigned char> s = live;
        if (cut == 15)
            continue; // a whole first page: a valid diff on its own
        CHECK_FALSE(rewind_diff_apply(s.data(), s.size(), diff.data(), cut));
    }
    // a page past the snapshot's end
    std::vector<unsigned char> bad = diff;
    const uint32_t far_page = 2;
    std::memcpy(bad.data(), &far_page, sizeof(far_page));
    CHECK_FALSE(rewind_diff_apply(snap.data(), snap.size(), bad.data(), bad.size()));
}
