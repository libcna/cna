# SPDX-License-Identifier: MS-PL
cmake_minimum_required(VERSION 3.20)
project(CnaIdeFoldersProbe NONE)

if(NOT CNA_IDE_FOLDER_MODULE)
    message(FATAL_ERROR "CNA_IDE_FOLDER_MODULE is required")
endif()
include("${CNA_IDE_FOLDER_MODULE}")

add_library(cna_fixture INTERFACE)
add_library(cna_renderer_fixture INTERFACE)
add_custom_target(CnaFixtureTests)
add_custom_target(cna_demo_fixture)
add_custom_target(cna_tool_fixture)
add_custom_target(cna_codegen_fixture)

cna_assign_ide_folders()

function(expect_folder target expected)
    get_target_property(_actual "${target}" FOLDER)
    if(NOT _actual STREQUAL expected)
        message(FATAL_ERROR "${target}: expected IDE folder '${expected}', got '${_actual}'")
    endif()
endfunction()

expect_folder(cna_fixture "CNA/Libraries")
expect_folder(cna_renderer_fixture "CNA/Renderers")
expect_folder(CnaFixtureTests "CNA/Tests")
expect_folder(cna_demo_fixture "CNA/Examples")
expect_folder(cna_tool_fixture "CNA/Tools")
expect_folder(cna_codegen_fixture "CNA/Build")
