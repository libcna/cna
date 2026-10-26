# SPDX-License-Identifier: MS-PL
#
# 2026-10-26 dependency audit, findings C-06 and C-07: how cna_configure_mojoshader() adds FNA3D to
# the build.
#   C-07  FNA3D is a static archive whatever BUILD_SHARED_LIBS the including project has -- also a
#         normal variable under CMP0126 NEW, which hides a cache entry -- and the variable is put
#         back exactly as it was, cache entry, type, docstring and absence included.
#   C-06  FNA3D's directory stays out of `all` even where FetchContent_Declare(EXCLUDE_FROM_ALL)
#         does not reach add_subdirectory(): CMake before 3.28, or a dependency provider. The
#         provider here adds the directory the way FetchContent_MakeAvailable did before 3.28.
#
# A fake FNA3D source tree (shaped like the real CMakeLists.txt, including its option(
# BUILD_SHARED_LIBS ... ON) under cmake_minimum_required(3.10)) is configured through the real
# cmake/ThirdPartyFNA3D.cmake. Configure only; nothing is compiled.
#
#   cmake -DCNA_SOURCE_DIR=<repo> -DCNA_WORK_DIR=<dir> [-DCNA_GENERATOR=<g>] \
#         -P cmake/Tests/Fna3dDependencyShapeCase.cmake

cmake_minimum_required(VERSION 3.20)

foreach(_required IN ITEMS CNA_SOURCE_DIR CNA_WORK_DIR)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR "Fna3dDependencyShapeCase.cmake requires -D${_required}=...")
    endif()
endforeach()
set(_w "${CNA_WORK_DIR}")
file(REMOVE_RECURSE "${_w}")

