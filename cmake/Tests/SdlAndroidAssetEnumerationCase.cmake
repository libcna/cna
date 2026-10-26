# SPDX-License-Identifier: MS-PL
#
# 2026-10-26 dependency audit, finding C-05: cmake/patches/sdl-86296ac8-0004-android-relative-asset-
# enumeration.patch makes SDL_EnumerateDirectory fall back to the APK's assets for a relative
# directory again ("./Content", TitleLocation.Path's "./" plus a folder), which SDL 86296ac8 only
# does for an explicit "assets://" URL, and still never for an absolute path.
#
# No Android device is needed: SDL_SYS_EnumerateDirectory is taken verbatim from the vendored
# SDL_sysfsops.c, before and after the patch, and compiled on the host with SDL_PLATFORM_ANDROID
# defined and the two Android services it calls replaced by stubs -- internal storage is a
# directory that does not exist, and the asset enumeration only counts its calls. Each build lists
# three directories:
#
#   ./Content          relative, absent from internal storage   patched: assets, unpatched: none
#   assets://Content   explicit asset URL                        assets either way
#   <absolute>         absolute, absent                          never assets (opendir's own error)
#
#   cmake -DCNA_SOURCE_DIR=<repo> -DCNA_WORK_DIR=<dir> -P cmake/Tests/SdlAndroidAssetEnumerationCase.cmake

cmake_minimum_required(VERSION 3.20)

foreach(_required IN ITEMS CNA_SOURCE_DIR CNA_WORK_DIR)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR "SdlAndroidAssetEnumerationCase.cmake requires -D${_required}=...")
    endif()
endforeach()

set(_source "${CNA_SOURCE_DIR}/third_party/SDL/src/filesystem/posix/SDL_sysfsops.c")
set(_patch "${CNA_SOURCE_DIR}/cmake/patches/sdl-86296ac8-0004-android-relative-asset-enumeration.patch")
foreach(_input IN ITEMS "${_source}" "${_patch}")
    if(NOT EXISTS "${_input}")
        message(FATAL_ERROR "SdlAndroidAssetEnumerationCase: ${_input} does not exist")
    endif()
endforeach()
find_program(_cc NAMES cc gcc clang)
find_program(_git NAMES git)
if(NOT _cc OR NOT _git)
    message(FATAL_ERROR "SdlAndroidAssetEnumerationCase: needs a host C compiler and git")
endif()

file(REMOVE_RECURSE "${CNA_WORK_DIR}")
file(MAKE_DIRECTORY "${CNA_WORK_DIR}/tree/src/filesystem/posix")
file(COPY "${_source}" DESTINATION "${CNA_WORK_DIR}/tree/src/filesystem/posix")
# GIT_CEILING_DIRECTORIES keeps git apply from resolving the patch against an enclosing repository.
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "GIT_CEILING_DIRECTORIES=${CNA_WORK_DIR}"
        "${_git}" apply "${_patch}"
    WORKING_DIRECTORY "${CNA_WORK_DIR}/tree"
    RESULT_VARIABLE _applied
    ERROR_VARIABLE _apply_error)
if(NOT _applied EQUAL 0)
    message(FATAL_ERROR "SdlAndroidAssetEnumerationCase: the patch does not apply:\n${_apply_error}")
endif()

