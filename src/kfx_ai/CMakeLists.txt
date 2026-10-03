# Refactor pass 2, S14 (docs/refactor-pass2/stage-14-ai-library-spike.md):
# the computer-player AI, a client of the simulation, ranked directly above
# kfx_sim. Its state stays in kfx_sim_state (player_computer_types.h); kfx_sim
# reaches the AI's behaviour only through kfx_config's AiPort. Globs
# src/kfx_ai/src/ like every other kfx_* library.

file(GLOB KFX_AI_SOURCES_C "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")
file(GLOB KFX_AI_SOURCES_CXX "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
set(KFX_AI_SOURCES ${KFX_AI_SOURCES_C} ${KFX_AI_SOURCES_CXX})

add_library(kfx_ai STATIC ${KFX_AI_SOURCES})
target_link_libraries(kfx_ai PRIVATE kfx_common_opts kfx_log_opts)
target_include_directories(kfx_ai PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

list(APPEND KFX_OBJECT_LIBS kfx_ai)
set(KFX_OBJECT_LIBS ${KFX_OBJECT_LIBS} PARENT_SCOPE)

if(KFX_BUILD_TESTS)
    add_subdirectory(tests)
endif()
