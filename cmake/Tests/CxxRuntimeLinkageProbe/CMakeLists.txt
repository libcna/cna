# SPDX-License-Identifier: MS-PL
# Programs that choose their C++ runtime in each of the usual ways, all linking an interface that
# carries the runtime-first option the way CNA::BuildConfig does (cmake/BuildPerformance.cmake).
# cmake/Tests/CxxRuntimeLinkageCase.cmake configures this, reads the link lines and links some.

cmake_minimum_required(VERSION 3.20)
project(CNACxxRuntimeLinkageProbe LANGUAGES CXX)

include("${CNA_SOURCE_DIR}/cmake/BuildPerformance.cmake")

if(PROBE_LINKER)
    add_link_options(-fuse-ld=${PROBE_LINKER})
endif()

# std::cout is libstdc++'s, so a program needs the shared runtime unless it links it statically.
file(WRITE "${CMAKE_BINARY_DIR}/main.cpp"
    "#include <iostream>\nint main() { std::cout << \"probe\\n\"; return 0; }\n")
file(WRITE "${CMAKE_BINARY_DIR}/module.cpp" "int probe_module() { return 0; }\n")

add_library(probe_build_config INTERFACE)
cna_add_cxx_runtime_first_link_option(probe_build_config)
# Reached through a static library, as a game reaches CNA::BuildConfig through CNA's modules.
add_library(probe_module STATIC "${CMAKE_BINARY_DIR}/module.cpp")
set_target_properties(probe_module PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(probe_module PUBLIC probe_build_config)

function(probe_program name)
    add_executable(${name} "${CMAKE_BINARY_DIR}/main.cpp")
    target_link_libraries(${name} PRIVATE probe_module)
endfunction()

probe_program(plain)
probe_program(libgcc_option)
target_link_options(libgcc_option PRIVATE -static-libgcc)
probe_program(libstdcxx_option)
target_link_options(libstdcxx_option PRIVATE -static-libstdc++)
probe_program(both_options)
target_link_options(both_options PRIVATE -static-libgcc -static-libstdc++)
probe_program(static_option)
target_link_options(static_option PRIVATE -static)
probe_program(static_pie_option)
target_link_options(static_pie_option PRIVATE -static-pie)
probe_program(nostdlib_option)
target_link_options(nostdlib_option PRIVATE -nostdlib++)
probe_program(link_flags)
set_target_properties(link_flags PROPERTIES LINK_FLAGS -static-libstdc++)
probe_program(link_libraries)
target_link_libraries(link_libraries PRIVATE -static-libstdc++)

# Generator expressions, decided in each configuration.
probe_program(config_option)
target_link_options(config_option PRIVATE $<$<CONFIG:Release>:-static-libstdc++>)
probe_program(debug_option)
target_link_options(debug_option PRIVATE $<$<CONFIG:Debug>:-static-libstdc++>)
probe_program(config_libgcc_option)
target_link_options(config_libgcc_option PRIVATE $<$<CONFIG:Release>:-static-libgcc>)
probe_program(if_option)
target_link_options(if_option PRIVATE $<IF:$<CONFIG:Release>,-static-libstdc++,-static-libgcc>)
probe_program(list_option)
target_link_options(list_option PRIVATE "$<$<CONFIG:Release>:-static-libgcc;-static-libstdc++>")
probe_program(bool_option)
target_link_options(bool_option PRIVATE $<$<BOOL:${PROBE_STATIC_RUNTIME}>:-static-libstdc++>)
probe_program(language_option)
target_link_options(language_option PRIVATE $<$<LINK_LANGUAGE:CXX>:-static-libstdc++>)
probe_program(config_libraries)
target_link_libraries(config_libraries PRIVATE $<$<CONFIG:Release>:-static-libstdc++>)

add_library(plain_shared SHARED "${CMAKE_BINARY_DIR}/module.cpp")
target_link_libraries(plain_shared PRIVATE probe_module)
add_library(libstdcxx_shared SHARED "${CMAKE_BINARY_DIR}/module.cpp")
target_link_libraries(libstdcxx_shared PRIVATE probe_module)
target_link_options(libstdcxx_shared PRIVATE -static-libstdc++)

add_subdirectory(consumer)
