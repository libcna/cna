# SPDX-License-Identifier: MS-PL
cmake_minimum_required(VERSION 3.20)
project(FakeSdl NONE)

get_filename_component(_source_name "${CMAKE_CURRENT_SOURCE_DIR}" NAME)
if(_source_name STREQUAL "SDL")
    set(_name SDL3)
else()
    set(_name SDL3_mixer)
endif()
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/version.txt" _version)
# Text payloads deliberately avoid compilers, SDKs, real SDL and platform libraries.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/payload" "complete ${_name} ${_version}\n")
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/lib${_name}.so"
    COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_CURRENT_BINARY_DIR}/payload"
        "${CMAKE_CURRENT_BINARY_DIR}/lib${_name}.so"
    DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/payload" VERBATIM)
add_custom_target(fake_sdl ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/lib${_name}.so")
configure_file(install.cmake.in install-fixture.cmake @ONLY)
install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/install-fixture.cmake")
# Script-compatible package configs: the fixture verifies discovery paths, not imported targets.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${_name}Config.cmake" "# Fake ${_name} ${_version}\n")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${_name}Config.cmake"
    DESTINATION "lib/cmake/${_name}")
