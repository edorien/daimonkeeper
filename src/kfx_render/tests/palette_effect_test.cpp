// PaletteEffect (lens palette): a lens palette becomes the main palette that possession/pain
// fades work from. With a fade running it must NOT switch at once (that would reset the fade,
// upstream #5302) -- only local_state.main_palette is updated and the fade brings it in.
#include <catch2/catch_test_macros.hpp>

#include "PaletteEffect.h"
#include "config_lenses.h"
#include "player_data.h"
#include "vidmode.h"
#include "kfx_sim_state.h"

#include <cstring>

TEST_CASE("PaletteEffect keeps a running possession fade going when the lens palette is applied", "[kfx_render][palette_effect]") {
    std::memset(&local_state, 0, sizeof(local_state));
    std::memset(&lenses_conf, 0, sizeof(lenses_conf));
    local_state.palette_fade_step_possession = 5;

    PaletteEffect effect;
    CHECK(effect.Setup(1));
    CHECK(local_state.lens_palette == lenses_conf.lenses[1].palette);
    CHECK(local_state.main_palette == lenses_conf.lenses[1].palette);
    CHECK(local_state.palette_fade_step_possession == 5); // fade untouched

    effect.Cleanup();
    CHECK(local_state.lens_palette == nullptr);
    CHECK(local_state.main_palette == engine_palette);
    CHECK(local_state.palette_fade_step_possession == 5);
}
