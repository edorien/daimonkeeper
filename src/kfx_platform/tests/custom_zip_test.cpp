// kfx_platform: custom_zip.c's read_map_zip_entry(). Only its early-
// return validation paths are attempted here (pattern B on
// FilePathPort, same shape as every other port fake in this
// plan): NULL out-parameter guards, and the prepare_map_zip_path
// callback returning NULL or a path to a file that doesn't exist. The
// actual zip-reading path (fastUnzConstructCache/fastUnzLocateFile/
// unzOpen/...) needs a real fixture .zip on disk, out of scope for this
// pass -- same "declare it, don't build the fixture" restraint as
// bflib_dernc_test.cpp's real-RNC-blob gap.
#include <catch2/catch_test_macros.hpp>

#include "custom_zip.h"
#include "ports/file_path_port.h"

namespace {
char *fake_path_returns_null(LevelNumber, const char *) { return nullptr; }

char g_missing_path[] = "kfx_platform_utest_custom_zip_definitely_missing.zip";
char *fake_path_returns_missing_file(LevelNumber, const char *) { return g_missing_path; }

struct MapZipPathFixture {
    struct FilePathPort port = file_path_port_defaults;
    explicit MapZipPathFixture(char *(*fn)(LevelNumber, const char *)) {
        port.prepare_map_zip_path = fn;
        set_file_path_port(&port);
    }
    ~MapZipPathFixture() { set_file_path_port(nullptr); }
};
}

TEST_CASE("read_map_zip_entry returns false when out_data is NULL", "[kfx_platform][custom_zip]") {
    size_t size;
    CHECK_FALSE(read_map_zip_entry(1, "entry.txt", nullptr, &size));
}

TEST_CASE("read_map_zip_entry returns false when out_size is NULL", "[kfx_platform][custom_zip]") {
    unsigned char *data;
    CHECK_FALSE(read_map_zip_entry(1, "entry.txt", &data, nullptr));
}

TEST_CASE("read_map_zip_entry returns false when entry_name is NULL", "[kfx_platform][custom_zip]") {
    unsigned char *data;
    size_t size;
    CHECK_FALSE(read_map_zip_entry(1, nullptr, &data, &size));
}

TEST_CASE("read_map_zip_entry returns false when the callback reports no zip path", "[kfx_platform][custom_zip]") {
    MapZipPathFixture fixture(fake_path_returns_null);
    unsigned char *data = nullptr;
    size_t size = 999;
    CHECK_FALSE(read_map_zip_entry(1, "entry.txt", &data, &size));
    CHECK(data == nullptr);
    CHECK(size == 0);
}

TEST_CASE("read_map_zip_entry returns false when the resolved zip file doesn't exist on disk", "[kfx_platform][custom_zip]") {
    MapZipPathFixture fixture(fake_path_returns_missing_file);
    unsigned char *data = nullptr;
    size_t size = 999;
    CHECK_FALSE(read_map_zip_entry(1, "entry.txt", &data, &size));
    CHECK(data == nullptr);
    CHECK(size == 0);
}
