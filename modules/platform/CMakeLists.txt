# CNA platform module (plans/plan_platform.md PLAT-10): the CNA-owned contract that separates the
# framework from SDL3, plus the implementations of that contract.
#
# This module is the ONE place a platform library may be linked. Every other module talks to
# CNA::Platform's interfaces and never includes an SDL header -- enforced by the ratchet gate
# (cmake/PlatformRatchet.cmake, PLAT-8) and, from PLAT-121, as a hard error.
#
# src/ is flat for the contract's own types; each implementation owns a subdirectory
# (src/Sdl3/, src/Headless/, src/Terminal/), with src/Common/ holding the pieces more than one
# of them shares.
file(GLOB CNA_PLATFORM_SOURCES CONFIGURE_DEPENDS "src/*.cpp")

# The selected implementation, chosen by CNA_PLATFORM (cmake/PlatformSelection.cmake).
if(CNA_PLATFORM STREQUAL "SDL3")
    file(GLOB CNA_PLATFORM_IMPL_SOURCES CONFIGURE_DEPENDS "src/Sdl3/*.cpp")
    list(APPEND CNA_PLATFORM_SOURCES ${CNA_PLATFORM_IMPL_SOURCES})
    # The one Objective-C++ translation unit: asking UIKit to re-read the orientation set has no
    # C API. Listed explicitly rather than globbed, so it cannot be picked up on a target whose
    # compiler would reject it.
    if(CNA_APPLE_IOS)
        list(APPEND CNA_PLATFORM_SOURCES "src/Sdl3/Sdl3AppleOrientation.mm")
    endif()
endif()
if(CNA_PLATFORM STREQUAL "SDL2")
    file(GLOB CNA_PLATFORM_IMPL_SOURCES CONFIGURE_DEPENDS "src/Sdl2/*.cpp")
    list(APPEND CNA_PLATFORM_SOURCES ${CNA_PLATFORM_IMPL_SOURCES})
endif()

# Implementation pieces shared between implementations that have no platform library to ask --
# std::filesystem path resolution, standard-library host facts. Not in the public include tree:
# an internal detail, not part of the contract.
file(GLOB CNA_PLATFORM_COMMON_SOURCES CONFIGURE_DEPENDS "src/Common/*.cpp")
list(APPEND CNA_PLATFORM_SOURCES ${CNA_PLATFORM_COMMON_SOURCES})

# Headless is ALWAYS compiled, whatever CNA_PLATFORM says. The conformance suite (PLAT-116) needs
# two implementations live in one process to be worth anything, and this one costs nothing to
# carry: it links no third-party library at all.
file(GLOB CNA_PLATFORM_HEADLESS_SOURCES CONFIGURE_DEPENDS "src/Headless/*.cpp")
list(APPEND CNA_PLATFORM_SOURCES ${CNA_PLATFORM_HEADLESS_SOURCES})

# Terminal is always compiled too, on every POSIX target, for the same reason and at the same
# cost -- termios and poll(2) are libc, not a third-party dependency. It is what makes PLAT-141's
# claim real: a THIRD implementation, one with a genuine display and genuine input that come from
# no windowing system at all, held to the same conformance suite. Windows has no termios, so the
# directory is excluded there rather than guarded file-by-file.
if(NOT WIN32)
    file(GLOB CNA_PLATFORM_TERMINAL_SOURCES CONFIGURE_DEPENDS "src/Terminal/*.cpp")
    list(APPEND CNA_PLATFORM_SOURCES ${CNA_PLATFORM_TERMINAL_SOURCES})
endif()
cna_add_module(cna_platform Platform ${CNA_PLATFORM_SOURCES})
cna_link_sharp_runtime(cna_platform PUBLIC Core.Base)

# Headers-only dependency on the core module for the CNAEXT marker macro, matching the math
# module's arrangement: no core symbol is referenced, so the link closure stays minimal.
target_link_libraries(cna_platform PUBLIC cna_core_headers)

# SDL3 is linked PRIVATE and only for the SDL3 selection: this module is the one place it may be
# linked at all, and even here it must not leak into the public include interface. The SDL-free
# probe (PLAT-27) is what proves it does not.
if(CNA_PLATFORM STREQUAL "SDL3")
    target_link_libraries(cna_platform PRIVATE SDL3::SDL3)

    # Android and iOS are the exception the probe cannot cover, because on exactly those two
    # targets CNA/Platform/Entrypoint.hpp really does include a platform header into the
    # consumer's own translation unit -- that is its whole job, and why PLAT-54 exempted it from
    # the SDL-free probe. A downstream executable therefore needs the include directory, and the
    # separately packaged main() implementation, or it compiles and then fails its final link on
    # a missing entry point. Desktop keeps the strict PRIVATE linkage above, so PLAT-27's probe
    # is unaffected: it builds for the host, never for a mobile target.
    if(ANDROID OR CNA_APPLE_IOS)
        target_link_libraries(cna_platform PUBLIC SDL3::SDL3)
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_platform PUBLIC SDL3::SDL3main)
        endif()
    endif()

    if(CNA_APPLE_IOS)
        set_source_files_properties(src/Sdl3/Sdl3AppleOrientation.mm
                PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
        target_link_libraries(cna_platform PRIVATE "-framework UIKit")
    endif()
endif()
if(CNA_PLATFORM STREQUAL "SDL2")
    target_link_libraries(cna_platform PRIVATE SDL2::SDL2)
endif()
