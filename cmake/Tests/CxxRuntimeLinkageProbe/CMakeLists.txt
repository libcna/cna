# SPDX-License-Identifier: MS-PL
# Programs that choose their C++ runtime in each of the usual ways, all linking an interface that
# carries the runtime-first option the way CNA::BuildConfig does (cmake/BuildPerformance.cmake).
# cmake/Tests/CxxRuntimeLinkageCase.cmake configures this and reads the link lines.

cmake_minimum_required(VERSION 3.20)
project(CNACxxRuntimeLinkageProbe LANGUAGES CXX)

include("${CNA_SOURCE_DIR}/cmake/BuildPerformance.cmake")

file(WRITE "${CMAKE_BINARY_DIR}/main.cpp" "int main() { return 0; }\n")
file(WRITE "${CMAKE_BINARY_DIR}/module.cpp" "int probe_module() { return 0; }\n")

add_library(probe_build_config INTERFACE)
cna_add_cxx_runtime_first_link_option(probe_build_config)
# Reached through a static library, as a game reaches CNA::BuildConfig through CNA's modules.
add_library(probe_module STATIC "${CMAKE_BINARY_DIR}/module.cpp")
set_target_properties(probe_module PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(probe_module PUBLIC probe_build_config)

function(probe_program name)
    add_executable(${name} "${CMAKE_BINARY_DIR}/main.cpp")
    target_link_libraries(${name} PRIVATE probe_module)
endfunction()

probe_program(plain)
probe_program(libgcc_option)
target_link_options(libgcc_option PRIVATE -static-libgcc)
probe_program(libstdcxx_option)
target_link_options(libstdcxx_option PRIVATE -static-libstdc++)
probe_program(both_options)
target_link_options(both_options PRIVATE -static-libgcc -static-libstdc++)
probe_program(static_option)
target_link_options(static_option PRIVATE -static)
probe_program(config_option)
target_link_options(config_option PRIVATE $<$<CONFIG:Release>:-static-libstdc++>)
probe_program(link_flags)
set_target_properties(link_flags PROPERTIES LINK_FLAGS -static-libstdc++)
probe_program(link_libraries)
target_link_libraries(link_libraries PRIVATE -static-libstdc++)

add_library(plain_shared SHARED "${CMAKE_BINARY_DIR}/module.cpp")
target_link_libraries(plain_shared PRIVATE probe_module)
add_library(libstdcxx_shared SHARED "${CMAKE_BINARY_DIR}/module.cpp")
target_link_libraries(libstdcxx_shared PRIVATE probe_module)
target_link_options(libstdcxx_shared PRIVATE -static-libstdc++)

add_subdirectory(consumer)
