# Build manifests for the persistent vendored-SDL installs (CNA_SDL_PREBUILT_ROOT).
#
# Those installs live outside every build tree and are shared by all of them, so the question
# "is the library already there?" is not enough to decide whether it may be reused: the vendored
# source can move to another revision, and CNA can add, change or drop a patch on top of it, while
# the old library keeps sitting there. A test run would then link the old SDL while the tree says
# the new one -- a validation that proves nothing.
#
# Each dependency therefore records, next to its install, the manifest of everything that went into
# it: the content of its source tree, the content of every patch applied on top, the CMake
# arguments it was configured with, and the content of its toolchain file. The install is reused only when the recorded manifest equals the
# one computed now. Nothing here is specific to one dependency or one patch.
#
# Included by cmake/ThirdPartySDL.cmake, and by cmake/Tests/SdlPrebuiltFingerprint.cmake, which
# exercises these functions directly.

include_guard(GLOBAL)

# Bump when the manifest's format or the meaning of its fields changes, so every existing stamp
# stops matching.
set(CNA_SDL_PREBUILT_MANIFEST_SCHEMA "cna-sdl-prebuilt-manifest-v1")

# Content hash of a source tree: SHA-256 over the sorted list of (relative path, file SHA-256).
# A revision string would miss a working tree that was edited or partly updated; the content cannot.
# About 0.2 s for SDL's 2 200 files, so it is cheap enough for every configure.
function(cna_sdl_source_tree_hash out_var source_dir)
    file(GLOB_RECURSE _files LIST_DIRECTORIES false FOLLOW_SYMLINKS
        RELATIVE "${source_dir}" "${source_dir}/*")
    list(FILTER _files EXCLUDE REGEX "(^|/)\\.git(/|$)")
    list(SORT _files)
    set(_listing "")
    foreach(_file IN LISTS _files)
        file(SHA256 "${source_dir}/${_file}" _file_hash)
        string(APPEND _listing "${_file} ${_file_hash}\n")
    endforeach()
    string(SHA256 _tree_hash "${_listing}")
    set(${out_var} "${_tree_hash}" PARENT_SCOPE)
endfunction()

# The settings a toolchain decides, as "<variable>=<value>" for cna_sdl_build_manifest's SETTINGS:
# the flags and target it initializes, the system and sysroot it names, the platform and ABI of an
# Android or Apple SDK. They are read from the calling scope, where the toolchain set them -- also
# when a file the toolchain includes did, which the toolchain file's own hash cannot see.
function(cna_sdl_toolchain_settings out_var)
    set(_settings "")
    foreach(_variable IN ITEMS
            CMAKE_SYSTEM_NAME CMAKE_SYSTEM_PROCESSOR CMAKE_SYSTEM_VERSION CMAKE_SYSROOT
            CMAKE_C_COMPILER CMAKE_C_COMPILER_TARGET CMAKE_CXX_COMPILER_TARGET
            CMAKE_C_FLAGS_INIT CMAKE_CXX_FLAGS_INIT
            CMAKE_EXE_LINKER_FLAGS_INIT CMAKE_SHARED_LINKER_FLAGS_INIT CMAKE_MODULE_LINKER_FLAGS_INIT
            CMAKE_ANDROID_ARCH_ABI CMAKE_ANDROID_API ANDROID_ABI ANDROID_PLATFORM ANDROID_STL
            CMAKE_OSX_SYSROOT CMAKE_OSX_ARCHITECTURES CMAKE_OSX_DEPLOYMENT_TARGET)
        if(DEFINED ${_variable})
            list(APPEND _settings "${_variable}=${${_variable}}")
        endif()
    endforeach()
    set(${out_var} "${_settings}" PARENT_SCOPE)
endfunction()

# cna_sdl_build_manifest(<out-var> NAME <n> SOURCE <dir> [PATCHES <file>...] [ARGS <arg>...]
#                        [TOOLCHAIN_FILE <file> [COMPILER <identity>] [SETTINGS <var=value>...]])
#
# Patches are listed in application order, by file name and content hash: reordering, renaming,
# editing, adding or removing one each changes the manifest.
#
# A toolchain file is named in ARGS by its path only, and editing it in place -- another compiler,
# sysroot or ABI flag -- would leave the manifest as it was (2026-10-26 dependency audit C-09). So
# its content hash is recorded too, and with it the compiler identity the toolchain resolved to in
# this configure (id, version, path), which also changes when a toolchain picks its SDK or NDK from
# outside the file, and the settings the toolchain decided (cna_sdl_toolchain_settings), which also
# change when a file the toolchain includes is edited -- an ABI flag or a sysroot in a shared
# settings file. Without a toolchain file none of these lines is written, so a native manifest is
# exactly what it was before.
function(cna_sdl_build_manifest out_var)
    cmake_parse_arguments(_M "" "NAME;SOURCE;TOOLCHAIN_FILE;COMPILER" "PATCHES;ARGS;SETTINGS" ${ARGN})
    cna_sdl_source_tree_hash(_source_hash "${_M_SOURCE}")
    set(_manifest "schema ${CNA_SDL_PREBUILT_MANIFEST_SCHEMA}\n")
    string(APPEND _manifest "name ${_M_NAME}\n")
    string(APPEND _manifest "source-tree-sha256 ${_source_hash}\n")
    foreach(_patch IN LISTS _M_PATCHES)
        if(NOT EXISTS "${_patch}")
            message(FATAL_ERROR "CNA: ${_M_NAME} patch ${_patch} does not exist")
        endif()
        get_filename_component(_patch_name "${_patch}" NAME)
        file(SHA256 "${_patch}" _patch_hash)
        string(APPEND _manifest "patch ${_patch_name} ${_patch_hash}\n")
    endforeach()
    foreach(_arg IN LISTS _M_ARGS)
        string(APPEND _manifest "arg ${_arg}\n")
    endforeach()
    if(DEFINED _M_TOOLCHAIN_FILE AND NOT _M_TOOLCHAIN_FILE STREQUAL "")
        if(EXISTS "${_M_TOOLCHAIN_FILE}")
            file(SHA256 "${_M_TOOLCHAIN_FILE}" _toolchain_hash)
        else()
            set(_toolchain_hash "absent")
        endif()
        string(APPEND _manifest "toolchain-sha256 ${_toolchain_hash}\n")
        if(DEFINED _M_COMPILER AND NOT _M_COMPILER STREQUAL "")
            string(APPEND _manifest "toolchain-compiler ${_M_COMPILER}\n")
        endif()
        foreach(_setting IN LISTS _M_SETTINGS)
            string(APPEND _manifest "toolchain-setting ${_setting}\n")
        endforeach()
    endif()
    set(${out_var} "${_manifest}" PARENT_SCOPE)
