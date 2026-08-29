# SPDX-License-Identifier: MS-PL

cmake_minimum_required(VERSION 3.20)

project(CNALinkerSelectionProbe LANGUAGES CXX)

if(NOT CNA_SOURCE_DIR)
    message(FATAL_ERROR "CNA_SOURCE_DIR must point at the CNA source tree.")
endif()

include("${CNA_SOURCE_DIR}/cmake/BuildPerformance.cmake")
cna_configure_linker()

add_executable(cna_linker_selection_probe main.cpp)
target_link_libraries(cna_linker_selection_probe PRIVATE CNA::LinkerOptions)

get_target_property(_cna_probe_link_options cna_linker_options INTERFACE_LINK_OPTIONS)
if(NOT _cna_probe_link_options)
    set(_cna_probe_link_options "<toolchain-default>")
endif()
message(STATUS "CNA linker probe options: ${_cna_probe_link_options}")
