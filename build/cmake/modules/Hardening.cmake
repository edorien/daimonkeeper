# Hardening.cmake - exploit-mitigation flags, set explicitly rather than left to the distro's compiler defaults
# (security pass S09). Ubuntu's gcc turns most of these on by itself; Arch, Fedora, clang and mingw-w64 builds
# differ, and a flag that is only "usually on" is not something to rely on.
#
# KFX_HARDEN (default ON) covers the game's own code: it goes on kfx_common_opts (every kfx_* library and the
# keeperfx sources) and on the keeperfx link. Fetched dependencies keep their own build flags.
#
# Linux:   stack protector (strong), stack-clash probes, CET (endbr/shadow-stack marking), _FORTIFY_SOURCE=3,
#          libstdc++ assertions, PIE, full RELRO (BIND_NOW), non-executable stack.
# Windows: stack protector (statically linked libssp), _FORTIFY_SOURCE=2, libstdc++ assertions, and the PE flags
#          ASLR (dynamicbase), DEP (nxcompat) and 64-bit high-entropy ASLR set explicitly.
#
# scripts/check_hardening.py reads the finished binary and fails when any of this is missing; CI runs it.
#
# Switched off by the sanitizer build (scripts/build-fuzz.sh: ASan replaces the fortify checks and the two disagree
# at -O1) and, for _FORTIFY_SOURCE only, by the -O0 coverage build (fortify needs optimisation).

option(KFX_HARDEN "Build the game with exploit-mitigation flags (stack protector, fortify, PIE/RELRO or ASLR/DEP); see build/cmake/modules/Hardening.cmake" ON)

# Compile flags, added to the INTERFACE library every kfx_* library and keeperfx compile against.
function(kfx_harden_compile TARGET)
    if(NOT KFX_HARDEN)
        return()
    endif()
    if(WIN32)
        target_compile_options(${TARGET} INTERFACE -fstack-protector-strong)
        target_compile_definitions(${TARGET} INTERFACE _GLIBCXX_ASSERTIONS)
        if(NOT KFX_TEST_COVERAGE)
            target_compile_options(${TARGET} INTERFACE -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2)
        endif()
    elseif(UNIX AND NOT APPLE)
        target_compile_options(${TARGET} INTERFACE
            -fstack-protector-strong -fstack-clash-protection -fcf-protection=full -fPIE)
        target_compile_definitions(${TARGET} INTERFACE _GLIBCXX_ASSERTIONS)
        if(NOT KFX_TEST_COVERAGE)
            # -U first: a distro gcc (Ubuntu) defines it already, and redefining would warn -- an error under -Werror.
            target_compile_options(${TARGET} INTERFACE -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3)
        endif()
    endif()
endfunction()

# Link flags for the game executable.
function(kfx_harden_link TARGET)
    if(NOT KFX_HARDEN)
        return()
    endif()
    if(WIN32)
        target_link_options(${TARGET} PRIVATE
            -Wl,--dynamicbase -Wl,--nxcompat -Wl,--high-entropy-va)
    elseif(UNIX AND NOT APPLE)
        target_link_options(${TARGET} PRIVATE
            -pie -Wl,-z,relro,-z,now,-z,noexecstack)
    endif()
endfunction()
