// Tripwire against a regression of the bug documented in
// docs/refactor/renderer/02c-post-migration-audit-and-refactor-opportunities.md
// §1.2: colored_stripey_lines[]'s stripey_line_color_array is TbPixel[16],
// but was initialized from 16 bare palette-index scalars -- valid while
// TbPixel was one byte, silently wrong (C++ brace elision only fills the
// first ~4 of 16 slots, the rest staying zero-initialized/transparent
// black) once TbPixel widened to a 4-byte struct. -Wmissing-braces didn't
// catch this nested-inside-a-struct-initializer case (it did catch the
// flatter net_player_colours[] instance elsewhere), so this test exists as
// the compiler-independent check.
#include <catch2/catch_test_macros.hpp>

#include "engine_render.h"

TEST_CASE("colored_stripey_lines[] has a real opaque colour in every one of its 16 animation slots",
          "[kfx_render][engine_render_data]") {
    for (int64_t line = 0; line < STRIPEY_LINE_COLOR_COUNT; line++) {
        for (int64_t slot = 0; slot < 16; slot++) {
            const TbPixel px = colored_stripey_lines[line].stripey_line_color_array[slot];
            INFO("colored_stripey_lines[" << line << "].stripey_line_color_array[" << slot << "]");
            // A real colour is always fully opaque (alpha 255); a slot left
            // at its zero-initialized default (the brace-elision bug) has
            // alpha 0 -- this is the one bit that distinguishes "never
            // written" from "genuinely opaque black", which a couple of
            // these tables' darkest fade-out slots legitimately are.
            CHECK(px.a == 255);
        }
    }
}
