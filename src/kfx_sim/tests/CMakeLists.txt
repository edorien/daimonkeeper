# docs/refactor/testing/stage-04c-kfx-sim.md: links the whole kfx_sim
# OBJECT library, same shape as kfx_pathfinding_utest -- explicit
# centitoml link for the same reason (kfx_config is transitively linked
# and calls toml_parse()).
#
# kfx_packet_test_stubs (get_packet/get_packet_direct/set_packet_action/
# set_players_packet_action) used to be defined here, needed by any
# *_utest that links kfx_sim but not kfx_net -- removed once those
# functions' real implementations moved down into kfx_sim itself
# (packet_data.c, docs/refactor/todo/remove-symbol-level-layering-residuals.md),
# so kfx_sim (always linked here) already provides them for real.
file(GLOB KFX_SIM_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_executable(kfx_sim_utest ${KFX_SIM_TEST_SOURCES})
target_link_libraries(kfx_sim_utest PRIVATE
    kfx_sim
    kfx_pathfinding
    kfx_config
    kfx_platform
    kfx_bfdebug_std
    kfx_common_opts
    centitoml
    tinyfiledialogs # same OBJECT-library explicit-link gap as centitoml --
                     # kfx_platform now links bflib_filedialogs.cpp, which
                     # needs tinyfiledialogs's compiled object.
    imgui # kfx_platform's ImGuiContext.cpp (and, transitively for some
          # *_utest targets, kfx_frontend's frontgui_*.cpp) reference imgui
          # symbols -- same two-levels-of-INTERFACE-indirection object-linking
          # gap as centitoml above.
    Catch2::Catch2WithMain)

include(CTest)
include(Catch)
catch_discover_tests(kfx_sim_utest TEST_PREFIX "kfx_sim.")
