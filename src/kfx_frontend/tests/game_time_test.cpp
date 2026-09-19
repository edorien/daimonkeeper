// kfx_frontend: front_input.c's in-game clock helpers behind the `-timer game [real]` display.
//  * get_game_time(): turns at a given fps -> H:M:S (pure).
//  * update_game_time(): the "real" clock ticks whole seconds itself (upstream #5313/#5322) --
//    it is advanced once per second of *game* time by the gameplay loop (scaled by frame skip)
//    instead of being derived from the turn count, so it stays correct under frame skipping.
#include <catch2/catch_test_macros.hpp>

#include "front_input.h"
#include "game_time.h"

TEST_CASE("get_game_time converts turns at a given fps into hours/minutes/seconds", "[kfx_frontend][game_time]") {
    struct GameTime gt;
    get_game_time(&gt, 20UL * (3600 + 2 * 60 + 5), 20); // 1h 2m 5s at 20 turns/s
    CHECK(gt.Hours == 1);
    CHECK(gt.Minutes == 2);
    CHECK(gt.Seconds == 5);

    get_game_time(&gt, 19, 20); // under a second
    CHECK(gt.Hours == 0);
    CHECK(gt.Minutes == 0);
    CHECK(gt.Seconds == 0);
}

TEST_CASE("update_game_time advances the counter by one second and carries into minutes and hours", "[kfx_frontend][game_time]") {
    struct GameTime gt = {};
    unsigned long seconds = 0;

    update_game_time(&gt, &seconds);
    CHECK(seconds == 1);
    CHECK(gt.Seconds == 1);
    CHECK(gt.Minutes == 0);

    seconds = 59;
    update_game_time(&gt, &seconds);
    CHECK(seconds == 60);
    CHECK(gt.Seconds == 0);
    CHECK(gt.Minutes == 1);

    seconds = 3599;
    update_game_time(&gt, &seconds);
    CHECK(seconds == 3600);
    CHECK(gt.Seconds == 0);
    CHECK(gt.Minutes == 0);
    CHECK(gt.Hours == 1);
}
