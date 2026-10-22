# SPDX-License-Identifier: MS-PL
include_guard(GLOBAL)

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
    get_target_property(_cna_imported ${target} IMPORTED)
    if(_cna_imported)
        target_link_options(${target} INTERFACE "${_cna_runtime_option}")
    else()
        # Installed clients choose their own C++ runtime. Reapply this same
        # detector from the package instead of baking in the producer's choice.
        target_link_options(${target} INTERFACE "$<BUILD_INTERFACE:${_cna_runtime_option}>")
    endif()
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
