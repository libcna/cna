# SPDX-License-Identifier: MS-PL
#
# REMED-MEDIA-001 / 32-bit CI coverage (BUILD_TEST_CI Wave 2): a small, deliberately STANDALONE
# CMake project (not add_subdirectory()'d into the main build) that compiles and runs
# AudioTagParser.cpp's own width-dependent std::size_t bounds check at a genuine 32-bit width.
#
# Kept entirely separate from the main project's CMakeLists.txt/CMakePresets.json on purpose:
# the main build's CnaLibrary.cmake unconditionally requires SDL3/SDL3_image/SDL3_mixer and (on
# every non-MinGW/Emscripten/Android platform, which includes a plain `-m32` Linux build)
# FFmpeg -- none of which this project vendors or has installed for a 32-bit target, and none of
# which AudioTagParser.cpp or ContentLoadException.cpp actually need (confirmed by reading both
# files' own #include lists: standard library only). Reusing the main CMakeLists.txt here would
# mean cross-building the entire SDL3/FFmpeg stack for `-m32` just to test a handful of pure
# arithmetic functions -- a much larger, slower undertaking for no additional real coverage.
#
# Usage (see .github/workflows/32bit-arithmetic-ci.yml for the CI invocation):
#   sudo apt-get install -y gcc-multilib g++-multilib
#   cmake -S tools/media/arithmetic32bit -B cmake-build-32bit-arithmetic \
#         -DCMAKE_C_FLAGS=-m32 -DCMAKE_CXX_FLAGS=-m32 -DCMAKE_EXE_LINKER_FLAGS=-m32
#   cmake --build cmake-build-32bit-arithmetic
#   ./cmake-build-32bit-arithmetic/cna_audio_tag_parser_32bit_overflow_check

cmake_minimum_required(VERSION 3.20)
project(CnaAudioTagParser32BitOverflowCheck LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Root of the main CNA checkout, three directories up from here
# (tools/media/arithmetic32bit/ -> tools/media/ -> tools/ -> repo root). Overridable in case this
# is ever invoked with the standalone project relocated relative to the main tree.
set(CNA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." CACHE PATH
    "Path to the main CNA repository checkout (include/ and src/)")
get_filename_component(CNA_ROOT "${CNA_ROOT}" ABSOLUTE)

add_executable(cna_audio_tag_parser_32bit_overflow_check
    "${CMAKE_CURRENT_SOURCE_DIR}/../audio_tag_parser_32bit_overflow_check.cpp"
    "${CNA_ROOT}/src/CNA/Internal/Media/AudioTagParser.cpp"
    "${CNA_ROOT}/src/Microsoft/Xna/Framework/Content/ContentLoadException.cpp"
)
target_include_directories(cna_audio_tag_parser_32bit_overflow_check PRIVATE
    "${CNA_ROOT}/include"
)

enable_testing()
add_test(NAME AudioTagParser32BitOverflowCheck
         COMMAND cna_audio_tag_parser_32bit_overflow_check)
