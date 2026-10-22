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
            # Keep independent cache entries so an existing build tree can switch between
            # MOLD and LLD without find_program reusing the previous candidate's result.
            find_program(CNA_MOLD_LINKER_EXECUTABLE NAMES mold)
            mark_as_advanced(CNA_MOLD_LINKER_EXECUTABLE)
            set(_cna_linker_program "${CNA_MOLD_LINKER_EXECUTABLE}")
            set(_cna_linker_driver_name mold)
        else()
            find_program(CNA_LLD_LINKER_EXECUTABLE NAMES ld.lld lld)
            mark_as_advanced(CNA_LLD_LINKER_EXECUTABLE)
            set(_cna_linker_program "${CNA_LLD_LINKER_EXECUTABLE}")
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

add_library(cna_emscripten_exception_abi INTERFACE)
add_library(CNA::EmscriptenExceptionAbi ALIAS cna_emscripten_exception_abi)
add_library(cna_emscripten_asyncify INTERFACE)
add_library(CNA::EmscriptenAsyncify ALIAS cna_emscripten_asyncify)

# Compatibility composition for the application-style Emscripten contract. New code should name
# the exception ABI and the blocking-loop facility separately: a JavaScript-entered library needs
# the former but must not inherit the latter merely because it links CNA.
add_library(cna_emscripten_abi INTERFACE)
add_library(CNA::EmscriptenAbi ALIAS cna_emscripten_abi)
target_link_libraries(cna_emscripten_abi INTERFACE
    cna_emscripten_exception_abi
    cna_emscripten_asyncify)
if(EMSCRIPTEN)
    # Every C++ frame that can propagate a CNA exception must use the same JS-lowered exception
    # ABI. Keep this target-scoped so ordinary native builds never inherit Emscripten flags.
    target_compile_options(cna_emscripten_exception_abi INTERFACE -fexceptions)
    target_link_options(cna_emscripten_exception_abi INTERFACE
        -fexceptions
        -sDISABLE_EXCEPTION_CATCHING=0
    )

    # Asyncify is an executable/event-loop policy, not part of the exception ABI. Application
    # targets that run blocking Game::Run opt in below; JS-driven library artifacts such as the C
    # API deliberately do not.
    target_link_options(cna_emscripten_asyncify INTERFACE -sASYNCIFY=1)
endif()

add_library(cna_instrumentation INTERFACE)
add_library(CNA::Instrumentation ALIAS cna_instrumentation)

add_library(cna_debug_info_options INTERFACE)
add_library(CNA::DebugInfoOptions ALIAS cna_debug_info_options)

set(CNA_DEBUG_INFO "FULL" CACHE STRING
    "Debug information for CNA-owned Debug targets (FULL, LINE_TABLES, or SPLIT)")
