# =====================================================================================
# Standalone platform-module test harness (plans/plan_win32.md WIN32-0004, section 16)
#
# Builds and runs the CNA platform module -- the contract, every implementation available for the
# selected target, and the module's whole test suite -- WITHOUT configuring the rest of CNA.
#
# Why it exists
# -------------
# `cna_platform` depends on nothing outside the C++ standard library: it has zero SharpRuntime
# includes, zero CNAEXT includes, and for the non-SDL selections zero third-party links. A full CNA
# configure nonetheless needs the sharp-runtime *sibling checkout*, which a sandboxed or offline
# worker may not be able to obtain. This harness makes the platform module's own validation
# independent of that.
#
# It does two things a normal configure cannot:
#
#   * it runs the platform suite for a selection whose target is not this host -- the Win32 backend
#     is built through mingw-w64 and executed under Wine, which is how it was developed;
#   * it runs the suite for several selections in one session, which is what makes "the other
#     backends did not regress" a measurement rather than an assumption.
#
# It is not a replacement for the real build: `cmake/UnitTests.cmake` remains where these tests run
# in a normal configuration, and this harness compiles exactly the same test sources under exactly
# the same selection rules.
#
# Usage (from the repository root)
# --------------------------------
#   # the native Win32 backend, cross-built and run under Wine
#   cmake -S tools/platform/standalone_tests -B cmake-build-win32 -G Ninja \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake \
#         -DCNA_PLATFORM=WIN32 -DCMAKE_BUILD_TYPE=Debug
#   cmake --build cmake-build-win32 --parallel
#   Xvfb :97 -screen 0 1280x1024x24 &
#   DISPLAY=:97 WINEDEBUG=-all wine64 cmake-build-win32/cna_platform_tests.exe
#
#   # a host-native selection
#   cmake -S tools/platform/standalone_tests -B cmake-build-platform-headless -G Ninja \
#         -DCNA_PLATFORM=HEADLESS -DCMAKE_BUILD_TYPE=Debug
#   cmake --build cmake-build-platform-headless --parallel
#   ./cmake-build-platform-headless/cna_platform_tests
# =====================================================================================

