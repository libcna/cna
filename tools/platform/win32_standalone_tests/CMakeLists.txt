# =====================================================================================
# Standalone Win32 platform test harness (plans/plan_win32.md WIN32-0004, section 16)
#
# Builds and runs the native Win32 platform backend -- the contract, the Headless reference
# implementation it is held against, the Win32 implementation itself and the whole platform test
# suite -- WITHOUT configuring the rest of CNA.
#
# Why it exists
# -------------
# `cna_platform` depends on nothing outside the C++ standard library: it has zero SharpRuntime
# includes, zero CNAEXT includes and, for CNA_PLATFORM=WIN32, zero third-party links. A full CNA
# configure nonetheless needs the sharp-runtime *sibling checkout*, which a sandboxed or offline
# worker may not be able to obtain. This harness makes the platform module's own validation
# independent of that, so the Win32 backend can be built and its tests actually EXECUTED on a
# Linux host through mingw-w64 + Wine.
#
# It is not a replacement for the real build. `cmake/UnitTests.cmake` remains where these tests
# run in a normal configuration; this harness compiles exactly the same test sources.
#
# Usage (from the repository root):
#
#   cmake -S tools/platform/win32_standalone_tests -B cmake-build-win32 -G Ninja \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake -DCMAKE_BUILD_TYPE=Debug
#   cmake --build cmake-build-win32 --parallel
#   Xvfb :97 -screen 0 1280x1024x24 &
#   DISPLAY=:97 WINEDEBUG=-all wine64 cmake-build-win32/cna_platform_win32_tests.exe
# =====================================================================================

cmake_minimum_required(VERSION 3.20)
project(CnaPlatformWin32StandaloneTests CXX)

if(NOT WIN32)
    message(FATAL_ERROR
        "This harness builds the Win32 platform backend and therefore needs a Windows toolchain. "
        "From Linux use -DCMAKE_TOOLCHAIN_FILE=<repo>/cmake/toolchains/mingw-w64.cmake.")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(CNA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

# The selection the real cmake/PlatformSelection.cmake would make, restated because that file is
# part of the full configure this harness deliberately does not run.
add_compile_definitions(CNA_PLATFORM_WIN32)

# --- googletest ------------------------------------------------------------------------------
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
add_subdirectory("${CNA_ROOT}/vendor/googletest" googletest EXCLUDE_FROM_ALL)

# --- the platform module, exactly as modules/platform/CMakeLists.txt composes it for WIN32 ----
file(GLOB CNA_PLATFORM_SOURCES CONFIGURE_DEPENDS
        "${CNA_ROOT}/modules/platform/src/*.cpp"
        "${CNA_ROOT}/modules/platform/src/Common/*.cpp"
        "${CNA_ROOT}/modules/platform/src/Headless/*.cpp"
        "${CNA_ROOT}/modules/platform/src/Win32/*.cpp")

add_library(cna_platform_win32 STATIC ${CNA_PLATFORM_SOURCES})
target_include_directories(cna_platform_win32
        PUBLIC "${CNA_ROOT}/modules/platform/include"
        PUBLIC "${CNA_ROOT}/modules/platform/src")
target_link_libraries(cna_platform_win32 PRIVATE user32 gdi32 opengl32 ole32 shell32 uuid)
target_compile_options(cna_platform_win32 PRIVATE -Wall -Wextra)

# --- the tests ---------------------------------------------------------------------------------
# The implementation-neutral suites plus the Win32-specific ones. The SDL3, SDL2 and Terminal
# suites are excluded for the same reason cmake/UnitTests.cmake excludes them under a different
# selection: they reference implementations that are not compiled here.
file(GLOB CNA_PLATFORM_TEST_SOURCES CONFIGURE_DEPENDS
        "${CNA_ROOT}/modules/platform/tests/CNA/Platform/*.cpp")
list(FILTER CNA_PLATFORM_TEST_SOURCES EXCLUDE REGEX ".*/Sdl[23].*\\.cpp$")
list(FILTER CNA_PLATFORM_TEST_SOURCES EXCLUDE REGEX ".*/Terminal.*\\.cpp$")
# Exercises the SDL3 audio contract headers, which live in a module this harness does not build.
list(FILTER CNA_PLATFORM_TEST_SOURCES EXCLUDE REGEX ".*/ContractIsSdlFreeTests\\.cpp$")
# Includes SharpRuntime's System/Environment.hpp, which is the one dependency this harness exists
# to do without. It runs unchanged in a normal CNA configure.
list(FILTER CNA_PLATFORM_TEST_SOURCES EXCLUDE REGEX ".*/StandardFileSystemTests\\.cpp$")

add_executable(cna_platform_win32_tests ${CNA_PLATFORM_TEST_SOURCES})
# NamespaceCollisionTests includes CNA/TargetPlatform.hpp from the core module. Headers only:
# nothing in the platform test suite references a core symbol, so no link edge is needed.
target_include_directories(cna_platform_win32_tests
        PRIVATE "${CNA_ROOT}/modules/platform/tests"
        PRIVATE "${CNA_ROOT}/modules/core/include")
target_link_libraries(cna_platform_win32_tests PRIVATE cna_platform_win32 gtest gtest_main)
target_compile_options(cna_platform_win32_tests PRIVATE -Wall -Wextra)
# Runtime linkage: the DYNAMIC C++ runtime, with the DLLs copied next to the executable.
#
# cmake/ThirdPartySDL.cmake already records the reason in cna_copy_mingw_cxx_runtime -- "targets
# with large RTTI graphs may not be compatible with -static-libstdc++" -- and this is one of them.
# The platform contract is built on interfaces and the services resolve windows with dynamic_cast,
# so every object carries the inline std::type_info::operator== that GCC 13's mingw-w64
# libstdc++.a also defines in tinfo.o; a static link reports it as multiply defined. Copying the
# three runtime DLLs is the repository's own documented answer, and it is what lets the produced
# .exe run under a bare Wine prefix.
foreach(_cna_runtime_dll libgcc_s_seh-1.dll libgcc_s_dw2-1.dll libgcc_s_sjlj-1.dll
                         libstdc++-6.dll libwinpthread-1.dll)
    execute_process(COMMAND "${CMAKE_CXX_COMPILER}" "-print-file-name=${_cna_runtime_dll}"
            OUTPUT_VARIABLE _cna_runtime_path OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(_cna_runtime_path AND _cna_runtime_path MATCHES "[/\\\\]" AND EXISTS "${_cna_runtime_path}")
        file(TO_CMAKE_PATH "${_cna_runtime_path}" _cna_runtime_path)
        add_custom_command(TARGET cna_platform_win32_tests POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${_cna_runtime_path}" $<TARGET_FILE_DIR:cna_platform_win32_tests>
                COMMENT "Copying ${_cna_runtime_dll} next to the Win32 platform tests"
                VERBATIM)
    endif()
endforeach()

enable_testing()
add_test(NAME CnaPlatformWin32Tests COMMAND cna_platform_win32_tests)
