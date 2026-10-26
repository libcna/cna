# SPDX-License-Identifier: MS-PL
#
# Fixture for cmake/Tests/Fna3dSourcePinCase.cmake: one configure of the FNA3D source-pin selection,
# as a given CNA revision ran it. revision-<rev>.cmake are verbatim copies of the cache statements
# of cmake/ThirdPartyFNA3D.cmake at that revision (prefork = every revision before 81ab03e72, here
# as 4bc4aa4bc had them), so a build tree any of them left behind can be reproduced exactly and then
# reconfigured with the current module.
#
#   FIXTURE_REVISION  prefork | 81ab03e72 | 13d0ed91a | module | real
#     module: cmake/Fna3dSourcePin.cmake with FIXTURE_DEFAULT_REPOSITORY / FIXTURE_DEFAULT_TAG
#     real:   cmake/ThirdPartyFNA3D.cmake itself, with CNA's own defaults
#
# The pair the configure ended with is written to fna3d-pin.txt in the build directory.

cmake_minimum_required(VERSION 3.20)
project(CnaFna3dSourcePinFixture LANGUAGES NONE)
# Every run passes the same -D set; reading them keeps CMake's unused-variable notice out of the log.
set(_fixture_inputs "${CNA_SOURCE_DIR}${FIXTURE_DEFAULT_REPOSITORY}${FIXTURE_DEFAULT_TAG}")

if(FIXTURE_REVISION MATCHES "^(prefork|81ab03e72|13d0ed91a)$")
    include("${CMAKE_CURRENT_SOURCE_DIR}/revision-${FIXTURE_REVISION}.cmake")
elseif(FIXTURE_REVISION STREQUAL "module")
    include("${CNA_SOURCE_DIR}/cmake/Fna3dSourcePin.cmake")
    cna_fna3d_select_source_pin(
        DEFAULT_REPOSITORY "${FIXTURE_DEFAULT_REPOSITORY}"
        DEFAULT_TAG "${FIXTURE_DEFAULT_TAG}")
elseif(FIXTURE_REVISION STREQUAL "real")
    include("${CNA_SOURCE_DIR}/cmake/ThirdPartyFNA3D.cmake")
else()
    message(FATAL_ERROR "Fna3dSourcePinFixture: unknown FIXTURE_REVISION '${FIXTURE_REVISION}'")
endif()

get_property(_repository_type CACHE CNA_FNA3D_GIT_REPOSITORY PROPERTY TYPE)
get_property(_tag_type CACHE CNA_FNA3D_GIT_TAG PROPERTY TYPE)
get_property(_repository_help CACHE CNA_FNA3D_GIT_REPOSITORY PROPERTY HELPSTRING)
get_property(_tag_help CACHE CNA_FNA3D_GIT_TAG PROPERTY HELPSTRING)
file(WRITE "${CMAKE_BINARY_DIR}/fna3d-pin.txt"
    "repository=${CNA_FNA3D_GIT_REPOSITORY}\n"
    "tag=${CNA_FNA3D_GIT_TAG}\n"
    "repository-type=${_repository_type}\n"
    "tag-type=${_tag_type}\n"
    "repository-help=${_repository_help}\n"
    "tag-help=${_tag_help}\n"
    "default-repository=$CACHE{_CNA_FNA3D_DEFAULT_GIT_REPOSITORY}\n"
    "default-tag=$CACHE{_CNA_FNA3D_DEFAULT_GIT_TAG}\n")