cmake_minimum_required(VERSION 3.20)
project(CnaPlatformStandaloneTests CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The warning set, expressed so it survives a native MSVC build of the same harness: cl.exe reads
# `-Wall` as `/Wall` -- a different and far noisier level than GCC's -- and does not know `-Wextra`
# at all. /W4 is the MSVC equivalent of what the rest of CNA asks for.
if(MSVC)
    set(CNA_HARNESS_WARNINGS /W4)
    # The sources are UTF-8. Without /utf-8, cl.exe reads them in the machine's ANSI code page,
    # and a test's non-ASCII character literal becomes "too many characters in constant" (C2015)
    # -- which is how the first native MSVC run of this harness failed to build.
    add_compile_options(/utf-8)
else()
    set(CNA_HARNESS_WARNINGS -Wall -Wextra)
endif()
get_filename_component(CNA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

# The real selection file, not a paraphrase of it: this harness is also how the selection's
# host-conditional refusals get exercised, and a second copy of that logic would drift.
include("${CNA_ROOT}/cmake/PlatformSelection.cmake")

# --- googletest ------------------------------------------------------------------------------
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
add_subdirectory("${CNA_ROOT}/vendor/googletest" googletest EXCLUDE_FROM_ALL)

# --- the platform module, composed exactly as modules/platform/CMakeLists.txt composes it -----
set(CNA_PLATFORM_SOURCE_GLOBS
        "${CNA_ROOT}/modules/platform/src/*.cpp"
        "${CNA_ROOT}/modules/platform/src/Common/*.cpp"
        # Always compiled, whatever the selection: the conformance suite needs two implementations
        # live in one process to be worth anything.
        "${CNA_ROOT}/modules/platform/src/Headless/*.cpp")
if(CNA_PLATFORM STREQUAL "WIN32")
    list(APPEND CNA_PLATFORM_SOURCE_GLOBS "${CNA_ROOT}/modules/platform/src/Win32/*.cpp")
elseif(CNA_PLATFORM STREQUAL "SDL3")
    list(APPEND CNA_PLATFORM_SOURCE_GLOBS "${CNA_ROOT}/modules/platform/src/Sdl3/*.cpp")
elseif(CNA_PLATFORM STREQUAL "SDL2")
    list(APPEND CNA_PLATFORM_SOURCE_GLOBS "${CNA_ROOT}/modules/platform/src/Sdl2/*.cpp")
endif()
if(NOT WIN32)
    # Terminal is always compiled on POSIX, at the same cost and for the same reason as Headless:
    # termios and poll(2) are libc, not a third-party dependency.
    list(APPEND CNA_PLATFORM_SOURCE_GLOBS "${CNA_ROOT}/modules/platform/src/Terminal/*.cpp")
endif()
file(GLOB CNA_PLATFORM_SOURCES CONFIGURE_DEPENDS ${CNA_PLATFORM_SOURCE_GLOBS})

add_library(cna_platform STATIC ${CNA_PLATFORM_SOURCES})
# The core module's include root carries CNA/TargetPlatform.hpp and the CNAEXT marker macro, which
# the SDL3 implementation and NamespaceCollisionTests include. Headers only -- the real
# modules/platform/CMakeLists.txt links cna_core_headers for exactly the same reason, and no core
# symbol is referenced either way.
target_include_directories(cna_platform
        PUBLIC "${CNA_ROOT}/modules/platform/include"
        PUBLIC "${CNA_ROOT}/modules/platform/src"
        PUBLIC "${CNA_ROOT}/modules/core/include")
target_compile_options(cna_platform PRIVATE ${CNA_HARNESS_WARNINGS})

if(CNA_PLATFORM STREQUAL "WIN32")
    # Operating-system import libraries only -- nothing to fetch or vendor. PRIVATE for the same
    # reason SDL is: an implementation detail must not become a usage requirement.
    target_link_libraries(cna_platform PRIVATE user32 gdi32 opengl32 ole32 shell32 uuid)
elseif(CNA_PLATFORM STREQUAL "SDL3" OR CNA_PLATFORM STREQUAL "SDL2")
    # Resolved from the persistent vendored install the real configure maintains, so this harness
    # never launches a toolkit build of its own.
    #
    # The cache directory's name is keyed by more than system and processor: cmake/ThirdPartySDL.cmake
    # appends a suffix for a Linux build whose Wayland prerequisites were present, and others for
    # Apple deployment targets and the iOS simulator. Rather than reproduce that naming, every
    # matching root is offered to find_package and the first usable one wins. An explicit
    # -DCNA_SDL_PREBUILT_ROOT still overrides it.
    if(CNA_SDL_PREBUILT_ROOT)
        list(APPEND CMAKE_PREFIX_PATH "${CNA_SDL_PREBUILT_ROOT}/install")
    else()
        file(GLOB CNA_SDL_PREBUILT_CANDIDATES
                "${CNA_ROOT}/.sdl-prebuilt-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}*")
        foreach(_cna_candidate IN LISTS CNA_SDL_PREBUILT_CANDIDATES)
            if(IS_DIRECTORY "${_cna_candidate}/install")
                list(APPEND CMAKE_PREFIX_PATH "${_cna_candidate}/install")
                message(STATUS "CNA: harness SDL prefix ${_cna_candidate}/install")
            endif()
        endforeach()
    endif()
    if(CNA_PLATFORM STREQUAL "SDL3")
        find_package(SDL3 REQUIRED)
        target_link_libraries(cna_platform PRIVATE SDL3::SDL3)
    else()
        find_package(SDL2 REQUIRED)
        target_link_libraries(cna_platform PRIVATE SDL2::SDL2)
    endif()
endif()

# --- the tests ---------------------------------------------------------------------------------
# The same per-selection filtering cmake/UnitTests.cmake applies, for the same reasons: a suite
# that reaches into an implementation this configuration did not compile would fail to link.
file(GLOB CNA_PLATFORM_TEST_SOURCES CONFIGURE_DEPENDS
        "${CNA_ROOT}/modules/platform/tests/CNA/Platform/*.cpp")
if(NOT CNA_PLATFORM STREQUAL "WIN32")
    list(FILTER CNA_PLATFORM_TEST_SOURCES EXCLUDE REGEX ".*/Win32.*\\.cpp$")
endif()
if(NOT CNA_PLATFORM STREQUAL "SDL3")
    list(FILTER CNA_PLATFORM_TEST_SOURCES EXCLUDE REGEX ".*/Sdl3.*\\.cpp$")
endif()
if(NOT CNA_PLATFORM STREQUAL "SDL2")
    list(FILTER CNA_PLATFORM_TEST_SOURCES EXCLUDE REGEX ".*/Sdl2.*\\.cpp$")
endif()
# plans/plan_native_platforms_integration.md NPI-0015: this harness's filters were all written
# before the X11 backend existed, so X11*.cpp fell through none of them and was compiled into
# every non-X11 selection (a WIN32 mingw cross-build fails outright: X11Headers.hpp has nothing to
# find on that toolchain).
if(NOT CNA_PLATFORM STREQUAL "X11")
    list(FILTER CNA_PLATFORM_TEST_SOURCES EXCLUDE REGEX ".*/X11.*\\.cpp$")
endif()
# plans/plan_wayland.md WAYLAND-0110/0128: the same for the native Wayland suites and the test
# compositor -- their headers are wayland-client's, which a WIN32 mingw cross-build has nothing to
# find either.
if(NOT CNA_PLATFORM STREQUAL "WAYLAND")
    list(FILTER CNA_PLATFORM_TEST_SOURCES EXCLUDE REGEX ".*/Wayland[A-Za-z]*\\.(cpp|hpp)$")
endif()
# plans/plan_wayland.md Phase B: the suites of the code the two native Unix backends share
# (src/Xkb/, src/Freedesktop/, src/Posix/, src/Linux/) are compiled only for those selections --
# the code itself is in cna_platform only there. They carried an X11 prefix until Phase B renamed
# them, which is when they started falling through the X11 filter above.
if(NOT CNA_PLATFORM STREQUAL "X11" AND NOT CNA_PLATFORM STREQUAL "WAYLAND")
    list(FILTER CNA_PLATFORM_TEST_SOURCES EXCLUDE REGEX ".*/(Xkb|Freedesktop|Posix|Linux)[A-Za-z]*\\.cpp$")
endif()
if(WIN32)
    list(FILTER CNA_PLATFORM_TEST_SOURCES EXCLUDE REGEX ".*/Terminal.*\\.cpp$")
endif()

# Two suites are excluded from this harness specifically, and only from it:
#
#   ContractIsSdlFreeTests includes the audio module's contract headers, which live in a module
#   this harness does not build. It runs unchanged in a normal configure, and tools/platform/
#   check_contract.py enforces the same rule mechanically on every commit.
#
list(FILTER CNA_PLATFORM_TEST_SOURCES EXCLUDE REGEX ".*/ContractIsSdlFreeTests\\.cpp$")

# Every suite that includes a SharpRuntime header, detected rather than enumerated. SharpRuntime is
# the sibling checkout this harness exists to do without, and a hand-written list would go stale the
# first time a test grew such an include -- silently, as a link error a reader would have to
# diagnose. The excluded names are printed so a run is never quietly narrower than it looks.
set(CNA_PLATFORM_TESTS_NEEDING_SHARP_RUNTIME)
foreach(_cna_test IN LISTS CNA_PLATFORM_TEST_SOURCES)
    file(STRINGS "${_cna_test}" _cna_sharp_includes REGEX "^#include \"System/")
    if(_cna_sharp_includes)
        get_filename_component(_cna_test_name "${_cna_test}" NAME)
        list(APPEND CNA_PLATFORM_TESTS_NEEDING_SHARP_RUNTIME "${_cna_test_name}")
        list(REMOVE_ITEM CNA_PLATFORM_TEST_SOURCES "${_cna_test}")
    endif()
endforeach()
if(CNA_PLATFORM_TESTS_NEEDING_SHARP_RUNTIME)
    list(JOIN CNA_PLATFORM_TESTS_NEEDING_SHARP_RUNTIME ", " _cna_excluded)
    message(STATUS "CNA: harness excludes ${_cna_excluded} (SharpRuntime); they run in a normal configure")
endif()
# Needs the separately built restoration/resize harness executables cmake/UnitTests.cmake
# registers; four of their five exit paths destroy the process, so they cannot run in-process.
list(FILTER CNA_PLATFORM_TEST_SOURCES EXCLUDE REGEX ".*/TerminalRestorationTests\\.cpp$")
list(FILTER CNA_PLATFORM_TEST_SOURCES EXCLUDE REGEX ".*/TerminalResizeTests\\.cpp$")
# plans/plan_vulkan.md VULKAN-154/VULKAN-157: this one provokes a real Xlib protocol error, and
# Xlib's default handler calls exit(). cmake/UnitTests.cmake therefore gives it a ctest process of
# its own (CnaPlatformXErrorHandlerTests) rather than letting it share a binary. This harness
# produces one binary, so running it inline would end the whole run at whatever test came next --
# which is the failure the test itself documents, not a result about the suite.
list(FILTER CNA_PLATFORM_TEST_SOURCES EXCLUDE REGEX ".*/Sdl3XErrorHandlerTests\\.cpp$")
# Holds the input module's SDL key bridge to the platform's own table, so it needs cna_input --
# the one platform test with a cross-module dependency. cmake/UnitTests.cmake gives the platform
# test group that edge explicitly; this harness deliberately builds one module.
list(FILTER CNA_PLATFORM_TEST_SOURCES EXCLUDE REGEX ".*/Sdl3KeyCodeRoundTripTests\\.cpp$")

add_executable(cna_platform_tests ${CNA_PLATFORM_TEST_SOURCES})
# NamespaceCollisionTests includes CNA/TargetPlatform.hpp from the core module. Headers only:
# nothing in the platform suite references a core symbol, so no link edge is needed.
target_include_directories(cna_platform_tests
        PRIVATE "${CNA_ROOT}/modules/platform/tests"
        PRIVATE "${CNA_ROOT}/modules/core/include")
target_link_libraries(cna_platform_tests PRIVATE cna_platform gtest gtest_main)
target_compile_options(cna_platform_tests PRIVATE ${CNA_HARNESS_WARNINGS})

# The SDL implementation suites include SDL's own headers to build native event structures, so the
# test target needs SDL directly -- cna_platform links it PRIVATE precisely so it does not leak to
# every consumer. cmake/UnitTests.cmake gives the real test target the same edge for the same
# reason.
if(CNA_PLATFORM STREQUAL "SDL3")
    target_link_libraries(cna_platform_tests PRIVATE SDL3::SDL3)
elseif(CNA_PLATFORM STREQUAL "SDL2")
    target_link_libraries(cna_platform_tests PRIVATE SDL2::SDL2)
endif()

if(CNA_PLATFORM STREQUAL "WIN32")
    # Points Win32NoSdlTests at the tree it audits. Under Wine the host filesystem is reached as
    # the Z: drive, so a bare POSIX path would not open and the audit would skip instead of run --
    # the same reasoning as cmake/UnitTests.cmake's copy of this.
    if(CMAKE_CROSSCOMPILING)
        set(CNA_WIN32_SOURCE_ROOT "Z:${CNA_ROOT}/modules/platform/src/Win32")
    else()
        set(CNA_WIN32_SOURCE_ROOT "${CNA_ROOT}/modules/platform/src/Win32")
    endif()
    target_compile_definitions(cna_platform_tests
            PRIVATE CNA_WIN32_SOURCE_ROOT="${CNA_WIN32_SOURCE_ROOT}")
endif()

# --- the real renderer integration probe (plans/plan_win32.md WIN32-0062/WIN32-0063) -------------
# A separate program rather than a test case: it needs d3d11/d3d12/dxgi, and a GPU dependency does
# not belong in the platform module's own suite -- `platform != renderer` is exactly the invariant
# that workstream exists to keep. It also reports "no Direct3D on this machine" as a distinct exit
# status, because that is an environment limitation rather than a defect.
if(CNA_PLATFORM STREQUAL "WIN32")
    add_executable(cna_win32_directx_probe win32_directx_probe.cpp)
    target_link_libraries(cna_win32_directx_probe PRIVATE cna_platform d3d11 d3d12 dxgi)
    target_compile_options(cna_win32_directx_probe PRIVATE ${CNA_HARNESS_WARNINGS})

    # --- the lifecycle stress and leak harness -------------------------------------------------
    # plans/plan_win32_native_validation.md. Also a separate program rather than a test case, for
    # a different reason: it asks whether ten thousand repetitions cost the process anything, which
    # needs Windows' own USER/GDI object accounting (Wine keeps its own, so this question could not
    # be asked before) and takes minutes rather than milliseconds.
    add_executable(cna_win32_native_stress win32_native_stress.cpp)
    target_link_libraries(cna_win32_native_stress PRIVATE cna_platform user32 gdi32 psapi)
    target_compile_options(cna_win32_native_stress PRIVATE ${CNA_HARNESS_WARNINGS})

    # --- the real-desktop checks ----------------------------------------------------------------
    # The questions that need a logged-on Windows desktop and a real Windows kernel: what the
    # backend does to process-global state it does not own, what DPI reports at a scaling factor
    # other than 100 %, whether the clipboard it writes is the one other programs read, and whether
    # repeated cursor changes leak USER objects. None of these can be answered from inside a single
    # process on an invisible window station, which is where an SSH session lives -- see
    # tools/platform/win32_run_interactive.ps1.
    add_executable(cna_win32_native_desktop win32_native_desktop.cpp)
    target_link_libraries(cna_win32_native_desktop PRIVATE cna_platform user32 gdi32 ole32)
    target_compile_options(cna_win32_native_desktop PRIVATE ${CNA_HARNESS_WARNINGS})
endif()

# --- MinGW runtime ------------------------------------------------------------------------------
# The DYNAMIC C++ runtime, with the DLLs copied next to each executable.
# cmake/ThirdPartySDL.cmake already records the reason in cna_copy_mingw_cxx_runtime -- "targets
# with large RTTI graphs may not be compatible with -static-libstdc++" -- and this is one of them.
# The platform contract is built on interfaces and its services resolve windows with dynamic_cast,
# so every object carries the inline std::type_info::operator== that GCC 13's mingw-w64
# libstdc++.a also defines in tinfo.o; a static link reports it as multiply defined.
if(MINGW)
    foreach(_cna_target IN ITEMS cna_platform_tests cna_win32_directx_probe cna_win32_native_stress
                              cna_win32_native_desktop)
        if(NOT TARGET ${_cna_target})
            continue()
        endif()
        foreach(_cna_runtime_dll libgcc_s_seh-1.dll libgcc_s_dw2-1.dll libgcc_s_sjlj-1.dll
                                 libstdc++-6.dll libwinpthread-1.dll)
            execute_process(COMMAND "${CMAKE_CXX_COMPILER}" "-print-file-name=${_cna_runtime_dll}"
                    OUTPUT_VARIABLE _cna_runtime_path OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
            if(_cna_runtime_path AND _cna_runtime_path MATCHES "[/\\\\]"
                    AND EXISTS "${_cna_runtime_path}")
                file(TO_CMAKE_PATH "${_cna_runtime_path}" _cna_runtime_path)
                add_custom_command(TARGET ${_cna_target} POST_BUILD
                        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                                "${_cna_runtime_path}" $<TARGET_FILE_DIR:${_cna_target}>
                        VERBATIM)
            endif()
        endforeach()
    endforeach()
endif()

message(STATUS "CNA: standalone platform harness for CNA_PLATFORM=${CNA_PLATFORM}")

enable_testing()
add_test(NAME CnaPlatformTests COMMAND cna_platform_tests)
