# SPDX-License-Identifier: MS-PL
cmake_minimum_required(VERSION 3.20)
project(CnaBuildSupportExportProbe LANGUAGES CXX)
enable_testing()

if(NOT CNA_SOURCE_DIR)
    message(FATAL_ERROR "CNA_SOURCE_DIR is required")
endif()
include("${CNA_SOURCE_DIR}/cmake/BuildPerformance.cmake")
if(NOT EMSCRIPTEN)
    set(CNA_SANITIZE undefined)
    cna_configure_instrumentation()
endif()

# Producer-only flags must still compile both kinds of target, but must not
# escape into an installed consumer (including the header-only route).
target_compile_options(cna_debug_info_options INTERFACE -DCNA_PROBE_PRODUCER=1)
# A semicolon inside a nested configuration expression must stay inside that
# expression when its installed usage requirements are captured.
target_compile_options(cna_emscripten_exception_abi INTERFACE
    "$<$<CONFIG:Debug>:-DCNA_PROBE_CONFIG_A=1;-DCNA_PROBE_CONFIG_B=1>")
add_library(probe_archive STATIC archive.cpp)
add_library(probe_headers INTERFACE)
foreach(_target IN ITEMS probe_archive probe_headers)
    cna_link_private_build_support(${_target}
        cna_instrumentation cna_debug_info_options cna_emscripten_exception_abi cna_size_options)
endforeach()
add_executable(probe_source source.cpp)
target_link_libraries(probe_source PRIVATE probe_archive probe_headers)
foreach(_target IN ITEMS probe_archive probe_source)
    target_compile_options(${_target} PRIVATE -Wall -Wextra -Werror)
endforeach()
add_test(NAME source_consumer COMMAND probe_source)
install(TARGETS probe_archive probe_headers EXPORT ProbeTargets ARCHIVE DESTINATION lib)
install(EXPORT ProbeTargets NAMESPACE Probe:: DESTINATION lib/cmake/Probe)
