// kfx_game: save-file layout versions (refactor pass 2, S09,
// docs/refactor-pass2/stage-09-versioned-state-chunks.md).
//
// A save written by this build must validate; one whose state chunk has
// another version or size, or that's cut short, must be refused -- and
// load_game_chunks() must refuse it before changing anything: every state
// struct it would copy into is hashed before and after.
//
// The save is written by the real save_game_chunks(), with the frontend
// state (a kfx_frontend struct, reached through UiPort) faked as the
// right number of zero bytes. The file is large (the state structs are
// ~150 MB), so it's written once per test into the working directory and
// removed afterwards.
#include <catch2/catch_test_macros.hpp>

#include "game_saves.h"
#include "game_legacy.h"
#include "game_merge.h"
#include "state_versions.h"
#include "bflib_fileio.h"
#include "kfx_sim_state.h"
#include "kfx_net_state.h"
#include "kfx_game_state.h"
#include "kfx_config/tests/scoped_port_override.h"
#include "ports/ui_port.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <unistd.h>

namespace {
// One file per process: ctest runs each test case as its own process, in
// parallel, so a shared name lets one case overwrite another's save.
const std::string kSavePathStr = "kfx_save_versions_test_" + std::to_string(getpid()) + ".sav";
const char *const kSavePath = kSavePathStr.c_str();

TbBool fake_save_frontend_state(TbFileHandle fh)
{
    std::vector<char> zeros(KFX_FRONTEND_STATE_SIZE, 0);
    return LbFileWrite(fh, zeros.data(), zeros.size()) == (int64_t)zeros.size();
}
size_t fake_frontend_state_size(void) { return KFX_FRONTEND_STATE_SIZE; }

uint64_t fnv1a(const void *data, size_t len, uint64_t h = 1469598103934665603ULL)
{
    const unsigned char *p = static_cast<const unsigned char *>(data);
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

uint64_t live_state_hash(void)
{
    uint64_t h = fnv1a(&game, sizeof(game));
    h = fnv1a(&kfx_sim_state, sizeof(kfx_sim_state), h);
    h = fnv1a(&kfx_net_state, sizeof(kfx_net_state), h);
    h = fnv1a(&kfx_game_state, sizeof(kfx_game_state), h);
    h = fnv1a(&intralvl, sizeof(intralvl), h);
    return h;
}

struct SaveFixture {
    ScopedPortOverride<UiPort> port{ui_port, set_ui_port};
    struct CatalogueEntry centry;
    SaveFixture() {
        port->save_frontend_state = &fake_save_frontend_state;
        port->get_frontend_state_size = &fake_frontend_state_size;
        std::memset(&centry, 0, sizeof(centry));
        std::snprintf(centry.textname, sizeof(centry.textname), "%s", "versions test");
        TbFileHandle fh = LbFileOpen(kSavePath, Lb_FILE_MODE_NEW);
        REQUIRE(fh);
        REQUIRE(save_game_chunks(fh, &centry));
        LbFileClose(fh);
    }
    ~SaveFixture() { std::remove(kSavePath); }

    // Offset of the header of the first chunk with this ID.
    static int64_t chunk_offset(uint64_t id) {
        std::FILE *f = std::fopen(kSavePath, "rb");
        int64_t pos = 0;
        struct FileChunkHeader hdr;
        while (f && std::fread(&hdr, sizeof(hdr), 1, f) == 1) {
            if (hdr.id == id) {
                std::fclose(f);
                return pos;
            }
            pos += sizeof(hdr) + hdr.len;
            std::fseek(f, (long)pos, SEEK_SET);
        }
        if (f) std::fclose(f);
        return -1;
    }
    static void patch_header(uint64_t id, uint64_t FileChunkHeader::*field, uint64_t value) {
        int64_t pos = chunk_offset(id);
        REQUIRE(pos >= 0);
        std::FILE *f = std::fopen(kSavePath, "r+b");
        REQUIRE(f);
        struct FileChunkHeader hdr;
        std::fseek(f, (long)pos, SEEK_SET);
        REQUIRE(std::fread(&hdr, sizeof(hdr), 1, f) == 1);
        hdr.*field = value;
        std::fseek(f, (long)pos, SEEK_SET);
        REQUIRE(std::fwrite(&hdr, sizeof(hdr), 1, f) == 1);
        std::fclose(f);
    }
    static TbBool validate(void) {
        TbFileHandle fh = LbFileOpen(kSavePath, Lb_FILE_MODE_READ_ONLY);
        REQUIRE(fh);
        TbBool ok = validate_save_chunks(fh);
        LbFileClose(fh);
        return ok;
    }
};
}

TEST_CASE_METHOD(SaveFixture, "a save written by this build validates", "[kfx_game][save_versions]") {
    CHECK(validate());
    CHECK(std::string(last_save_refusal_reason()).empty());
}

TEST_CASE_METHOD(SaveFixture, "a state chunk with another version is refused", "[kfx_game][save_versions]") {
    patch_header(SGC_KfxSimState, &FileChunkHeader::ver, KFX_SIM_STATE_VER + 1);
    CHECK_FALSE(validate());
    CHECK(std::string(last_save_refusal_reason()).find("version") != std::string::npos);
}

TEST_CASE_METHOD(SaveFixture, "an unversioned (pre-S09, version 0) save is refused", "[kfx_game][save_versions]") {
    patch_header(SGC_KfxGameState, &FileChunkHeader::ver, 0);
    CHECK_FALSE(validate());
}

TEST_CASE_METHOD(SaveFixture, "a state chunk of another size is refused", "[kfx_game][save_versions]") {
    // Shrinking the stated length keeps the file walkable, so the size
    // check itself is what refuses it.
    patch_header(SGC_IntralevelData, &FileChunkHeader::len, KFX_INTRALEVEL_SIZE - 1);
    CHECK_FALSE(validate());
}

TEST_CASE_METHOD(SaveFixture, "a save cut short is refused", "[kfx_game][save_versions]") {
    std::FILE *f = std::fopen(kSavePath, "rb");
    REQUIRE(f);
    std::fseek(f, 0, SEEK_END);
    const long len = std::ftell(f);
    std::fclose(f);
    // Keep only the first half of the file.
    std::vector<char> head((size_t)len / 2);
    f = std::fopen(kSavePath, "rb");
    REQUIRE(std::fread(head.data(), 1, head.size(), f) == head.size());
    std::fclose(f);
    f = std::fopen(kSavePath, "wb");
    REQUIRE(std::fwrite(head.data(), 1, head.size(), f) == head.size());
    std::fclose(f);
    CHECK_FALSE(validate());
}

TEST_CASE_METHOD(SaveFixture, "load_game_chunks refuses a mismatched save without touching any live state", "[kfx_game][save_versions]") {
    patch_header(SGC_KfxNetState, &FileChunkHeader::ver, KFX_NET_STATE_VER + 7);
    // Rename the info chunk to an ID the loader skips: it would otherwise
    // switch campaign first, which fails in this test binary and ends the
    // load before any state chunk. This way a one-pass loader would copy the
    // GameOrig and KfxSimState chunks before reaching the bad KfxNetState
    // one -- exactly the partial overwrite the pre-pass exists to prevent.
    patch_header(SGC_InfoBlock, &FileChunkHeader::id, 0x58585858); // "XXXX"
    // Make the live state differ from what's in the file, so a partial load
    // would show up in the hash.
    kfx_sim_state.loaded_level_number = 12345;
    kfx_sim_state.play_gameturn = 678;
    const uint64_t before = live_state_hash();

    TbFileHandle fh = LbFileOpen(kSavePath, Lb_FILE_MODE_READ_ONLY);
    REQUIRE(fh);
    struct CatalogueEntry loaded;
    std::memset(&loaded, 0, sizeof(loaded));
    CHECK(load_game_chunks(fh, &loaded) == GLoad_Failed);
    LbFileClose(fh);

    CHECK(last_save_was_refused());
    CHECK(live_state_hash() == before);
    CHECK(kfx_sim_state.loaded_level_number == 12345);
}
