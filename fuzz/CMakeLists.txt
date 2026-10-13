# Fuzz harnesses (security pass S08). Included from the root CMakeLists.txt when KFX_BUILD_FUZZ is on.
#
# One executable, kfx_fuzz: fuzz_driver.c (the fuzzer) plus a harness_*.c(pp) per family of parsers, each registering
# its targets with FUZZ_TARGET(). It links what the game does (everything but main()) so a target can call any loader
# the way the game would.
#
# gcc has no libFuzzer; -fsanitize-coverage=trace-pc on the kfx_* libraries (not the dependencies) is the feedback the
# driver uses (it defines __sanitizer_cov_trace_pc).
file(GLOB KFX_FUZZ_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.c" "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_executable(kfx_fuzz ${KFX_FUZZ_SOURCES})
target_compile_definitions(kfx_fuzz PRIVATE KFX_FUZZ_REPO_ROOT="${CMAKE_SOURCE_DIR}")
target_link_libraries(kfx_fuzz PRIVATE ${KFX_STATIC_LIBS} kfx_log_opts kfx_common_opts centitoml tinyfiledialogs imgui)
kfx_link_dependencies(kfx_fuzz)
target_link_libraries(kfx_fuzz PRIVATE m dl)
# backtrace_symbols_fd() in the timeout handler names functions only from the dynamic symbol table
target_link_options(kfx_fuzz PRIVATE -rdynamic)

foreach(lib ${KFX_OBJECT_LIBS})
    target_compile_options(${lib} PRIVATE -fsanitize-coverage=trace-pc)
endforeach()
