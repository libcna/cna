# SPDX-License-Identifier: MS-PL

include_guard(GLOBAL)

# Keep build-speed controls in one file rather than scattering raw compiler flags through
# presets and module CMakeLists. Public ABI requirements remain in CNA::BuildConfig; this file
# owns only project-local diagnostics, instrumentation, linker selection, and optional IPO.

option(CNA_EXPORT_COMPILE_COMMANDS
    "Generate compile_commands.json with Ninja and Makefile generators" ON)
if(CNA_EXPORT_COMPILE_COMMANDS AND CMAKE_GENERATOR MATCHES "Ninja|Makefiles")
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL
        "Export compilation commands for tooling" FORCE)
endif()

add_library(cna_linker_options INTERFACE)
add_library(CNA::LinkerOptions ALIAS cna_linker_options)

set(CNA_LINKER "AUTO" CACHE STRING
    "Linker for supported native builds (AUTO, DEFAULT, LLD, or MOLD)")
set_property(CACHE CNA_LINKER PROPERTY STRINGS AUTO DEFAULT LLD MOLD)

function(cna_configure_linker)
    string(TOUPPER "${CNA_LINKER}" _cna_linker_requested)
    if(NOT _cna_linker_requested MATCHES "^(AUTO|DEFAULT|LLD|MOLD)$")
        message(FATAL_ERROR
            "CNA_LINKER must be AUTO, DEFAULT, LLD, or MOLD (received '${CNA_LINKER}').")
    endif()

    # These linker driver flags are ELF-specific. Do not guess an equivalent for Apple, MSVC,
    # Windows cross-builds, Android, or Emscripten.
    if(_cna_linker_requested STREQUAL "DEFAULT")
        message(STATUS "CNA: using the toolchain default linker")
        return()
    endif()
    if(NOT UNIX OR APPLE OR WIN32 OR ANDROID OR EMSCRIPTEN OR CMAKE_CROSSCOMPILING OR
       NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        if(_cna_linker_requested STREQUAL "AUTO")
            message(STATUS "CNA: fast linker selection is unavailable for this toolchain; using default linker")
            return()
        endif()
        message(FATAL_ERROR
            "CNA_LINKER=${_cna_linker_requested} is supported only by native GNU/Clang ELF builds. "
            "Use CNA_LINKER=DEFAULT for this toolchain.")
    endif()

    set(_cna_linker_candidates)
    if(_cna_linker_requested STREQUAL "AUTO")
        # Mold is normally the faster option when both are installed. The probe below still
        # decides whether the selected compiler can actually drive it.
        set(_cna_linker_candidates MOLD LLD)
    else()
        set(_cna_linker_candidates "${_cna_linker_requested}")
    endif()

    include(CheckLinkerFlag)
    foreach(_cna_linker_candidate IN LISTS _cna_linker_candidates)
        if(_cna_linker_candidate STREQUAL "MOLD")
            find_program(_cna_linker_program NAMES mold)
            set(_cna_linker_driver_name mold)
        else()
            find_program(_cna_linker_program NAMES ld.lld lld)
            set(_cna_linker_driver_name lld)
        endif()

        if(NOT _cna_linker_program)
            continue()
        endif()

        string(TOLOWER "${_cna_linker_candidate}" _cna_linker_candidate_lower)
        check_linker_flag(CXX "-fuse-ld=${_cna_linker_driver_name}"
            "CNA_LINKER_${_cna_linker_candidate}_SUPPORTED")
        if(CNA_LINKER_${_cna_linker_candidate}_SUPPORTED)
            target_link_options(cna_linker_options INTERFACE
                "-fuse-ld=${_cna_linker_driver_name}")
            message(STATUS
                "CNA: using ${_cna_linker_candidate_lower} linker (${_cna_linker_program})")
            return()
        endif()
    endforeach()

    if(_cna_linker_requested STREQUAL "AUTO")
        message(STATUS "CNA: no supported fast linker found; using the toolchain default linker")
    else()
        message(FATAL_ERROR
            "CNA_LINKER=${_cna_linker_requested} was requested but is unavailable or rejected by "
            "${CMAKE_CXX_COMPILER_ID}. Install the requested linker or use CNA_LINKER=DEFAULT.")
    endif()
endfunction()

add_library(cna_emscripten_abi INTERFACE)
add_library(CNA::EmscriptenAbi ALIAS cna_emscripten_abi)
if(EMSCRIPTEN)
    # Every C++ frame that can propagate a CNA exception must use the same JS-lowered exception
    # ABI. Keep this target-scoped so ordinary native builds never inherit Emscripten flags.
    target_compile_options(cna_emscripten_abi INTERFACE -fexceptions)
    target_link_options(cna_emscripten_abi INTERFACE
        -fexceptions
        -sDISABLE_EXCEPTION_CATCHING=0
        -sASYNCIFY=1
    )
endif()

add_library(cna_instrumentation INTERFACE)
add_library(CNA::Instrumentation ALIAS cna_instrumentation)

set(CNA_SANITIZE "" CACHE STRING
    "Comma-separated sanitizers to enable (for example address,undefined); empty disables")
set(CNA_SANITIZE_OPTIMIZATION "DEFAULT" CACHE STRING
    "Optimization override for sanitizer builds (DEFAULT, O0, O1, O2, or O3)")
set_property(CACHE CNA_SANITIZE_OPTIMIZATION PROPERTY STRINGS DEFAULT O0 O1 O2 O3)

function(cna_configure_instrumentation)
    if(NOT CNA_SANITIZE)
        return()
    endif()

    if(EMSCRIPTEN)
        message(FATAL_ERROR
            "CNA_SANITIZE is not supported by the current Emscripten build. Use a native sanitizer "
            "preset; WebAssembly instrumentation requires a separately verified toolchain path.")
    endif()
    if(MSVC OR NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        message(FATAL_ERROR
            "CNA_SANITIZE=${CNA_SANITIZE} requires a GNU or Clang-family compiler in this build system.")
    endif()

    string(REPLACE "," ";" _cna_sanitizer_list "${CNA_SANITIZE}")
    foreach(_cna_sanitizer IN LISTS _cna_sanitizer_list)
        if(_cna_sanitizer STREQUAL "")
            message(FATAL_ERROR "CNA_SANITIZE contains an empty sanitizer name.")
        endif()
    endforeach()
    list(FIND _cna_sanitizer_list address _cna_address_sanitizer_index)
    list(FIND _cna_sanitizer_list thread _cna_thread_sanitizer_index)
    list(FIND _cna_sanitizer_list memory _cna_memory_sanitizer_index)
    if(_cna_address_sanitizer_index GREATER -1 AND _cna_thread_sanitizer_index GREATER -1)
        message(FATAL_ERROR "AddressSanitizer and ThreadSanitizer cannot be enabled together.")
    endif()
    if(_cna_thread_sanitizer_index GREATER -1 AND _cna_memory_sanitizer_index GREATER -1)
        message(FATAL_ERROR "ThreadSanitizer and MemorySanitizer cannot be enabled together.")
    endif()

    string(TOUPPER "${CNA_SANITIZE_OPTIMIZATION}" _cna_sanitize_optimization)
    if(NOT _cna_sanitize_optimization MATCHES "^(DEFAULT|O0|O1|O2|O3)$")
        message(FATAL_ERROR
            "CNA_SANITIZE_OPTIMIZATION must be DEFAULT, O0, O1, O2, or O3 "
            "(received '${CNA_SANITIZE_OPTIMIZATION}').")
    endif()

    target_compile_options(cna_instrumentation INTERFACE
        "-fsanitize=${CNA_SANITIZE}"
        -fno-omit-frame-pointer
        -g)
    target_link_options(cna_instrumentation INTERFACE "-fsanitize=${CNA_SANITIZE}")
    if(NOT _cna_sanitize_optimization STREQUAL "DEFAULT")
        target_compile_options(cna_instrumentation INTERFACE "-${_cna_sanitize_optimization}")
    endif()
    message(STATUS
        "CNA: sanitizers enabled: ${CNA_SANITIZE} "
        "(optimization: ${_cna_sanitize_optimization})")
endfunction()

function(cna_link_private_build_support target)
    get_target_property(_cna_target_type "${target}" TYPE)
    if(_cna_target_type STREQUAL "INTERFACE_LIBRARY")
        target_link_libraries("${target}" INTERFACE ${ARGN})
    else()
        target_link_libraries("${target}" PRIVATE ${ARGN})
    endif()
endfunction()

function(cna_apply_sharp_runtime_build_support)
    set(_cna_runtime_support cna_instrumentation cna_emscripten_abi)
    if(CNA_SHARP_RUNTIME_IS_MODULAR)
        sharp_runtime_get_enabled_components(_cna_sharp_runtime_components)
        set(_cna_sharp_runtime_targets)
        foreach(_cna_component IN LISTS _cna_sharp_runtime_components)
            set(_cna_component_alias "SharpRuntime::${_cna_component}")
            if(TARGET "${_cna_component_alias}")
                get_target_property(_cna_component_target
                    "${_cna_component_alias}" ALIASED_TARGET)
                if(NOT _cna_component_target)
                    set(_cna_component_target "${_cna_component_alias}")
                endif()
                list(APPEND _cna_sharp_runtime_targets "${_cna_component_target}")
            endif()
        endforeach()
        list(REMOVE_DUPLICATES _cna_sharp_runtime_targets)
        foreach(_cna_component_target IN LISTS _cna_sharp_runtime_targets)
            cna_link_private_build_support("${_cna_component_target}" ${_cna_runtime_support})
        endforeach()
    elseif(TARGET SHARP_RUNTIME)
        cna_link_private_build_support(SHARP_RUNTIME ${_cna_runtime_support})
    endif()
endfunction()

function(cna_collect_buildsystem_targets directory output_variable)
    get_property(_cna_targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    get_property(_cna_subdirectories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(_cna_subdirectory IN LISTS _cna_subdirectories)
        cna_collect_buildsystem_targets("${_cna_subdirectory}" _cna_child_targets)
        list(APPEND _cna_targets ${_cna_child_targets})
    endforeach()
    list(REMOVE_DUPLICATES _cna_targets)
    set("${output_variable}" "${_cna_targets}" PARENT_SCOPE)
endfunction()

function(cna_is_owned_target target output_variable)
    get_target_property(_cna_target_source_dir "${target}" SOURCE_DIR)
    if(NOT _cna_target_source_dir)
        set("${output_variable}" FALSE PARENT_SCOPE)
        return()
    endif()

    file(RELATIVE_PATH _cna_target_relative_source_dir
        "${CMAKE_SOURCE_DIR}" "${_cna_target_source_dir}")
    if(_cna_target_relative_source_dir MATCHES "^\\.\\." OR
       IS_ABSOLUTE "${_cna_target_relative_source_dir}" OR
       _cna_target_relative_source_dir MATCHES "^(third_party|vendor)(/|$)")
        set("${output_variable}" FALSE PARENT_SCOPE)
        return()
    endif()
    set("${output_variable}" TRUE PARENT_SCOPE)
endfunction()

function(cna_apply_build_support_to_cna_targets)
    if(NOT TARGET cna_project_options)
        message(FATAL_ERROR "CNA project build options must exist before applying target support.")
    endif()

    cna_collect_buildsystem_targets("${CMAKE_SOURCE_DIR}" _cna_all_targets)
    set(_cna_supported_target_count 0)
    foreach(_cna_target IN LISTS _cna_all_targets)
        cna_is_owned_target("${_cna_target}" _cna_target_is_owned)
        if(NOT _cna_target_is_owned)
            continue()
        endif()
        get_target_property(_cna_target_type "${_cna_target}" TYPE)
        if(NOT _cna_target_type MATCHES
           "^(EXECUTABLE|STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY)$")
            continue()
        endif()
        cna_link_private_build_support("${_cna_target}"
            cna_project_options
            cna_instrumentation
            cna_emscripten_abi
            cna_linker_options)
        math(EXPR _cna_supported_target_count "${_cna_supported_target_count} + 1")
    endforeach()
    message(STATUS
        "CNA: applied private compiler/linker support to ${_cna_supported_target_count} owned targets")
endfunction()

option(CNA_ENABLE_IPO
    "Enable IPO/LTO for CNA-owned release artifacts (not compatible with the static C API package)" OFF)

function(cna_apply_ipo_to_cna_targets)
    if(NOT CNA_ENABLE_IPO)
        return()
    endif()
    if(CNA_SANITIZE)
        message(FATAL_ERROR "CNA_ENABLE_IPO cannot be combined with CNA_SANITIZE.")
    endif()
    if(EMSCRIPTEN OR CMAKE_CROSSCOMPILING)
        message(FATAL_ERROR
            "CNA_ENABLE_IPO is currently supported only by verified native builds; disable it for "
            "Emscripten and cross-compiles.")
    endif()
    if(CNA_BUILD_C_API AND CNA_C_API_BUILD_STATIC)
        message(FATAL_ERROR
            "CNA_ENABLE_IPO cannot package CNA_C_API_BUILD_STATIC=ON: the generated archive would "
            "require downstream consumers to use a compatible LTO linker. Set "
            "CNA_C_API_BUILD_STATIC=OFF or disable CNA_ENABLE_IPO.")
    endif()

    if(CMAKE_CONFIGURATION_TYPES)
        set(_cna_ipo_configurations RELEASE RELWITHDEBINFO MINSIZEREL)
    elseif(CMAKE_BUILD_TYPE MATCHES "^(Release|RelWithDebInfo|MinSizeRel)$")
        set(_cna_ipo_configurations "")
    else()
        message(FATAL_ERROR
            "CNA_ENABLE_IPO requires Release, RelWithDebInfo, or MinSizeRel; current build type is "
            "'${CMAKE_BUILD_TYPE}'.")
    endif()

    cmake_policy(SET CMP0069 NEW)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT _cna_ipo_supported OUTPUT _cna_ipo_failure
        LANGUAGES CXX)
    if(NOT _cna_ipo_supported)
        message(FATAL_ERROR "CNA_ENABLE_IPO was requested but is unsupported:\n${_cna_ipo_failure}")
    endif()

    cna_collect_buildsystem_targets("${CMAKE_SOURCE_DIR}" _cna_all_targets)
    set(_cna_ipo_target_count 0)
    foreach(_cna_target IN LISTS _cna_all_targets)
        cna_is_owned_target("${_cna_target}" _cna_target_is_owned)
        if(NOT _cna_target_is_owned)
            continue()
        endif()
        get_target_property(_cna_target_type "${_cna_target}" TYPE)
        if(NOT _cna_target_type MATCHES
           "^(EXECUTABLE|STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY)$")
            continue()
        endif()
        if(_cna_ipo_configurations)
            foreach(_cna_ipo_configuration IN LISTS _cna_ipo_configurations)
                set_property(TARGET "${_cna_target}"
                    PROPERTY "INTERPROCEDURAL_OPTIMIZATION_${_cna_ipo_configuration}" TRUE)
            endforeach()
        else()
            set_property(TARGET "${_cna_target}" PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
        endif()
        math(EXPR _cna_ipo_target_count "${_cna_ipo_target_count} + 1")
    endforeach()
    message(STATUS "CNA: IPO/LTO enabled for ${_cna_ipo_target_count} owned release targets")
endfunction()
