# SPDX-License-Identifier: MS-PL

include(GNUInstallDirs)

function(cna_c_api_enable_strict_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endfunction()

add_library(cna_c_api SHARED
    src/CnaCApi.cpp
    src/CnaCApiDetail.cpp
    src/CnaCApiRuntime.cpp
)
add_library(CNA::CApi ALIAS cna_c_api)

set_target_properties(cna_c_api PROPERTIES
    EXPORT_NAME CApi
    OUTPUT_NAME cna_c_api
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)

if(UNIX AND NOT APPLE)
    target_link_options(cna_c_api PRIVATE
        "LINKER:--exclude-libs,ALL"
        "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/cmake/CnaCApiExports.map"
    )
endif()

target_compile_definitions(cna_c_api PRIVATE CNA_C_API_BUILD)
target_include_directories(cna_c_api
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
# Keep the initial ABI substrate narrowly linked to the canonical core module. Each later C API
# family adds the exact CNA module it adapts; linking the whole aggregate before a graphics/runtime
# adapter exists needlessly builds every renderer-facing archive.
target_link_libraries(cna_c_api PRIVATE cna_runtime)

install(TARGETS cna_c_api
    EXPORT CNACTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(EXPORT CNACTargets
    FILE CNACTargets.cmake
    NAMESPACE CNA::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CNA
)

if(CNA_BUILD_TESTS)
    find_package(Threads REQUIRED)

    if(UNIX AND NOT APPLE)
        add_test(NAME CApi_Exports
            COMMAND ${CMAKE_COMMAND}
                -DCNA_C_API_LIBRARY=$<TARGET_FILE:cna_c_api>
                -DCNA_C_API_NM=${CMAKE_NM}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckElfExports.cmake
        )
    endif()

    add_custom_target(cna_c_api_header_audit
        COMMAND ${CMAKE_COMMAND}
            -DCNA_C_API_INCLUDE_DIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckPublicHeaders.cmake
        VERBATIM
    )
    add_test(NAME CApi_HeaderAudit
        COMMAND ${CMAKE_COMMAND}
            -DCNA_C_API_INCLUDE_DIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}/include
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckPublicHeaders.cmake
    )

    add_executable(cna_c_api_abi_smoke
        tests/pure_c/AbiSmoke.c
    )
    set_target_properties(cna_c_api_abi_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_abi_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_abi_smoke)
    add_test(NAME CApi_AbiSmoke COMMAND cna_c_api_abi_smoke)

    add_executable(cna_c_api_error_smoke
        tests/pure_c/ErrorSmoke.c
    )
    set_target_properties(cna_c_api_error_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_error_smoke PRIVATE cna_c_api)
    cna_c_api_enable_strict_warnings(cna_c_api_error_smoke)
    add_test(NAME CApi_ErrorSmoke COMMAND cna_c_api_error_smoke)

    add_executable(cna_c_api_lifecycle_smoke
        tests/pure_c/LifecycleSmoke.c
    )
    set_target_properties(cna_c_api_lifecycle_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_lifecycle_smoke PRIVATE cna_c_api)
    target_link_libraries(cna_c_api_lifecycle_smoke PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_lifecycle_smoke)
    add_test(NAME CApi_LifecycleSmoke COMMAND cna_c_api_lifecycle_smoke)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SDL_RENDERER")
        set_tests_properties(CApi_LifecycleSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        )
    endif()

    add_library(cna_c_api_header_c OBJECT tests/pure_c/AbiHeaderC.c)
    set_target_properties(cna_c_api_header_c PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_include_directories(cna_c_api_header_c PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_dependencies(cna_c_api_header_c cna_c_api_header_audit)
    cna_c_api_enable_strict_warnings(cna_c_api_header_c)

    add_library(cna_c_api_header_cpp OBJECT tests/cpp/AbiHeaderCpp.cpp)
    target_include_directories(cna_c_api_header_cpp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_dependencies(cna_c_api_header_cpp cna_c_api_header_audit)
    cna_c_api_enable_strict_warnings(cna_c_api_header_cpp)

    add_executable(cna_c_api_handle_registry_test
        tests/cpp/HandleRegistryTest.cpp
        src/CnaCApiDetail.cpp
    )
    target_include_directories(cna_c_api_handle_registry_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(cna_c_api_handle_registry_test PRIVATE Threads::Threads)
    cna_c_api_enable_strict_warnings(cna_c_api_handle_registry_test)
    add_test(NAME CApi_HandleRegistry COMMAND cna_c_api_handle_registry_test)

    add_executable(cna_c_api_boundary_detail_test
        tests/cpp/BoundaryDetailTest.cpp
        src/CnaCApi.cpp
        src/CnaCApiDetail.cpp
    )
    target_include_directories(cna_c_api_boundary_detail_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    cna_c_api_enable_strict_warnings(cna_c_api_boundary_detail_test)
    add_test(NAME CApi_BoundaryDetail COMMAND cna_c_api_boundary_detail_test)
endif()
