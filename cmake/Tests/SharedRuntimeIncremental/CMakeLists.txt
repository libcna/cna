# SPDX-License-Identifier: MS-PL
cmake_minimum_required(VERSION 3.30)
project(CnaSharedRuntimeIncremental LANGUAGES CXX)
include("${CNA_SOURCE_DIR}/cmake/SharedRuntimePolicy.cmake")
include("${CNA_SOURCE_DIR}/cmake/SharedRuntimeLibrary.cmake")
set(CNA_SHARED_MODULES_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
add_library(runtime STATIC data.cpp layout.cpp virtual.cpp impl.cpp)
target_include_directories(runtime PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>" "$<INSTALL_INTERFACE:include>")
add_library(CNA INTERFACE)
if(CNA_FIXTURE_POLICY_ONLY)
    # Isolate LINK_DEPENDS_NO_SHARED from the helper's injected direct library path.
    add_library(cna_shared SHARED data.cpp layout.cpp virtual.cpp impl.cpp)
    set_target_properties(cna_shared PROPERTIES OUTPUT_NAME cna)
    target_link_libraries(CNA INTERFACE cna_shared)
else()
    cna_define_shared_runtime(CNA runtime)
    # A direct module before the shared alias must not extract a second runtime.
    add_library(sidecar STATIC sidecar.cpp)
    target_link_libraries(sidecar PUBLIC CNA)
    add_executable(mixed_consumer mixed_consumer.cpp)
    target_link_options(mixed_consumer PRIVATE "LINKER:--as-needed")
    target_link_libraries(mixed_consumer PRIVATE runtime CNA::Shared sidecar)

    set_target_properties(cna_shared PROPERTIES EXPORT_NAME Shared)
    set_target_properties(runtime PROPERTIES EXPORT_NAME Runtime)
    install(TARGETS cna_shared runtime CNA EXPORT FixtureTargets
        LIBRARY DESTINATION lib COMPONENT CNACpp ARCHIVE DESTINATION lib COMPONENT CNACpp)
    install(EXPORT FixtureTargets NAMESPACE CNA:: DESTINATION lib/cmake/fixture COMPONENT CNACpp)
    install(FILES data_api.hpp DESTINATION include COMPONENT CNACpp)
    file(WRITE "${CMAKE_BINARY_DIR}/features.cmake"
        "set(CMAKE_LINK_LIBRARY_USING_cna_shared_needed \"${CMAKE_LINK_LIBRARY_USING_cna_shared_needed}\")\n"
        "set(CMAKE_LINK_LIBRARY_USING_cna_shared_needed_SUPPORTED TRUE)\n"
        "set(CMAKE_LINK_LIBRARY_cna_shared_needed_ATTRIBUTES \"${CMAKE_LINK_LIBRARY_cna_shared_needed_ATTRIBUTES}\")\n")
    install(FILES "${CMAKE_BINARY_DIR}/features.cmake" DESTINATION lib/cmake/fixture COMPONENT CNACpp)

    # Exercise the C-only package's real archive generator, not just C++ target exports.
    # Defined after the runtime so the tiny fixture needs no optional CNA modules.
    add_library(cna_c_api SHARED EXCLUDE_FROM_ALL c_api.cpp)
    target_link_libraries(cna_c_api PRIVATE CNA)
    set_target_properties(cna_c_api PROPERTIES EXPORT_NAME CApi INSTALL_RPATH "$ORIGIN")
    set_target_properties(cna_shared PROPERTIES INSTALL_RPATH "$ORIGIN")
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_custom_command(
        OUTPUT "${CMAKE_BINARY_DIR}/libcna_c_api_static.a" "${CMAKE_BINARY_DIR}/CNACStaticTargets.cmake"
        COMMAND Python3::Interpreter "${CNA_FIXTURE_SOURCE_DIR}/tools/c-api/generate_static_archive.py"
            --build-dir "${CMAKE_BINARY_DIR}" --module-binary-dir "${CMAKE_BINARY_DIR}"
            --shared-runtime-library "$<TARGET_FILE:cna_shared>"
            --shared-runtime-binary-dir "${CMAKE_BINARY_DIR}"
            --output "${CMAKE_BINARY_DIR}/libcna_c_api_static.a"
            --targets-file "${CMAKE_BINARY_DIR}/CNACStaticTargets.cmake"
            --work-dir "${CMAKE_BINARY_DIR}/static-archive"
        DEPENDS cna_c_api cna_shared
            "${CNA_FIXTURE_SOURCE_DIR}/tools/c-api/generate_static_archive.py"
        VERBATIM)
    add_custom_target(cna_c_api_static
        DEPENDS "${CMAKE_BINARY_DIR}/libcna_c_api_static.a" "${CMAKE_BINARY_DIR}/CNACStaticTargets.cmake")
    install(TARGETS cna_c_api EXPORT CNACTargets LIBRARY DESTINATION lib COMPONENT CNACApi)
    install(TARGETS cna_shared LIBRARY DESTINATION lib COMPONENT CNACApi)
    install(EXPORT CNACTargets NAMESPACE CNA:: DESTINATION lib/cmake/CNA COMPONENT CNACApi)
    install(FILES "${CMAKE_BINARY_DIR}/libcna_c_api_static.a" DESTINATION lib COMPONENT CNACApi)
    install(FILES "${CMAKE_BINARY_DIR}/CNACStaticTargets.cmake"
        DESTINATION lib/cmake/CNA COMPONENT CNACApi)
    install(FILES data_api.hpp DESTINATION include COMPONENT CNACApi)
    include(CMakePackageConfigHelpers)
    set(CNA_INSTALL_LIBDIR lib)
    set(CNA_INSTALL_INCLUDEDIR include)
    configure_package_config_file(
        "${CNA_FIXTURE_SOURCE_DIR}/modules/c-api/cmake/CNAConfig.cmake.in"
        "${CMAKE_BINARY_DIR}/CNAConfig.cmake"
        INSTALL_DESTINATION lib/cmake/CNA PATH_VARS CNA_INSTALL_LIBDIR CNA_INSTALL_INCLUDEDIR)
    install(FILES "${CMAKE_BINARY_DIR}/CNAConfig.cmake" DESTINATION lib/cmake/CNA COMPONENT CNACApi)
endif()
foreach(_name IN ITEMS data layout virtual opaque)
    add_executable(${_name}_consumer ${_name}_consumer.cpp)
    target_link_libraries(${_name}_consumer PRIVATE CNA)
endforeach()
# A module-only consumer must retain ordinary archive dependencies.
add_executable(static_consumer opaque_consumer.cpp)
target_link_libraries(static_consumer PRIVATE runtime)

# Deliberately precompiled input cannot inherit CNA's consumer compile options.
# On x86-64 this must fail the guarded link rather than create COPY or TEXTREL.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
    add_library(nonpic_input OBJECT EXCLUDE_FROM_ALL data_consumer.cpp)
    set_target_properties(nonpic_input PROPERTIES POSITION_INDEPENDENT_CODE OFF)
    target_compile_options(nonpic_input PRIVATE -fno-pic -fno-PIE)
    add_executable(nonpic_consumer EXCLUDE_FROM_ALL $<TARGET_OBJECTS:nonpic_input>)
    target_link_libraries(nonpic_consumer PRIVATE CNA)
    target_link_options(nonpic_consumer PRIVATE -no-pie)
    file(WRITE "${CMAKE_BINARY_DIR}/has-nonpic-check" "yes\n")
endif()