endfunction()

# Sets <out-var> to TRUE when <library> exists and <stamp> holds exactly <manifest>, and puts the
# reason for a rebuild in <reason-var> otherwise. An install without a stamp -- every install made
# before manifests existed -- is rebuilt once, since nothing records what it was built from.
function(cna_sdl_prebuilt_is_current out_var reason_var)
    cmake_parse_arguments(_C "" "LIBRARY;STAMP;MANIFEST" "" ${ARGN})
    if(NOT EXISTS "${_C_LIBRARY}")
        set(${out_var} FALSE PARENT_SCOPE)
        set(${reason_var} "${_C_LIBRARY} is absent" PARENT_SCOPE)
        return()
    endif()
    if(NOT EXISTS "${_C_STAMP}")
        set(${out_var} FALSE PARENT_SCOPE)
        set(${reason_var} "the install has no build manifest, so what it was built from is unknown"
            PARENT_SCOPE)
        return()
    endif()
    file(READ "${_C_STAMP}" _recorded)
    if(NOT _recorded STREQUAL _C_MANIFEST)
        set(${out_var} FALSE PARENT_SCOPE)
        set(${reason_var} "its source, patches or configuration changed since it was built"
            PARENT_SCOPE)
        return()
    endif()
    set(${out_var} TRUE PARENT_SCOPE)
    set(${reason_var} "" PARENT_SCOPE)
endfunction()

# Every tree on the machine shares one install per prebuilt root, and another tree's configure may
# rebuild it for a different manifest. Making the stamp an input of this tree's own configure means
# its next build re-runs that configure first, which compares the manifests again and rebuilds the
# install this tree accepted, instead of linking the other tree's (2026-10-26 dependency audit
# C-08). Only a rebuild rewrites the stamp, so trees with the same manifest are never disturbed.
function(cna_sdl_prebuilt_watch stamp)
    set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${stamp}")
endfunction()

# Copies <source> to <destination> and applies <patches> there, in order. The vendored tree itself
# is never modified, so a checkout stays pristine and every build tree reads the same bytes.
#
# GIT_CEILING_DIRECTORIES stops git from discovering an enclosing repository -- the default prebuilt
# root sits inside CNA's own checkout, and `git apply` run inside a repository resolves the patch's
# paths against that repository's root rather than the staged tree.
function(cna_sdl_stage_patched_source)
    cmake_parse_arguments(_S "" "SOURCE;DESTINATION" "PATCHES" ${ARGN})
    find_program(CNA_SDL_PATCH_GIT_EXECUTABLE git)
    if(NOT CNA_SDL_PATCH_GIT_EXECUTABLE)
        message(FATAL_ERROR "CNA: git not found -- required to apply the vendored-SDL patches")
    endif()
    file(REMOVE_RECURSE "${_S_DESTINATION}")
    file(MAKE_DIRECTORY "${_S_DESTINATION}")
    file(COPY "${_S_SOURCE}/" DESTINATION "${_S_DESTINATION}" PATTERN ".git" EXCLUDE)
    get_filename_component(_ceiling "${_S_DESTINATION}" DIRECTORY)
    foreach(_patch IN LISTS _S_PATCHES)
        get_filename_component(_patch_name "${_patch}" NAME)
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E env "GIT_CEILING_DIRECTORIES=${_ceiling}"
                    "${CNA_SDL_PATCH_GIT_EXECUTABLE}" apply --whitespace=nowarn "${_patch}"
            WORKING_DIRECTORY "${_S_DESTINATION}"
            RESULT_VARIABLE _rc
            ERROR_VARIABLE _err)
        if(NOT _rc EQUAL 0)
            file(REMOVE_RECURSE "${_S_DESTINATION}")
            message(FATAL_ERROR
                "CNA: ${_patch_name} does not apply to ${_S_SOURCE}:\n${_err}"
                "The patch series is pinned to one vendored revision; update both together.")
        endif()
        # `git apply` inside a repository that does not contain the patched paths exits 0 having
        # changed nothing, so success is proven by the patch now applying in reverse.
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E env "GIT_CEILING_DIRECTORIES=${_ceiling}"
                    "${CNA_SDL_PATCH_GIT_EXECUTABLE}" apply --reverse --check "${_patch}"
            WORKING_DIRECTORY "${_S_DESTINATION}"
            RESULT_VARIABLE _rc
            OUTPUT_QUIET ERROR_QUIET)
        if(NOT _rc EQUAL 0)
            file(REMOVE_RECURSE "${_S_DESTINATION}")
            message(FATAL_ERROR "CNA: ${_patch_name} reported success but is not in the staged tree")
        endif()
        message(STATUS "CNA: applied ${_patch_name}")
    endforeach()
endfunction()
