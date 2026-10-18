// process_command_line()'s string options: each copy into start_params
// must stay inside its destination buffer whatever the argument length.
#include <catch2/catch_test_macros.hpp>

#include "command_line.h"
#include "config_keeperfx.h"
#include "globals.h"

#include <cstring>
#include <string>

namespace {

struct CommandLineFixture {
    char saved_runtime_dir[sizeof(keeper_runtime_directory)];
    CommandLineFixture() { std::memcpy(saved_runtime_dir, keeper_runtime_directory, sizeof(saved_runtime_dir)); }
    ~CommandLineFixture() { std::memcpy(keeper_runtime_directory, saved_runtime_dir, sizeof(saved_runtime_dir)); }
};

int64_t run(std::initializer_list<std::string> args) {
    static std::string storage[16];
    static char *argv[16];
    int64_t argc = 0;
    for (const std::string &a : args) {
        storage[argc] = a;
        argv[argc] = storage[argc].data();
        argc++;
    }
    return process_command_line(argc, argv);
}

} // namespace

TEST_CASE_METHOD(CommandLineFixture, "-campaign truncates a long name to each destination buffer", "[kfx_apploop][command_line]") {
    // autostart_multiplayer_campaign is 80 bytes; this used to be an
    // unbounded strcpy of the (up to CMDLN_MAXLEN) argument.
    const std::string name(200, 'c');
    CHECK(run({"daimonkeeper", "-level", "1", "-campaign", name}) == 1);

    CHECK(std::string(start_params.selected_campaign) == name);
    CHECK(std::string(start_params.autostart_multiplayer_campaign) == name.substr(0, sizeof(start_params.autostart_multiplayer_campaign) - 1));
}

TEST_CASE_METHOD(CommandLineFixture, "-campaign as the last argument gives an empty name", "[kfx_apploop][command_line]") {
    CHECK(run({"daimonkeeper", "-level", "1", "-campaign"}) == 1);

    CHECK(start_params.selected_campaign[0] == '\0');
    CHECK(start_params.autostart_multiplayer_campaign[0] == '\0');
}
