cmake_minimum_required(VERSION 3.6)

project(my_app)

# plans/plan_devices.md Task DEVICES-0124: CNA's own add_subdirectory(cna_build)
# (the parent CMakeLists.txt) already cached SDL3_DIR pointing at the
# cross-compiled .sdl-prebuilt-Android-aarch64 install, but the SDL3::SDL3
# IMPORTED target itself is scoped to that directory, not visible to this
# sibling "src" subdirectory -- re-running find_package() here is a cheap,
# idempotent re-import against the same cached SDL3_DIR, not a second build.
if(NOT TARGET SDL3::SDL3)
    find_package(SDL3 CONFIG REQUIRED)
endif()

add_library(main SHARED
    Main.cpp
    DevicesDemo.cpp
    DevicesDemo.hpp
)
target_link_libraries(main PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