set(_fake "${_w}/fake-fna3d")
file(WRITE "${_fake}/CMakeLists.txt" "cmake_minimum_required(VERSION 3.10)
project(FNA3D C)
option(BUILD_SHARED_LIBS \"Build shared library\" ON)
add_definitions(-DMOJOSHADER_USE_SDL_STDLIB -DUSE_SDL3 -DSUPPORT_PROFILE_BYTECODE=0)
add_library(FNA3D src/FNA3D.c)
add_library(mojoshader STATIC MojoShader/mojoshader.c MojoShader/mojoshader_sdlgpu.c)
target_link_libraries(FNA3D PRIVATE mojoshader)
")
file(WRITE "${_fake}/src/FNA3D.c" "int fna3d_version(void) { return 1; }\n")
file(WRITE "${_fake}/MojoShader/mojoshader.h" "int mojoshader_version(void);\n")
file(WRITE "${_fake}/MojoShader/mojoshader.c" "int mojoshader_version(void) { return 1; }\n")
file(WRITE "${_fake}/MojoShader/mojoshader_sdlgpu.c" "#error the SDL-free build drops this file\n")
file(WRITE "${_fake}/MojoShader/profiles/mojoshader_profile_bytecode.c" "int mojoshader_bytecode;\n")

set(_consumer "${_w}/consumer")
file(WRITE "${_consumer}/CMakeLists.txt" "cmake_minimum_required(VERSION 3.20)
project(CnaFna3dDependencyShape C)
if(FIXTURE_NEW_POLICY)
    cmake_policy(SET CMP0126 NEW)
endif()
if(FIXTURE_NORMAL_SHARED)
    set(BUILD_SHARED_LIBS ON)
endif()
set(CNA_ENABLE_SDL OFF)
include(\"\${CNA_SOURCE_DIR}/cmake/ThirdPartyFNA3D.cmake\")
cna_configure_mojoshader()
FetchContent_GetProperties(fna3d)
get_target_property(_type FNA3D TYPE)
get_property(_excluded DIRECTORY \"\${fna3d_SOURCE_DIR}\" PROPERTY EXCLUDE_FROM_ALL)
set(_cache absent)
if(DEFINED CACHE{BUILD_SHARED_LIBS})
    get_property(_cache_type CACHE BUILD_SHARED_LIBS PROPERTY TYPE)
    get_property(_cache_help CACHE BUILD_SHARED_LIBS PROPERTY HELPSTRING)
    set(_cache \"\$CACHE{BUILD_SHARED_LIBS}|\${_cache_type}|\${_cache_help}\")
endif()
file(WRITE \"\${CMAKE_BINARY_DIR}/shape.txt\"
    \"type=\${_type}\\nexcluded=\${_excluded}\\ncache=\${_cache}\\nvisible=\${BUILD_SHARED_LIBS}\\n\")
")

# CMake before 3.28 added a FetchContent dependency with a plain add_subdirectory(); so does a
# dependency provider that populates it itself.
set(_provider "${_w}/provider.cmake")
file(WRITE "${_provider}" "include(FetchContent)
macro(cna_fixture_provide method name)
    if(\"\${name}\" STREQUAL \"fna3d\")
        add_subdirectory(\"${_fake}\" \"\${CMAKE_BINARY_DIR}/_deps/fna3d-build\")
        FetchContent_SetPopulated(fna3d SOURCE_DIR \"${_fake}\"
            BINARY_DIR \"\${CMAKE_BINARY_DIR}/_deps/fna3d-build\")
    endif()
endmacro()
cmake_language(SET_DEPENDENCY_PROVIDER cna_fixture_provide
    SUPPORTED_METHODS FETCHCONTENT_MAKEAVAILABLE_SERIAL)
")

set(_generator "")
if(DEFINED CNA_GENERATOR)
    set(_generator -G "${CNA_GENERATOR}")
endif()
set(_failures 0)

# _shape(<label> <case> <expected type> <expected excluded> <expected cache> <expected visible>
#        [cmake args...])
function(_shape label case type excluded cache visible)
    set(_bin "${_w}/${case}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -S "${_consumer}" -B "${_bin}" ${_generator}
            "-DCNA_SOURCE_DIR=${CNA_SOURCE_DIR}" ${ARGN}
        RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
    if(NOT _rc EQUAL 0)
        message(SEND_ERROR "Fna3dDependencyShapeCase: ${label}: configure failed:\n${_out}${_err}")
        set(_failures_here 1 PARENT_SCOPE)
        return()
    endif()
    file(STRINGS "${_bin}/shape.txt" _lines)
    set(_want "type=${type}" "excluded=${excluded}" "cache=${cache}" "visible=${visible}")
    if(NOT _lines STREQUAL _want)
        string(REPLACE ";" "\n  " _got_text "${_lines}")
        string(REPLACE ";" "\n  " _want_text "${_want}")
        message(SEND_ERROR "Fna3dDependencyShapeCase: ${label}:\n got\n  ${_got_text}\n expected\n  ${_want_text}")
        set(_failures_here 1 PARENT_SCOPE)
        return()
    endif()
    if(CNA_GENERATOR STREQUAL "Ninja")
        # The generated `all` itself: nothing of FNA3D's may be in it.
        execute_process(COMMAND "${CMAKE_COMMAND}" --build "${_bin}" -- -n
            RESULT_VARIABLE _rc OUTPUT_VARIABLE _plan ERROR_VARIABLE _plan_err)
        if(NOT _rc EQUAL 0 OR _plan MATCHES "FNA3D\\.c|mojoshader\\.c")
            message(SEND_ERROR "Fna3dDependencyShapeCase: ${label}: `all` builds FNA3D:\n${_plan}${_plan_err}")
            set(_failures_here 1 PARENT_SCOPE)
            return()
        endif()
    endif()
    message(STATUS "ok: ${label}")
    set(_failures_here 0 PARENT_SCOPE)
endfunction()

set(_cmdline "No help, variable specified on the command line.")
set(_override "-DFETCHCONTENT_SOURCE_DIR_FNA3D=${_fake}")
_shape("no BUILD_SHARED_LIBS anywhere: FNA3D static, nothing left behind" default
    STATIC_LIBRARY TRUE absent "" "${_override}")
math(EXPR _failures "${_failures} + ${_failures_here}")
_shape("typed cache entry ON: FNA3D static, entry restored with its type and docstring" cache-typed
    STATIC_LIBRARY TRUE "ON|BOOL|${_cmdline}" ON "${_override}" -DBUILD_SHARED_LIBS:BOOL=ON)
math(EXPR _failures "${_failures} + ${_failures_here}")
_shape("untyped cache entry ON: FNA3D static, entry restored untyped" cache-untyped
    STATIC_LIBRARY TRUE "ON|UNINITIALIZED|${_cmdline}" ON "${_override}" -DBUILD_SHARED_LIBS=ON)
math(EXPR _failures "${_failures} + ${_failures_here}")
_shape("normal variable ON under CMP0126 NEW: FNA3D static, no cache entry created" normal-new
    STATIC_LIBRARY TRUE absent ON "${_override}" -DFIXTURE_NORMAL_SHARED=ON -DFIXTURE_NEW_POLICY=ON)
math(EXPR _failures "${_failures} + ${_failures_here}")
_shape("normal variable ON under CMP0126 OLD: FNA3D static, no cache entry created" normal-old
    STATIC_LIBRARY TRUE absent ON "${_override}" -DFIXTURE_NORMAL_SHARED=ON)
math(EXPR _failures "${_failures} + ${_failures_here}")

_shape("FNA3D added without EXCLUDE_FROM_ALL (pre-3.28 FetchContent, a provider): still out of all"
    provider STATIC_LIBRARY TRUE absent ""
    "-DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=${_provider}" -DCNA_FNA3D_ALLOW_SOURCE_DRIFT=ON)
math(EXPR _failures "${_failures} + ${_failures_here}")

if(_failures GREATER 0)
    message(FATAL_ERROR "Fna3dDependencyShapeCase: ${_failures} case(s) failed")
endif()
file(REMOVE_RECURSE "${_w}")
message(STATUS "Fna3dDependencyShapeCase: all cases passed")
