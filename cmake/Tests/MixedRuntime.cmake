# SPDX-License-Identifier: MS-PL
# ISSUE-016: a STATIC framework and OBJECT sidecar must not extract a second CNA.
# These are real module consumers, not a synthetic approximation of the link graph.
if(CNA_SHARED_LIBRARY AND CNA_ENABLE_GAMER_SERVICES AND CMAKE_NM AND CMAKE_READELF
   AND CMAKE_EXECUTABLE_FORMAT STREQUAL "ELF" AND NOT CMAKE_CROSSCOMPILING)
    add_library(cna_mixed_runtime_framework STATIC tests/modules/mixed_runtime_framework.cpp)
    target_link_libraries(cna_mixed_runtime_framework PUBLIC CNA)
    add_library(cna_mixed_runtime_objects OBJECT tests/modules/mixed_runtime_main.cpp)
    target_link_libraries(cna_mixed_runtime_objects PUBLIC
        cna_mixed_runtime_framework CNA::GamerServices)
    add_library(cna_mixed_runtime_opaque_framework STATIC tests/modules/mixed_runtime_opaque_framework.cpp)
    target_link_libraries(cna_mixed_runtime_opaque_framework PUBLIC CNA CNA::GamerServices)
    add_library(cna_mixed_runtime_opaque_objects OBJECT tests/modules/mixed_runtime_opaque_main.cpp)
    target_link_libraries(cna_mixed_runtime_opaque_objects PUBLIC cna_mixed_runtime_opaque_framework)
    add_library(cna_mixed_runtime_unused SHARED tests/modules/mixed_runtime_framework.cpp)

    function(cna_add_mixed_runtime_probe suffix)
        set(_target cna_mixed_runtime_${suffix})
        if(suffix MATCHES "^opaque_")
            add_executable(${_target} $<TARGET_OBJECTS:cna_mixed_runtime_opaque_objects>)
            target_link_options(${_target} PRIVATE "LINKER:--as-needed")
            set(_needed_check -DCNA_CHECK_NEEDED=TRUE -DCNA_READELF=${CMAKE_READELF})
        else()
            add_executable(${_target} $<TARGET_OBJECTS:cna_mixed_runtime_objects>)
        endif()
        target_link_libraries(${_target} PRIVATE ${ARGN})
        if(suffix MATCHES "^opaque_")
            target_link_libraries(${_target} PRIVATE cna_mixed_runtime_unused)
        endif()
        add_test(NAME MixedRuntime_${suffix} COMMAND ${_target})
        add_test(NAME MixedRuntimeSymbols_${suffix}
            COMMAND ${CMAKE_COMMAND} -DCNA_NM=${CMAKE_NM} ${_needed_check}
                -DCNA_PROBE=$<TARGET_FILE:${_target}> -DCNA_RUNTIME=$<TARGET_FILE:cna_shared>
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/Tests/MixedRuntimeSymbols.cmake)
        set_tests_properties(MixedRuntime_${suffix} MixedRuntimeSymbols_${suffix}
            PROPERTIES TIMEOUT 30 LABELS "configuration;shared-runtime")
    endfunction()

    cna_add_mixed_runtime_probe(framework_first cna_mixed_runtime_framework cna_mixed_runtime_objects)
    cna_add_mixed_runtime_probe(objects_first cna_mixed_runtime_objects cna_mixed_runtime_framework)
    cna_add_mixed_runtime_probe(module_first cna_runtime cna_mixed_runtime_objects)
    cna_add_mixed_runtime_probe(alias_first CNA::Runtime cna_mixed_runtime_objects)
    cna_add_mixed_runtime_probe(graphics_first cna_graphics_core cna_mixed_runtime_objects)
    cna_add_mixed_runtime_probe(graphics_alias_first CNA::GraphicsCore cna_mixed_runtime_objects)
    cna_add_mixed_runtime_probe(shared_last CNA::GamerServices cna_shared cna_mixed_runtime_objects)
    cna_add_mixed_runtime_probe(shared_alias_last CNA::GamerServices CNA::Shared cna_mixed_runtime_objects)
    cna_add_mixed_runtime_probe(existing_workaround "$<TARGET_FILE:cna_shared>" cna_mixed_runtime_objects)
    cna_add_mixed_runtime_probe(opaque_framework_first cna_mixed_runtime_opaque_framework cna_mixed_runtime_opaque_objects)
    cna_add_mixed_runtime_probe(opaque_shared_last CNA::GamerServices cna_shared cna_mixed_runtime_opaque_objects)
    include(CheckLinkerFlag)
    check_linker_flag(CXX "-fuse-ld=bfd" CNA_MIXED_RUNTIME_BFD_SUPPORTED)
    if(CNA_MIXED_RUNTIME_BFD_SUPPORTED)
        cna_add_mixed_runtime_probe(opaque_bfd CNA::GamerServices cna_shared cna_mixed_runtime_opaque_objects)
        # Test-only driver selection after CNA's private AUTO linker options.
        # Keep exercising ld.bfd's positional --as-needed even when AUTO uses Mold.
        target_link_libraries(cna_mixed_runtime_opaque_bfd PRIVATE -fuse-ld=bfd)
    endif()

    # cna-engine UP-006: unlike the sidecar fixtures, this library knows only the
    # public module aliases. Its application also links the umbrella, in both orders.
    add_library(cna_mixed_runtime_engine STATIC tests/modules/mixed_runtime_engine.cpp)
    target_link_libraries(cna_mixed_runtime_engine PUBLIC
        CNA::Core CNA::Math CNA::Input CNA::GraphicsCore CNA::Content CNA::Audio CNA::Runtime)
    foreach(_order IN ITEMS engine_first umbrella_first)
        set(_probe cna_mixed_runtime_${_order})
        add_executable(${_probe} tests/modules/mixed_runtime_engine_main.cpp)
        if(_order STREQUAL "engine_first")
            target_link_libraries(${_probe} PRIVATE cna_mixed_runtime_engine CNA)
        else()
            target_link_libraries(${_probe} PRIVATE CNA cna_mixed_runtime_engine)
        endif()
        add_test(NAME MixedRuntime_${_order} COMMAND ${_probe})
        add_test(NAME MixedRuntimeSymbols_${_order}
            COMMAND ${CMAKE_COMMAND} -DCNA_NM=${CMAKE_NM}
                -DCNA_CHECK_ENGINE=TRUE -DCNA_PROBE=$<TARGET_FILE:${_probe}>
                -DCNA_RUNTIME=$<TARGET_FILE:cna_shared>
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/Tests/MixedRuntimeSymbols.cmake)
        set_tests_properties(MixedRuntime_${_order} MixedRuntimeSymbols_${_order}
            PROPERTIES TIMEOUT 30 LABELS "configuration;shared-runtime")
    endforeach()
endif()
