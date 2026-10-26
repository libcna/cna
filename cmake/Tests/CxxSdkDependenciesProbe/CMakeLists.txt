# SPDX-License-Identifier: MS-PL
#
# Fixture for cmake/Tests/CxxSdkDependenciesCase.cmake: stand-in FNA3D/MojoShader targets shaped as
# cmake/ThirdPartyFNA3D.cmake leaves them, then the real cna_install_cxx_dependencies(). Writes the
# bundled dependency list to bundled.txt.
#
#   FIXTURE_SHAPE  compiled-effects  cna_mojoshader -> mojoshader, no FNA3D renderer
#                  fna3d-renderer    as above, plus cna_fna3d -> FNA3D
#                  fused             an FNA3D revision without a mojoshader archive:
#                                    cna_mojoshader -> FNA3D

cmake_minimum_required(VERSION 3.20)
project(CnaCxxSdkDependenciesProbe LANGUAGES NONE)
include(GNUInstallDirs)

set(_cna_real_source_dir "${CNA_SOURCE_DIR}")
set(CNA_SOURCE_DIR "${CMAKE_CURRENT_BINARY_DIR}/fake-cna")
foreach(_file IN ITEMS third_party/cgltf/LICENSE third_party/stb/stb_image.h
        third_party/stb/stb_image_write.h fna3d/include/FNA3D.h fna3d/MojoShader/mojoshader.h)
    file(WRITE "${CNA_SOURCE_DIR}/${_file}" "")
endforeach()

add_library(FNA3D INTERFACE)
set_property(TARGET FNA3D PROPERTY SOURCE_DIR "${CNA_SOURCE_DIR}/fna3d")
add_library(cna_mojoshader INTERFACE)
if(FIXTURE_SHAPE STREQUAL "fused")
    target_link_libraries(cna_mojoshader INTERFACE FNA3D)
else()
    add_library(mojoshader INTERFACE)
    set_property(TARGET mojoshader PROPERTY SOURCE_DIR "${CNA_SOURCE_DIR}/fna3d")
    target_link_libraries(cna_mojoshader INTERFACE mojoshader)
endif()
if(FIXTURE_SHAPE STREQUAL "fna3d-renderer")
    add_library(cna_fna3d INTERFACE)
    target_link_libraries(cna_fna3d INTERFACE FNA3D cna_mojoshader)
endif()

include("${_cna_real_source_dir}/cmake/InstallCxxDependencies.cmake")
cna_install_cxx_dependencies()
get_property(_bundled GLOBAL PROPERTY CNA_CXX_BUNDLED_TARGETS)
file(WRITE "${CMAKE_BINARY_DIR}/bundled.txt" "${_bundled}")
