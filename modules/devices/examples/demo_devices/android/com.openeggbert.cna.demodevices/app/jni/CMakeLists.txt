cmake_minimum_required(VERSION 3.6)

project(GAME)

# plans/plan_devices.md Task DEVICES-0124: reuse CNA's own root project (and its
# existing, already-cross-compiled .sdl-prebuilt-Android-aarch64 SDL3/
# SDL3_image/SDL3_mixer install) instead of building a second, separate SDL
# from the vendored app/jni/SDL copy this template generates by default --
# avoids configuring/building SDL twice and any SDL3::SDL3 target collision.
set(CNA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CNA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../../.. cna_build)

# Your game and its CMakeLists.txt are in a subfolder named "src"
add_subdirectory(src)

