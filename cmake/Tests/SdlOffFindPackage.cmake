# SPDX-License-Identifier: MS-PL
#
# plans/plan_native_platform_validation.md NPV-0114: CNA_ENABLE_SDL=OFF promises that no SDL is
# found, and that has to hold for every project a CNA build adds -- the sibling easy-gl once ran
# find_package(SDL3 QUIET) from its example directory, found an SDL3 installed in /usr/local and
# linked it into the default target of an "SDL-free" build.
#
# A fixture project includes the real cmake/SdlAvailability.cmake and then looks SDL up the way a
# third-party subdirectory would, with an SDL3 package config sitting on CMAKE_PREFIX_PATH so the
# lookup can succeed. The AUTO configuration is the control: it must find that package, or the
# OFF case would be passing for the wrong reason.

foreach(_required IN ITEMS CNA_SOURCE_DIR CNA_WORK_DIR CNA_GENERATOR)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR "SdlOffFindPackage.cmake requires -D${_required}=...")
    endif()
endforeach()

file(REMOVE_RECURSE "${CNA_WORK_DIR}")
set(_fixture "${CNA_WORK_DIR}/fixture")
set(_prefix "${CNA_WORK_DIR}/prefix")

file(WRITE "${_prefix}/lib/cmake/SDL3/SDL3Config.cmake" "set(SDL3_FOUND TRUE)\n")
file(WRITE "${_prefix}/lib/cmake/SDL3/SDL3ConfigVersion.cmake"
    "set(PACKAGE_VERSION 3.0.0)\nset(PACKAGE_VERSION_COMPATIBLE TRUE)\n")

file(WRITE "${_fixture}/CMakeLists.txt" "
cmake_minimum_required(VERSION 3.23)
project(CnaSdlOffFindPackage LANGUAGES NONE)
set(CNA_PLATFORM X11)
set(CNA_AUDIO_PLATFORM NULL)
set(CNA_GRAPHICS_RENDERER OPENGL33)
include(\"${CNA_SOURCE_DIR}/cmake/SdlAvailability.cmake\")
add_subdirectory(third_party)
if(CNA_FIXTURE_REQUIRED)
    find_package(SDL3 REQUIRED CONFIG)
endif()
")
file(WRITE "${_fixture}/third_party/CMakeLists.txt" "
find_package(SDL3 QUIET)
message(STATUS \"fixture: third-party SDL3_FOUND=[\${SDL3_FOUND}]\")
")

function(_cna_configure_fixture name enable required out_result out_log)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -S "${_fixture}" -B "${CNA_WORK_DIR}/${name}"
            -G "${CNA_GENERATOR}"
            "-DCMAKE_PREFIX_PATH=${_prefix}"
            "-DCNA_ENABLE_SDL=${enable}"
            "-DCNA_FIXTURE_REQUIRED=${required}"
        RESULT_VARIABLE _result
        OUTPUT_VARIABLE _stdout
        ERROR_VARIABLE _stderr)
    set(${out_result} "${_result}" PARENT_SCOPE)
    set(${out_log} "${_stdout}${_stderr}" PARENT_SCOPE)
endfunction()

_cna_configure_fixture(auto AUTO OFF _result _log)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "The AUTO control failed to configure:\n${_log}")
endif()
if(NOT _log MATCHES "third-party SDL3_FOUND=\\[(1|TRUE|ON)\\]")
    message(FATAL_ERROR
        "The AUTO control did not find the fixture's SDL3 package, so the OFF case below would "
        "prove nothing:\n${_log}")
endif()

_cna_configure_fixture(off OFF OFF _result _log)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "CNA_ENABLE_SDL=OFF failed to configure the fixture:\n${_log}")
endif()
if(NOT _log MATCHES "third-party SDL3_FOUND=\\[(0|FALSE|OFF)?\\]")
    message(FATAL_ERROR
        "With CNA_ENABLE_SDL=OFF a subdirectory's optional find_package(SDL3) still found an "
        "installed SDL3:\n${_log}")
endif()

_cna_configure_fixture(off-required OFF ON _result _log)
if(_result EQUAL 0)
    message(FATAL_ERROR
        "With CNA_ENABLE_SDL=OFF a REQUIRED find_package(SDL3) configured successfully; it must "
        "fail loudly instead:\n${_log}")
endif()
if(NOT _log MATCHES "SDL3")
    message(FATAL_ERROR "The REQUIRED lookup failed without naming SDL3:\n${_log}")
endif()

file(REMOVE_RECURSE "${CNA_WORK_DIR}")
message(STATUS "CNA_ENABLE_SDL=OFF hides an installed SDL3 from optional and REQUIRED lookups")