set_property(CACHE CNA_DEBUG_INFO PROPERTY STRINGS FULL LINE_TABLES SPLIT)

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

    # GCC's `undefined` group leaves float->integer conversion overflow (a NaN or out-of-range
    # float cast to an integer type) unchecked; it is a separately named check there, whereas
    # Clang already folds it into `undefined`. Name it explicitly for GCC so an unclamped
    # `static_cast<int>(someFloat)` is reported instead of passing the UBSan builds silently.
    set(_cna_sanitize_flag "${CNA_SANITIZE}")
    list(FIND _cna_sanitizer_list undefined _cna_undefined_sanitizer_index)
    list(FIND _cna_sanitizer_list float-cast-overflow _cna_float_cast_sanitizer_index)
    if(_cna_undefined_sanitizer_index GREATER -1
       AND _cna_float_cast_sanitizer_index EQUAL -1
       AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        string(APPEND _cna_sanitize_flag ",float-cast-overflow")
    endif()

    target_compile_options(cna_instrumentation INTERFACE
        "-fsanitize=${_cna_sanitize_flag}"
        -fno-omit-frame-pointer
        -g)
    target_link_options(cna_instrumentation INTERFACE "-fsanitize=${_cna_sanitize_flag}")
    if(NOT _cna_sanitize_optimization STREQUAL "DEFAULT")
        target_compile_options(cna_instrumentation INTERFACE "-${_cna_sanitize_optimization}")
    endif()
    message(STATUS
        "CNA: sanitizers enabled: ${_cna_sanitize_flag} "
        "(optimization: ${_cna_sanitize_optimization})")
endfunction()

function(cna_configure_debug_info)
    string(TOUPPER "${CNA_DEBUG_INFO}" _cna_debug_info)
    if(NOT _cna_debug_info MATCHES "^(FULL|LINE_TABLES|SPLIT)$")
        message(FATAL_ERROR
            "CNA_DEBUG_INFO must be FULL, LINE_TABLES, or SPLIT "
            "(received '${CNA_DEBUG_INFO}').")
    endif()
    set(CNA_DEBUG_INFO "${_cna_debug_info}" CACHE STRING
        "Debug information for CNA-owned Debug targets (FULL, LINE_TABLES, or SPLIT)" FORCE)
    set_property(CACHE CNA_DEBUG_INFO PROPERTY STRINGS FULL LINE_TABLES SPLIT)

    if(_cna_debug_info STREQUAL "FULL")
        return()
    endif()
    if(CNA_SANITIZE)
        message(FATAL_ERROR
            "CNA_DEBUG_INFO=${_cna_debug_info} cannot be combined with CNA_SANITIZE; sanitizer "
            "presets retain full debug information for diagnostics.")
    endif()
    if(EMSCRIPTEN OR CMAKE_CROSSCOMPILING OR
       NOT CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang)$")
        message(FATAL_ERROR
            "CNA_DEBUG_INFO=${_cna_debug_info} is currently supported only by verified native "
            "GNU/Clang builds. Use CNA_DEBUG_INFO=FULL for this toolchain.")
    endif()
    if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        message(FATAL_ERROR
            "CNA_DEBUG_INFO=${_cna_debug_info} affects only Debug configurations; current build "
            "type is '${CMAKE_BUILD_TYPE}'.")
    endif()

    if(_cna_debug_info STREQUAL "LINE_TABLES")
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            set(_cna_debug_flag -g1)
        else()
            set(_cna_debug_flag -gline-tables-only)
        endif()
    else()
        set(_cna_debug_flag -gsplit-dwarf)
    endif()
    target_compile_options(cna_debug_info_options INTERFACE
        "$<$<CONFIG:Debug>:${_cna_debug_flag}>")
    message(STATUS
        "CNA: Debug information mode ${_cna_debug_info} (${_cna_debug_flag})")
endfunction()

function(cna_link_private_build_support target)
    get_target_property(_cna_target_type "${target}" TYPE)
    if(_cna_target_type STREQUAL "INTERFACE_LIBRARY")
        set(_cna_scope INTERFACE)
    else()
        set(_cna_scope PRIVATE)
    endif()
    foreach(_cna_support IN LISTS ARGN)
        # These parent-owned policy targets must not become dependencies of an
        # installed Sharp Runtime/CNA archive. Keep source-build semantics intact.
        target_link_libraries("${target}" ${_cna_scope} "$<BUILD_INTERFACE:${_cna_support}>")

        # Instrumented objects still require their runtime at the final link;
        # Emscripten frames must keep the configured exception ABI. Embed those
        # usage requirements, not producer-only debug/optimization/linker policy.
        # Capture them after configuration, preserving any nested config genex.
        if(NOT _cna_support MATCHES "^cna_(instrumentation|emscripten_exception_abi)$")
            continue()
        endif()
        if(_cna_target_type MATCHES "^(STATIC_LIBRARY|OBJECT_LIBRARY|INTERFACE_LIBRARY)$")
            get_target_property(_cna_link_options "${_cna_support}" INTERFACE_LINK_OPTIONS)
            if(_cna_link_options)
                target_link_options("${target}" INTERFACE "$<INSTALL_INTERFACE:${_cna_link_options}>")
            endif()
        endif()
        if(_cna_target_type STREQUAL "INTERFACE_LIBRARY")
            get_target_property(_cna_compile_options "${_cna_support}" INTERFACE_COMPILE_OPTIONS)
            if(_cna_compile_options)
                target_compile_options("${target}" INTERFACE "$<INSTALL_INTERFACE:${_cna_compile_options}>")
            endif()
        endif()
    endforeach()
endfunction()

function(cna_apply_sharp_runtime_build_support)
    set(_cna_runtime_support
        cna_instrumentation cna_debug_info_options cna_emscripten_exception_abi cna_size_options)
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
        "${CNA_SOURCE_DIR}" "${_cna_target_source_dir}")
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

    cna_collect_buildsystem_targets("${CNA_SOURCE_DIR}" _cna_all_targets)
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
            cna_debug_info_options
            cna_emscripten_exception_abi
            cna_linker_options
            cna_size_options)
        if(EMSCRIPTEN AND _cna_target_type STREQUAL "EXECUTABLE")
            get_target_property(_cna_asyncify_mode
                "${_cna_target}" CNA_EMSCRIPTEN_ASYNCIFY)
            if(NOT _cna_asyncify_mode STREQUAL "OFF")
                cna_link_private_build_support("${_cna_target}" cna_emscripten_asyncify)
            endif()
        endif()
        math(EXPR _cna_supported_target_count "${_cna_supported_target_count} + 1")
    endforeach()
    message(STATUS
        "CNA: applied private compiler/linker support to ${_cna_supported_target_count} owned targets")
