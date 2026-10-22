# SPDX-License-Identifier: MS-PL
cmake_minimum_required(VERSION 3.20)
project(CnaBuildSupportInstalledConsumer LANGUAGES CXX)
enable_testing()
include("${PROBE_PREFIX}/lib/cmake/Probe/ProbeTargets.cmake")
foreach(_kind IN ITEMS archive headers)
    add_executable(consumer_${_kind} main.cpp)
    target_link_libraries(consumer_${_kind} PRIVATE Probe::probe_archive)
    if(_kind STREQUAL "headers")
        target_link_libraries(consumer_${_kind} PRIVATE Probe::probe_headers)
        target_compile_definitions(consumer_${_kind} PRIVATE PROBE_EXCEPTION_TEST)
    endif()
    target_compile_options(consumer_${_kind} PRIVATE -Wall -Wextra -Werror)
    add_test(NAME consumer_${_kind} COMMAND consumer_${_kind})
endforeach()

# Static private instrumentation must retain only link requirements. Header-only
# code still needs compile instrumentation; exception ABI flags are likewise
# retained without a reference to a target in CNA's producer build.
get_target_property(_private_compile Probe::probe_archive INTERFACE_COMPILE_OPTIONS)
if(_private_compile)
    message(FATAL_ERROR "Private archive compile flags leaked: ${_private_compile}")
endif()
get_target_property(_header_compile Probe::probe_headers INTERFACE_COMPILE_OPTIONS)
get_target_property(_archive_link Probe::probe_archive INTERFACE_LINK_OPTIONS)
if(EMSCRIPTEN)
    if(NOT "-fexceptions" IN_LIST _header_compile OR
       NOT "-sDISABLE_EXCEPTION_CATCHING=0" IN_LIST _archive_link)
        message(FATAL_ERROR "Installed exception ABI is missing")
    endif()
else()
    if(NOT _header_compile MATCHES "-fsanitize=undefined" OR
       NOT _archive_link MATCHES "-fsanitize=undefined")
        message(FATAL_ERROR "Installed sanitizer contract is missing")
    endif()
endif()
