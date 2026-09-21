# SPDX-License-Identifier: MS-PL
#
# plans/plan_vulkan_parity.md VKPAR-0017: the CNA runtime as one shared library.
#
# With CNA_SHARED_LIBRARY=ON the `CNA` umbrella stops handing its static archives to every
# executable. Instead:
#
#   * cna_shared (libcna.so) is linked from those archives with WHOLE_ARCHIVE -- a shared library has
#     no main(), so without it the linker would pull nothing in and the library would be empty, and
#     every public symbol a test or game may call has to be present;
#   * `CNA` then gives consumers libcna.so, the COMPILE-only usage requirements of the archives
#     (include directories, definitions, options -- $<COMPILE_ONLY>), and the libraries the archives
#     depend on that are NOT inside libcna.so (Vulkan, SDL, X11, sharp-runtime, ...), which a consumer
#     calling those APIs directly still needs on its own link line.
#
# Keeping the CNA archives themselves off consumers' link lines is what stops a one-line change in a
# module .cpp from relinking every executable (an archive is a link input, and CMAKE_LINK_DEPENDS_NO_SHARED
# only exempts shared libraries). It is also what keeps a symbol from being defined twice, in the
# executable and in libcna.so, which would give a static object two copies and run its initialiser
# twice.
#
# "CNA-owned" means a non-imported target whose SOURCE_DIR is under modules/: the source-partition
# validator in modules/CMakeLists.txt already guarantees every production translation unit lives there.

include_guard(GLOBAL)

function(_cna_shared_is_cna_owned out target)
    get_target_property(_imported "${target}" IMPORTED)
    if(_imported)
        set(${out} FALSE PARENT_SCOPE)
        return()
    endif()
    get_target_property(_dir "${target}" SOURCE_DIR)
    cmake_path(IS_PREFIX CNA_SHARED_MODULES_ROOT "${_dir}" NORMALIZE _inside)
    set(${out} ${_inside} PARENT_SCOPE)
endfunction()

# Walks the link closure of the given runtime parts.
#   out_whole     CNA-owned STATIC libraries -- linked WHOLE_ARCHIVE into libcna.so
#   out_external  everything else a consumer may still need to link against directly
#   out_options   INTERFACE_LINK_OPTIONS of the CNA-owned targets walked (sanitizer and linker
#                 flags must still reach an executable's own link step)
function(_cna_shared_walk out_whole out_external out_options)
    set(_queue ${ARGN})
    set(_seen)
    set(_whole)
    set(_external)
    set(_options)
    while(_queue)
        list(POP_FRONT _queue _item)
        if(_item MATCHES "^\\$<LINK_ONLY:([^<>]+)>$")
            set(_item "${CMAKE_MATCH_1}")
        endif()
        if(_item STREQUAL "" OR _item IN_LIST _seen)
            continue()
        endif()
        list(APPEND _seen "${_item}")
        # A generator expression cannot be evaluated at configure time. Passing it on verbatim is
        # always safe: at worst it names an archive that is already inside libcna.so, which then
        # appears after it on the link line and contributes nothing.
        if(_item MATCHES "^\\$<" OR NOT TARGET "${_item}")
            list(APPEND _external "${_item}")
            continue()
        endif()
        get_target_property(_aliased "${_item}" ALIASED_TARGET)
        if(_aliased)
            list(APPEND _queue "${_aliased}")
            continue()
        endif()
        _cna_shared_is_cna_owned(_owned "${_item}")
        get_target_property(_type "${_item}" TYPE)
        if(_owned AND _type MATCHES "^(STATIC_LIBRARY|INTERFACE_LIBRARY|OBJECT_LIBRARY)$")
            if(_type STREQUAL "STATIC_LIBRARY")
                list(APPEND _whole "${_item}")
            endif()
            get_target_property(_deps "${_item}" INTERFACE_LINK_LIBRARIES)
            if(_deps)
                list(APPEND _queue ${_deps})
            endif()
            get_target_property(_link_options "${_item}" INTERFACE_LINK_OPTIONS)
            if(_link_options)
                list(APPEND _options ${_link_options})
            endif()
        else()
            list(APPEND _external "${_item}")
        endif()
    endwhile()
    list(REMOVE_DUPLICATES _options)
    set(${out_whole} "${_whole}" PARENT_SCOPE)
    set(${out_external} "${_external}" PARENT_SCOPE)
    set(${out_options} "${_options}" PARENT_SCOPE)
endfunction()

# Defines cna_shared from the parts and points the `CNA` interface at it. Must run after every target
# in the closure exists and has its final link libraries -- see the DEFER in modules/CMakeLists.txt.
function(cna_define_shared_runtime umbrella)
    set(_parts ${ARGN})
    _cna_shared_walk(_whole _external _options ${_parts})
    if(NOT _whole)
        message(FATAL_ERROR "CNA_SHARED_LIBRARY: found no CNA-owned static library under ${CNA_SHARED_MODULES_ROOT}")
    endif()

    # A shared library needs at least one source; generated so the source-partition validator,
    # which owns every production .cpp under modules/, is not asked to own a placeholder.
    set(_stub "${CMAKE_BINARY_DIR}/generated/cna_shared_library.cpp")
    file(WRITE "${_stub}"
        "// Generated by cmake/SharedRuntimeLibrary.cmake. libcna.so's code comes from the CNA\n"
        "// module archives linked WHOLE_ARCHIVE; this translation unit exists only because a\n"
        "// shared library must have one.\n")
    add_library(cna_shared SHARED "${_stub}")
    add_library(CNA::Shared ALIAS cna_shared)
    set_target_properties(cna_shared PROPERTIES OUTPUT_NAME cna LINKER_LANGUAGE CXX)

    list(JOIN _whole "," _whole_csv)
    target_link_libraries(cna_shared PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,${_whole_csv}>")
    # The parts depend on one another, so CMake also meets them as ordinary transitive items and
    # would refuse the mix of features. The override states which one wins.
    foreach(_lib IN LISTS _whole)
        set_property(TARGET cna_shared PROPERTY "LINK_LIBRARY_OVERRIDE_${_lib}" WHOLE_ARCHIVE)
    endforeach()

    target_link_libraries(${umbrella} INTERFACE cna_shared)
    foreach(_part IN LISTS _parts)
        target_link_libraries(${umbrella} INTERFACE "$<COMPILE_ONLY:${_part}>")
    endforeach()
    if(_external)
        target_link_libraries(${umbrella} INTERFACE ${_external})
    endif()
    if(_options)
        target_link_options(${umbrella} INTERFACE ${_options})
    endif()

    list(LENGTH _whole _whole_count)
    list(LENGTH _external _external_count)
    message(STATUS "CNA: runtime linked as libcna.so -- ${_whole_count} module archives inside, "
                   "${_external_count} external link items passed to consumers")
endfunction()

# The DEFER target: reads what modules/CMakeLists.txt recorded and defines the library.
function(cna_define_shared_runtime_deferred)
    get_property(_parts GLOBAL PROPERTY CNA_SHARED_RUNTIME_PARTS)
    get_property(CNA_SHARED_MODULES_ROOT GLOBAL PROPERTY CNA_SHARED_MODULES_ROOT)
    set(_existing)
    foreach(_part IN LISTS _parts)
        # cna_graphics_ext and cna_devices_ext exist only with CNA_CNAEXT, and the umbrella has always
        # tolerated naming them regardless; keep that tolerance.
        if(TARGET "${_part}")
            list(APPEND _existing "${_part}")
        endif()
    endforeach()
    cna_define_shared_runtime(CNA ${_existing})
endfunction()