endfunction()

option(CNA_ENABLE_IPO
    "Enable IPO/LTO for CNA-owned release artifacts (not compatible with the static C API package)" OFF)

# Section-level dead stripping (docs/link-size-architecture.md). The module structure decides which
# objects a static link extracts; inside an extracted object every function comes along unless the
# object was compiled into one section per function and the final link collects the unreferenced
# ones. CNA's own objects (and sharp-runtime's) get the sections here; the final links of programs
# that link CNA get the collection through CNA::BuildConfig (modules/CMakeLists.txt). No CNA code
# registers itself through a linker section, so nothing a program uses can be collected.
#
# MSVC already does both in Release (/Gy and /OPT:REF are its defaults) and wasm-ld collects
# unreferenced functions by default, so the option adds nothing there.
option(CNA_OPTIMIZE_FOR_SIZE
    "Compile CNA into per-function sections and drop unreferenced ones in final links" OFF)
add_library(cna_size_options INTERFACE)
add_library(CNA::SizeOptions ALIAS cna_size_options)
if(CNA_OPTIMIZE_FOR_SIZE AND NOT MSVC AND NOT EMSCRIPTEN
   AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(cna_size_options INTERFACE -ffunction-sections -fdata-sections)
    set(CNA_SIZE_GC_LINK_OPTION "$<IF:$<PLATFORM_ID:Darwin,iOS>,-Wl$<COMMA>-dead_strip,-Wl$<COMMA>--gc-sections>")
endif()

# The C++ runtime named ahead of CNA's archives in every final link that consumes CNA
# (docs/link-size-architecture.md, "Linker independence"). From C++20 on, libstdc++ no longer
# declares std::string's members instantiated in its shared library, so each CNA object carries
# weak copies of the ones it uses. mold resolves a symbol that a not-yet-extracted archive member
# and a shared library both define weakly to whichever comes first on the command line, and the
# compiler driver names the runtime last. A std::string member that a C++ shared library needs
# (libshaderc, in a build with SDL_GPU) then extracts the first CNA archive member that defines it:
# in a multi-renderer 2D game, the gamer-services backend, the system Guide and the avatar
# catalog. Named first, the shared runtime satisfies such a reference, and no member is extracted
# for a standard-library symbol alone. As-needed, so it adds no dependency; GNU ld and lld link the
# same members with or without it.
#
# Only for a shared runtime: named first, a shared runtime would replace a static one with lld and
# mold. AUTO decides for each program in each configuration (_cna_note_static_cxx_runtimes). SHARED
# and STATIC decide for every program, whatever AUTO would read, for a runtime chosen where CMake
# cannot show it: a compiler wrapper or spec file, an imported target hidden in a subdirectory.
set(CNA_CXX_RUNTIME_LINKAGE "AUTO" CACHE STRING
    "C++ runtime linkage of the programs that link CNA (AUTO, SHARED or STATIC)")
set_property(CACHE CNA_CXX_RUNTIME_LINKAGE PROPERTY STRINGS AUTO SHARED STATIC)

function(cna_add_cxx_runtime_first_link_option target)
    string(TOUPPER "${CNA_CXX_RUNTIME_LINKAGE}" _cna_linkage)
    if(NOT _cna_linkage MATCHES "^(AUTO|SHARED|STATIC)$")
        message(FATAL_ERROR "CNA_CXX_RUNTIME_LINKAGE must be AUTO, SHARED or STATIC "
            "(received '${CNA_CXX_RUNTIME_LINKAGE}').")
    endif()
    if(_cna_linkage STREQUAL "STATIC" OR NOT CMAKE_EXECUTABLE_FORMAT STREQUAL "ELF" OR ANDROID
       OR NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        return()
    endif()
    set(_cna_runtime_option "")
    foreach(_cna_runtime IN ITEMS stdc++ c++)
        if(_cna_runtime IN_LIST CMAKE_CXX_IMPLICIT_LINK_LIBRARIES)
            set(_cna_runtime_option
                "SHELL:-Wl,--push-state,--as-needed -l${_cna_runtime} -Wl,--pop-state")
            break()
        endif()
    endforeach()
    if(NOT _cna_runtime_option)
        return()
    endif()
    if(_cna_linkage STREQUAL "AUTO")
        # Not in a configuration in which the program's static-runtime flags, which
        # _cna_note_static_cxx_runtimes copies with each flag replaced by CNA_STATIC, apply.
        set(_cna_static "$<GENEX_EVAL:$<TARGET_PROPERTY:CNA_CXX_STATIC_RUNTIME>>")
        set(_cna_runtime_option
            "$<$<NOT:$<IN_LIST:CNA_STATIC,${_cna_static}>>:${_cna_runtime_option}>")
        get_property(_cna_scheduled GLOBAL PROPERTY CNA_CXX_RUNTIME_DETECTION_SCHEDULED)
        if(NOT _cna_scheduled)
            set_property(GLOBAL PROPERTY CNA_CXX_RUNTIME_DETECTION_SCHEDULED TRUE)
            cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}"
                CALL _cna_note_static_cxx_runtimes)
        endif()
    endif()
    target_link_options(${target} INTERFACE "${_cna_runtime_option}")