set(_head [=[
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define SDL_PLATFORM_ANDROID 1
#define SDL_asprintf asprintf
#define SDL_strlen strlen
#define SDL_strcmp strcmp
#define SDL_strncmp strncmp
#define SDL_free free
#define SDL_SetError(...) false
typedef enum { SDL_ENUM_CONTINUE, SDL_ENUM_SUCCESS, SDL_ENUM_FAILURE } SDL_EnumerationResult;
typedef SDL_EnumerationResult (*SDL_EnumerateDirectoryCallback)(void *, const char *, const char *);
static const char *internal_storage;
static int asset_calls;
static const char *SDL_GetAndroidInternalStoragePath(void) { return internal_storage; }
static bool Android_JNI_EnumerateAssetDirectory(const char *path, SDL_EnumerateDirectoryCallback cb,
                                                void *userdata)
{
    (void) path; (void) cb; (void) userdata;
    asset_calls++;
    return true;
}
]=])
set(_tail [=[
int main(int argc, char **argv)
{
    const char *paths[3] = { "./Content", "assets://Content", NULL };
    int i;
    (void) argc;
    internal_storage = argv[1];
    paths[2] = argv[1];
    for (i = 0; i < 3; i++) {
        bool ok;
        asset_calls = 0;
        ok = SDL_SYS_EnumerateDirectory(paths[i], NULL, NULL);
        printf("%d ok=%d assets=%d\n", i, ok ? 1 : 0, asset_calls);
    }
    return 0;
}
]=])

# _enumerate(<label> <source file>) -> _output: the three lines the build printed.
function(_enumerate label file)
    file(READ "${file}" _text)
    string(FIND "${_text}" "bool SDL_SYS_EnumerateDirectory(" _begin)
    string(FIND "${_text}" "\nbool SDL_SYS_RemovePath(" _end)
    if(_begin EQUAL -1 OR _end EQUAL -1 OR NOT _end GREATER _begin)
        message(FATAL_ERROR "SdlAndroidAssetEnumerationCase: SDL_SYS_EnumerateDirectory not found in ${file}")
    endif()
    math(EXPR _length "${_end} - ${_begin}")
    string(SUBSTRING "${_text}" ${_begin} ${_length} _function)
    file(WRITE "${CNA_WORK_DIR}/${label}.c" "${_head}${_function}\n${_tail}")
    execute_process(
        COMMAND "${_cc}" -Wall -o "${CNA_WORK_DIR}/${label}" "${CNA_WORK_DIR}/${label}.c"
        RESULT_VARIABLE _compiled
        ERROR_VARIABLE _compile_error)
    if(NOT _compiled EQUAL 0)
        message(FATAL_ERROR "SdlAndroidAssetEnumerationCase: ${label} does not compile:\n${_compile_error}")
    endif()
    execute_process(
        COMMAND "${CNA_WORK_DIR}/${label}" "${CNA_WORK_DIR}/absent-internal-storage"
        RESULT_VARIABLE _ran
        OUTPUT_VARIABLE _stdout)
    if(NOT _ran EQUAL 0)
        message(FATAL_ERROR "SdlAndroidAssetEnumerationCase: ${label} exited with ${_ran}")
    endif()
    set(_output "${_stdout}" PARENT_SCOPE)
endfunction()

set(_failures 0)
macro(_expect label output want)
    if("${output}" STREQUAL "${want}")
        message(STATUS "ok: ${label}")
    else()
        message(SEND_ERROR "SdlAndroidAssetEnumerationCase: ${label}: expected\n${want}got\n${output}")
        math(EXPR _failures "${_failures} + 1")
    endif()
endmacro()

_enumerate(patched "${CNA_WORK_DIR}/tree/src/filesystem/posix/SDL_sysfsops.c")
_expect("patched: a relative directory falls back to the assets, an asset URL reads them, an absolute path does not"
    "${_output}" "0 ok=1 assets=1\n1 ok=1 assets=1\n2 ok=0 assets=0\n")
_enumerate(unpatched "${_source}")
_expect("unpatched SDL, the defect the patch carries: a relative directory does not reach the assets"
    "${_output}" "0 ok=0 assets=0\n1 ok=1 assets=1\n2 ok=0 assets=0\n")

if(_failures GREATER 0)
    message(FATAL_ERROR "SdlAndroidAssetEnumerationCase: ${_failures} case(s) failed")
endif()
message(STATUS "SdlAndroidAssetEnumerationCase: all cases passed")
