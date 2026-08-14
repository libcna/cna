# SPDX-License-Identifier: MS-PL

include(GNUInstallDirs)

add_library(cna_c_api SHARED
    src/CnaCApi.cpp
    src/CnaCApiDetail.cpp
)
add_library(CNA::CApi ALIAS cna_c_api)

set_target_properties(cna_c_api PROPERTIES
    EXPORT_NAME CApi
    OUTPUT_NAME cna_c_api
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)

target_compile_definitions(cna_c_api PRIVATE CNA_C_API_BUILD)
target_include_directories(cna_c_api
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
# Keep the initial ABI substrate narrowly linked to the canonical core module. Each later C API
# family adds the exact CNA module it adapts; linking the whole aggregate before a graphics/runtime
# adapter exists needlessly builds every renderer-facing archive.
target_link_libraries(cna_c_api PRIVATE cna_core)

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
    add_executable(cna_c_api_abi_smoke
        tests/pure_c/AbiSmoke.c
    )
    set_target_properties(cna_c_api_abi_smoke PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_link_libraries(cna_c_api_abi_smoke PRIVATE cna_c_api)
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
    add_test(NAME CApi_ErrorSmoke COMMAND cna_c_api_error_smoke)

    add_library(cna_c_api_header_c OBJECT tests/pure_c/AbiHeaderC.c)
    set_target_properties(cna_c_api_header_c PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_include_directories(cna_c_api_header_c PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

    add_library(cna_c_api_header_cpp OBJECT tests/cpp/AbiHeaderCpp.cpp)
    target_include_directories(cna_c_api_header_cpp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

    add_executable(cna_c_api_handle_registry_test
        tests/cpp/HandleRegistryTest.cpp
        src/CnaCApiDetail.cpp
    )
    target_include_directories(cna_c_api_handle_registry_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    find_package(Threads REQUIRED)
    target_link_libraries(cna_c_api_handle_registry_test PRIVATE Threads::Threads)
    add_test(NAME CApi_HandleRegistry COMMAND cna_c_api_handle_registry_test)
endif()