endfunction()

# Gives every program whose link names a static C++ runtime, or none, a CNA_CXX_STATIC_RUNTIME
# property that evaluates to CNA_STATIC in the configurations in which it does. The property holds
# a copy of each setting that names one, generator expressions kept and the flag replaced by
# CNA_STATIC, so CMake itself decides per configuration: -static-libstdc++ inside
# $<$<CONFIG:Release>:...> counts in Release only. Read: the program's directory's CMAKE_CXX_FLAGS
# and CMAKE_<EXE|SHARED|MODULE>_LINKER_FLAGS with their configuration variants (toolchain and LDFLAGS
# initial values included); its LINK_FLAGS, LINK_OPTIONS and LINK_LIBRARIES; and the
# INTERFACE_LINK_OPTIONS and INTERFACE_LINK_LIBRARIES of every target its libraries reach. A target
# linked only in some configurations is reached in all. -static-libgcc does not count.
function(_cna_note_static_cxx_runtimes)
    set(_cna_static_flag "(^|[^A-Za-z0-9_+-])-(static|static-pie|static-libstdc\\+\\+|nostdlib|nostdlib\\+\\+|nodefaultlibs)($|[^A-Za-z0-9_+-])")
    get_property(_cna_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    if(_cna_multi_config)
        set(_cna_configs ${CMAKE_CONFIGURATION_TYPES})
    else()
        set(_cna_configs ${CMAKE_BUILD_TYPE})
    endif()
    # An imported target that a subdirectory defines is not visible here, so its flags cannot be
    # read. CNA's own, in its source tree, are the system libraries it finds, which choose no
    # runtime; a program that links any other is taken for static, as its runtime is unknown. A
    # build tree inside CNA's source tree is not CNA's; CNA fetched into a build tree still is.
    get_filename_component(_cna_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}" DIRECTORY)
    cmake_path(IS_PREFIX _cna_root "${CMAKE_BINARY_DIR}" NORMALIZE _cna_build_inside)
    set(_cna_hidden "")
    set(_cna_directories "${CMAKE_SOURCE_DIR}")
    while(_cna_directories)
        list(POP_FRONT _cna_directories _cna_directory)
        cmake_path(IS_PREFIX _cna_root "${_cna_directory}" NORMALIZE _cna_own)
        if(_cna_own AND _cna_build_inside)
            cmake_path(IS_PREFIX CMAKE_BINARY_DIR "${_cna_directory}" NORMALIZE _cna_generated)
            if(_cna_generated)
                set(_cna_own FALSE)
            endif()
        endif()
        get_property(_cna_imported DIRECTORY "${_cna_directory}" PROPERTY IMPORTED_TARGETS)
        foreach(_cna_name IN LISTS _cna_imported)
            if(NOT _cna_own AND NOT TARGET "${_cna_name}")
                list(APPEND _cna_hidden "${_cna_name}")
            endif()
        endforeach()
        get_property(_cna_subdirectories DIRECTORY "${_cna_directory}" PROPERTY SUBDIRECTORIES)
        list(APPEND _cna_directories ${_cna_subdirectories})
    endwhile()

    set(_cna_unread_programs "")
    set(_cna_unread_targets "")
    cna_collect_buildsystem_targets("${CMAKE_SOURCE_DIR}" _cna_targets)
    foreach(_cna_target IN LISTS _cna_targets)
        get_target_property(_cna_type ${_cna_target} TYPE)
        if(_cna_type STREQUAL "EXECUTABLE")
            set(_cna_kind EXE)
        elseif(_cna_type STREQUAL "SHARED_LIBRARY")
            set(_cna_kind SHARED)
        elseif(_cna_type STREQUAL "MODULE_LIBRARY")
            set(_cna_kind MODULE)
        else()
            continue()
        endif()
        # The directory's flags and LINK_FLAGS_<CONFIG> hold no generator expressions.
        get_target_property(_cna_directory ${_cna_target} SOURCE_DIR)
        set(_cna_marks "")
        foreach(_cna_config IN ITEMS "" ${_cna_configs})
            string(TOUPPER "${_cna_config}" _cna_config)
            set(_cna_suffix "")
            if(_cna_config)
                set(_cna_suffix "_${_cna_config}")
            endif()
            set(_cna_flags "")
            foreach(_cna_variable IN ITEMS CMAKE_CXX_FLAGS CMAKE_${_cna_kind}_LINKER_FLAGS)
                get_directory_property(_cna_value DIRECTORY "${_cna_directory}"
                    DEFINITION ${_cna_variable}${_cna_suffix})
                string(APPEND _cna_flags " ${_cna_value}")
            endforeach()
            if(_cna_config)
                get_property(_cna_value TARGET ${_cna_target} PROPERTY LINK_FLAGS${_cna_suffix})
                string(APPEND _cna_flags " ${_cna_value}")
            endif()
            if(_cna_flags MATCHES "${_cna_static_flag}")
                if(NOT _cna_config)
                    set(_cna_marks CNA_STATIC)
                    break()
                endif()
                list(APPEND _cna_marks "$<$<CONFIG:${_cna_config}>:CNA_STATIC>")
            endif()
        endforeach()
        if(NOT _cna_marks STREQUAL "CNA_STATIC")
            set(_cna_link "")
            foreach(_cna_property IN ITEMS LINK_FLAGS LINK_OPTIONS LINK_LIBRARIES)
                get_property(_cna_value TARGET ${_cna_target} PROPERTY ${_cna_property})
                list(APPEND _cna_link "${_cna_value}")
            endforeach()
            _cna_static_cxx_runtime_items("${_cna_link}" _cna_items)
            list(APPEND _cna_marks ${_cna_items})
            get_property(_cna_libraries TARGET ${_cna_target} PROPERTY LINK_LIBRARIES)
            _cna_cxx_runtime_link_targets("${_cna_libraries}" _cna_queue _cna_unread)
            set(_cna_reached "")
            while(_cna_queue)
                list(POP_FRONT _cna_queue _cna_dependency)
                if(_cna_dependency IN_LIST _cna_reached)
                    continue()
                endif()
                list(APPEND _cna_reached "${_cna_dependency}")
                if(NOT DEFINED "_cna_read_${_cna_dependency}")
                    set(_cna_libraries "")
                    foreach(_cna_property IN ITEMS INTERFACE_LINK_LIBRARIES
                                                   INTERFACE_LINK_LIBRARIES_DIRECT)
                        get_property(_cna_value TARGET ${_cna_dependency} PROPERTY ${_cna_property})
                        list(APPEND _cna_libraries "${_cna_value}")
                    endforeach()
                    get_property(_cna_value TARGET ${_cna_dependency} PROPERTY INTERFACE_LINK_OPTIONS)
                    _cna_static_cxx_runtime_items("${_cna_value};${_cna_libraries}"
                        "_cna_items_${_cna_dependency}")
                    _cna_cxx_runtime_link_targets("${_cna_libraries}"
                        "_cna_next_${_cna_dependency}" "_cna_unread_${_cna_dependency}")
                    set("_cna_read_${_cna_dependency}" TRUE)
                endif()
                list(APPEND _cna_marks ${_cna_items_${_cna_dependency}})
                list(APPEND _cna_queue ${_cna_next_${_cna_dependency}})
                list(APPEND _cna_unread ${_cna_unread_${_cna_dependency}})
            endwhile()
            if(_cna_unread)
                list(APPEND _cna_marks CNA_STATIC)
                list(APPEND _cna_unread_programs ${_cna_target})
                list(APPEND _cna_unread_targets ${_cna_unread})
            endif()
        endif()
        if(_cna_marks)
            set_property(TARGET ${_cna_target} PROPERTY CNA_CXX_STATIC_RUNTIME "${_cna_marks}")
        endif()
    endforeach()
    if(_cna_unread_programs)
        list(LENGTH _cna_unread_programs _cna_count)
        list(REMOVE_DUPLICATES _cna_unread_targets)
        string(REPLACE ";" ", " _cna_unread_targets "${_cna_unread_targets}")
        message(STATUS "CNA: ${_cna_count} program(s) link imported targets defined in a "
            "subdirectory (${_cna_unread_targets}), whose flags CNA cannot read, and do not get the "
            "runtime-first link option (docs/link-size-architecture.md). If they link the C++ "
            "runtime as a shared library, set CNA_CXX_RUNTIME_LINKAGE=SHARED.")
    endif()
endfunction()

# The items of a link setting that name a static C++ runtime, each such flag replaced by CNA_STATIC.
# An item is a list element, or several where a generator expression holds a list.
function(_cna_static_cxx_runtime_items value output_variable)
    set(_cna_items "")
    set(_cna_item "")
    foreach(_cna_piece IN LISTS value)
        string(APPEND _cna_item "${_cna_piece}")
        string(REGEX MATCHALL "\\$<" _cna_opened "${_cna_item}")
        string(REGEX MATCHALL ">" _cna_closed "${_cna_item}")
        list(LENGTH _cna_opened _cna_opened)
        list(LENGTH _cna_closed _cna_closed)
        if(_cna_opened GREATER _cna_closed)
            string(APPEND _cna_item ";")
            continue()
        endif()
        if(_cna_item MATCHES "${_cna_static_flag}")
            # Until none is left: two flags can share the character between them.
            while(_cna_item MATCHES "${_cna_static_flag}")
                string(REGEX REPLACE "${_cna_static_flag}" "\\1;CNA_STATIC;\\3"
                    _cna_item "${_cna_item}")
            endwhile()
            # Evaluated among link options, where a link-only item is its content.
            string(REPLACE "$<LINK_ONLY:" "$<1:" _cna_item "${_cna_item}")
            list(APPEND _cna_items "${_cna_item}")
        endif()
        set(_cna_item "")
    endforeach()
    set(${output_variable} "${_cna_items}" PARENT_SCOPE)
endfunction()

# The targets a link setting names, inside generator expressions too, with aliases resolved. Names
# of imported targets hidden from the top-level directory go to <hidden_variable>.
function(_cna_cxx_runtime_link_targets value targets_variable hidden_variable)
    string(REGEX MATCHALL "[A-Za-z0-9_.+-]+(::[A-Za-z0-9_.+-]+)*" _cna_names "${value}")
    set(_cna_targets "")
    set(_cna_unread "")
    foreach(_cna_name IN LISTS _cna_names)
        if(TARGET "${_cna_name}")
            get_property(_cna_aliased TARGET "${_cna_name}" PROPERTY ALIASED_TARGET)
            if(_cna_aliased)
                set(_cna_name "${_cna_aliased}")
            endif()
            list(APPEND _cna_targets "${_cna_name}")
        elseif(_cna_name IN_LIST _cna_hidden)
            list(APPEND _cna_unread "${_cna_name}")
        endif()
    endforeach()
    set(${targets_variable} "${_cna_targets}" PARENT_SCOPE)
    set(${hidden_variable} "${_cna_unread}" PARENT_SCOPE)
endfunction()

option(CNA_ENABLE_UNITY_BUILD
    "Enable the measured clean-CI unity-build pilot for the core and math modules" OFF)

function(cna_apply_unity_to_pilot_targets)
    if(NOT CNA_ENABLE_UNITY_BUILD)
        return()
    endif()
    if(EMSCRIPTEN OR CMAKE_CROSSCOMPILING)
        message(FATAL_ERROR
            "CNA_ENABLE_UNITY_BUILD is currently a native clean-CI experiment; disable it for "
            "Emscripten and cross-compiles.")
    endif()
    set(_cna_unity_targets cna_core cna_math)
    foreach(_cna_unity_target IN LISTS _cna_unity_targets)
        if(NOT TARGET "${_cna_unity_target}")
            message(FATAL_ERROR
                "CNA unity pilot target '${_cna_unity_target}' was not created.")
        endif()
        set_target_properties("${_cna_unity_target}" PROPERTIES
            UNITY_BUILD ON
            UNITY_BUILD_MODE GROUP)
    endforeach()

    # Anonymous namespaces from separately compiled files merge inside a unity TU. Keep the
    # six math files that deliberately use the same helper names in different groups; distribute
    # the remaining sources so every math unity file contains at most three implementation files.
    set_source_files_properties(
        "${CMAKE_SOURCE_DIR}/modules/math/src/BoundingBox.cpp"
        "${CMAKE_SOURCE_DIR}/modules/math/src/Matrix.cpp"
        "${CMAKE_SOURCE_DIR}/modules/math/src/Point.cpp"
        DIRECTORY "${CMAKE_SOURCE_DIR}/modules/math" PROPERTIES UNITY_GROUP cna_math_unity_0)
    set_source_files_properties(
        "${CMAKE_SOURCE_DIR}/modules/math/src/BoundingFrustum.cpp"
        "${CMAKE_SOURCE_DIR}/modules/math/src/Plane.cpp"
        "${CMAKE_SOURCE_DIR}/modules/math/src/Quaternion.cpp"
        DIRECTORY "${CMAKE_SOURCE_DIR}/modules/math" PROPERTIES UNITY_GROUP cna_math_unity_1)
    set_source_files_properties(
        "${CMAKE_SOURCE_DIR}/modules/math/src/BoundingSphere.cpp"
        "${CMAKE_SOURCE_DIR}/modules/math/src/Ray.cpp"
        "${CMAKE_SOURCE_DIR}/modules/math/src/Vector2.cpp"
        DIRECTORY "${CMAKE_SOURCE_DIR}/modules/math" PROPERTIES UNITY_GROUP cna_math_unity_2)
    set_source_files_properties(
        "${CMAKE_SOURCE_DIR}/modules/math/src/Color.cpp"
        "${CMAKE_SOURCE_DIR}/modules/math/src/Rectangle.cpp"
        "${CMAKE_SOURCE_DIR}/modules/math/src/Vector3.cpp"
        DIRECTORY "${CMAKE_SOURCE_DIR}/modules/math" PROPERTIES UNITY_GROUP cna_math_unity_3)
    set_source_files_properties(
        "${CMAKE_SOURCE_DIR}/modules/math/src/Curve.cpp"
        "${CMAKE_SOURCE_DIR}/modules/math/src/MathHelper.cpp"
        "${CMAKE_SOURCE_DIR}/modules/math/src/Vector4.cpp"
        DIRECTORY "${CMAKE_SOURCE_DIR}/modules/math" PROPERTIES UNITY_GROUP cna_math_unity_4)
    set_source_files_properties(
        "${CMAKE_SOURCE_DIR}/modules/math/src/CurveKey.cpp"
        "${CMAKE_SOURCE_DIR}/modules/math/src/CurveKeyCollection.cpp"
        DIRECTORY "${CMAKE_SOURCE_DIR}/modules/math" PROPERTIES UNITY_GROUP cna_math_unity_5)

    # The eight core sources have disjoint internal helper names and form one bounded group.
    get_target_property(_cna_core_unity_sources cna_core SOURCES)
    set_source_files_properties(${_cna_core_unity_sources}
        DIRECTORY "${CMAKE_SOURCE_DIR}/modules/core" PROPERTIES UNITY_GROUP cna_core_unity_0)

    # Focused test object libraries are leaf-only and dominate this preset after the module
    # sources are merged. Keep batches small to bound diagnostics, memory and collision scope.
    set(_cna_unity_test_targets cna_core_test_objects cna_math_test_objects)
    foreach(_cna_unity_test_target IN LISTS _cna_unity_test_targets)
        if(TARGET "${_cna_unity_test_target}")
            set_target_properties("${_cna_unity_test_target}" PROPERTIES
                UNITY_BUILD ON
                UNITY_BUILD_BATCH_SIZE 8)
        endif()
    endforeach()
    if(TARGET cna_math_test_objects)
        # Seven independent test files use the conventional anonymous-namespace name kEps.
        # CMake's per-source unity identifier gives each generated include a unique spelling,
        # avoiding a merged-namespace collision without changing the normal test sources.
        set_target_properties(cna_math_test_objects PROPERTIES
            UNITY_BUILD_MODE BATCH
            UNITY_BUILD_UNIQUE_ID CNA_UNITY_MATH_TEST_ID
            UNITY_BUILD_CODE_BEFORE_INCLUDE "#define kEps CNA_UNITY_MATH_TEST_ID"
            UNITY_BUILD_CODE_AFTER_INCLUDE "#undef kEps")
    endif()
    message(STATUS
        "CNA: collision-aware unity-build pilot enabled for ${_cna_unity_targets} "
        "(core group: 8 sources; math groups: at most 3 sources; test batches: 8 sources)")
endfunction()

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

    cna_collect_buildsystem_targets("${CNA_SOURCE_DIR}" _cna_all_targets)
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
